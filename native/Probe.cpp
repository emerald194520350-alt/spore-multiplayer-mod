// SPDX-License-Identifier: GPL-3.0-or-later
// Experimental Cell co-op adapter. Engine behavior requires manual game testing.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <cstdio>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <Spore/BasicIncludes.h>
#include <Spore/UTFWin/ButtonDrawableStandard.h>
#include <Spore/Simulator/SubSystem/GameTimeManager.h>

#include "CoopNet.h"
#include "CellVisuals.h"
#include "SessionRules.h"
#include "ProgressSync.h"
#include "CellEngineAbi.h"
#include "CellMotionAbi.h"
#include "CellReplicaAbi.h"
#include "CellGrowthAbi.h"
#include "CellProgressAbi.h"
#include "WorldCoordinates.h"
#include "NpcMotion.h"
#include "EditorSync.h"
#include "SaveCompatibility.h"
#include "CoopUi.h"
#include <Spore/Simulator/Cell/cCellGFX.h>
#include <Spore/Editors/BakeManager.h>
#include <cmath>

namespace
{
    void WriteProbeLog(const char* message);
    using CellGraphicsUpdateFunction = void(__cdecl*)();
    CellGraphicsUpdateFunction gCellGraphicsUpdateOriginal = nullptr;
    bool gCellGraphicsHookAttached = false;
    void* gCellGrowthOriginal = nullptr;
    bool gCellGrowthHookAttached = false;
    CoopWorld::Coordinates gWorldCoordinates;
    CoopWorld::Point gGrowthBefore;
    float gGrowthScaleBefore = 0;
    Simulator::cObjectPoolIndex gGrowthAvatar = -1;

    void __cdecl BeforeWorldGrowth()
    {
        auto game = Simulator::Cell::cCellGame::Get();
        auto player = game ? game->mCells.GetIfNotDeleted(game->mAvatarCellIndex) : nullptr;
        gGrowthAvatar = player ? player->Index() : -1;
        gGrowthScaleBefore = player ? player->mTransform.GetScale() : 0;
        if (player) { const auto& p = player->GetPosition(); gGrowthBefore = {p.x,p.y,p.z}; }
    }

    void __cdecl AfterWorldGrowth()
    {
        auto game = Simulator::Cell::cCellGame::Get();
        auto player = game && game->mAvatarCellIndex == gGrowthAvatar
            ? game->mCells.GetIfNotDeleted(gGrowthAvatar) : nullptr;
        if (!player || gGrowthScaleBefore <= 0 || player->mTransform.GetScale() <= 0) return;
        const auto& p = player->GetPosition();
        gWorldCoordinates.Rebase(gGrowthBefore, {p.x,p.y,p.z},
            double(gGrowthScaleBefore)/player->mTransform.GetScale());
    }

    // The engine uses EAX for the pivot and one caller-cleaned stack argument.
    // Preserve that ABI on both sides of the trampoline.
    __declspec(naked) void CellGrowthHook()
    {
        __asm {
            pushfd
            pushad
            call BeforeWorldGrowth
            popad
            popfd
            push dword ptr [esp+4]
            call dword ptr [gCellGrowthOriginal]
            add esp,4
            pushfd
            pushad
            call AfterWorldGrowth
            popad
            popfd
            ret
        }
    }

    CoopNet::Snapshot LocalWorldSnapshot(CoopNet::Snapshot snapshot)
    {
        const auto p = gWorldCoordinates.Decode({snapshot.remoteX,snapshot.remoteY,snapshot.remoteZ});
        snapshot.remoteX = float(p.x); snapshot.remoteY = float(p.y); snapshot.remoteZ = float(p.z);
        snapshot.remoteScale /= float(gWorldCoordinates.unit);
        snapshot.remoteTargetSize /= float(gWorldCoordinates.unit);
        snapshot.remotePose.scale /= float(gWorldCoordinates.unit);
        for (auto& npc : snapshot.remoteNpcs)
        {
            const auto n = gWorldCoordinates.Decode({npc.x,npc.y,npc.z});
            npc.x=float(n.x); npc.y=float(n.y); npc.z=float(n.z);
            npc.scale/=float(gWorldCoordinates.unit); npc.targetSize/=float(gWorldCoordinates.unit);
        }
        return snapshot;
    }

    UpdateMessageListenerPtr gUpdateListener;
    IMessageListenerPtr gEditorListener;
    Simulator::cObjectPoolIndex gRemoteCellIndex = -1;
    uint32_t gRemoteCellModel = 0;
    uint32_t gRemoteCellResource = 0;
    Simulator::cObjectPoolIndex gHostAppearanceProxyIndex = -1;
    uint32_t gHostAppearanceProxyModel = 0;
    bool gLocalPlayerHiddenByProxy = false;
    Simulator::cObjectPoolIndex gHiddenPlayerIndex = -1;
    float gSavedLocalOpacity = 1.0f;
    float gSavedLocalTargetOpacity = 1.0f;
    ULONG64 gRemoteCreateRetryAfter = 0;
    ULONG64 gRemoteRenderWaitSince = 0;
    bool gRemoteRenderDelayed = false;
    bool gJoinedPlayerPlaced = false;
    bool gJoinFailed = false;
    Simulator::cObjectPoolIndex gLocalCellIndex = -1;
    Simulator::cObjectPoolIndex gLocalCellGfxIndex = -1;
    uint32_t gLocalCellResource = 0;
    ResourceKey gLocalSpeciesKey{};
    ULONG64 gLocalCellStableSince = 0;
    ResourceKey gLastSubmittedAppearanceKey{};
    uint32_t gLastSubmittedAppearanceCellResource = 0;
    ULONG64 gLocalAppearanceStableSince = 0;
    uint64_t gAppliedRemoteAppearanceSequence = 0;
    bool gRemoteAppearanceAttempted = false;
    uint32_t gRemoteAppearanceCacheSerial = 0;
    uint64_t gObservedConnectionGeneration = 0;
    uint64_t gObservedWorldGeneration = 0;
    uint64_t gObservedRemotePeerGeneration = 0;
    ResourceKey gRemoteCreationKey{};
    cEditorResourcePtr gRemoteAppearanceResource;
    std::string gLastNetworkError;
    ULONG64 gLastPositionTick = 0;
    ULONG64 gLastProgressTick = 0;
    bool gProgressSeedSent = false;
    CoopProgress::Reconciler gProgressSync;
    bool gSuppressEditorRelay = false;
    bool gWasEditorMode = false;
    uint32_t gMirroredEditorRequestID = 0;
    ULONG64 gMirroredEditorRequestTick = 0;
    bool gAutoJoinAttempted = false;
    ULONG64 gAutoJoinRetryAfter = 0;
    IWindowPtr gInviteButton;
    IWindowPtr gInvitePickerBackdrop;
    IWindowPtr gInvitePickerTitle;
    IWindowPtr gInvitePlayerButton;
    IWindowPtr gInvitePickerCancelButton;
    IWindowPtr gAcceptInviteButton;
    IWindowPtr gDeclineInviteButton;
    IWindowPtr gInvitePromptBackdrop;
    IWindowPtr gInvitePromptTitle;
    IWindowPtr gInvitePromptDetail;
    IWindowPtr gSessionStatus;
    IWindowPtr gSessionEndedPanel;
    IWindowPtr gSessionEndedTitle;
    IWindowPtr gSessionEndedOk;
    bool gReturnAfterInvite = false;
    bool gObservedInviteAccepted = false;
    IWinProcPtr gInviteWinProc;
    bool gInviteResponseSent = false;
    bool gInviteRequestSent = false;
    bool gInvitePickerOpen = false;
    bool gUiUsesRussian = false;
    int gLastHostPauseSent = -1;
    CoopSession::PauseLease gCoopPause;
    ULONG64 gLastSpeciesTick = 0;
    Editors::EditorModel* gObservedEditorModel = nullptr;
    ULONG64 gEditorModelReadySince = 0;
    bool gEditorHistoryPending = false;
    cEditorResourcePtr gEditorEntryResource;
    std::uint64_t gEditorEntrySpeciesSequence = 0;
    bool gEditorInitialModelPending = false;
    std::string gLastLocalSpecies;
    uint64_t gAppliedSpeciesSequence = 0;
    uint64_t gPendingSpeciesSequence = 0;
    std::string gPendingSpecies;
    ULONG64 gLastNpcSnapshotTick = 0;

    struct MirroredNpc
    {
        Simulator::cObjectPoolIndex cellIndex = -1;
        cCellCellResourcePtr resource;
        std::uint64_t seenSequence = 0;
        ResourceKey modelKey{};
        ULONG64 createdAt = 0;
        int appliedHealth = 6;
        std::vector<std::pair<std::uint64_t,int>> pendingDamage;
        std::uint64_t pendingRemoval = 0;
        std::uint64_t appliedAnimationSequence = 0;
        CoopWorld::NpcMotion motion;
    };
    std::map<std::uint32_t, MirroredNpc> gMirroredNpcs;
    std::uint64_t gWorldActionApplied = 0;
    bool gWorldRemovalHookAttached = false;

    bool JoinSavedWorld();
    bool ValidateIncomingSavedWorld(const char* inviterRole);
    void RemoveHostAppearanceProxy(const char* reason);
    std::string SerializeCreation(const ResourceKey& key);
    bool DeserializeEditorResource(const std::string& blob,
        Editors::cEditorResource* resource);

    Simulator::Cell::cCellObjectData* GetLocalPlayerCell()
    {
        if (!Simulator::IsCellGame()) return nullptr;
        auto game = Simulator::Cell::cCellGame::Get();
        return game && CoopVisual::HasCellIndex(game->mAvatarCellIndex)
            ? game->mCells.GetIfNotDeleted(game->mAvatarCellIndex) : nullptr;
    }

    bool SameResourceKey(const ResourceKey& left, const ResourceKey& right)
    {
        return left.instanceID == right.instanceID && left.typeID == right.typeID &&
            left.groupID == right.groupID;
    }

    bool IsProfile2()
    {
        char profile[8]{};
        return GetEnvironmentVariableA("SPORE_COOP_PROFILE", profile,
            static_cast<DWORD>(sizeof(profile))) > 0 && profile[0] == '2';
    }

    using CreateMutexAFunction = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
    CreateMutexAFunction gCreateMutexAOriginal = nullptr;
    bool gMutexDetourAttached = false;

    HANDLE WINAPI CreateMutexAHook(LPSECURITY_ATTRIBUTES attributes,
        BOOL initialOwner, LPCSTR name)
    {
        if (name && name[0])
        {
            char isolatedName[512]{};
            if (sprintf_s(isolatedName, "%s_SporeCoop2", name) > 0)
            {
                return gCreateMutexAOriginal(attributes, initialOwner, isolatedName);
            }
        }
        return gCreateMutexAOriginal(attributes, initialOwner, name);
    }

    member_detour(ProfilePathsDetour, App::cAppSystem,
        void(const char16_t*, const char16_t*))
    {
    public:
        void detoured(const char16_t* creationsFolderName,
            const char16_t* appDataFolderName)
        {
            if (IsProfile2())
            {
                original_function(this, u"My Spore Creations Coop 2", u"SporeCoop2");
                WriteProbeLog("Profile 2 paths selected: SporeCoop2 and My Spore Creations Coop 2.");
                return;
            }
            original_function(this, creationsFolderName, appDataFolderName);
        }
    };

    void WriteProbeLog(const char* message)
    {
        char tempPath[MAX_PATH]{};
        char logPath[MAX_PATH]{};
        if (!GetTempPathA(MAX_PATH, tempPath)) return;
        if (sprintf_s(logPath, "%sSporeCoop.Probe.log", tempPath) <= 0) return;

        HANDLE file = CreateFileA(logPath, FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;

        SYSTEMTIME time{};
        GetLocalTime(&time);
        char line[768]{};
        const int length = sprintf_s(line,
            "[%04u-%02u-%02u %02u:%02u:%02u pid=%lu role=%s] %s\r\n",
            time.wYear, time.wMonth, time.wDay,
            time.wHour, time.wMinute, time.wSecond, GetCurrentProcessId(), CoopNet::GetRole(), message);
        if (length > 0)
        {
            DWORD written = 0;
            WriteFile(file, line, static_cast<DWORD>(length), &written, nullptr);
        }
        CloseHandle(file);
    }

    void Status()
    {
        App::ConsolePrintF("SporeCoop probe: mode=0x%x", Simulator::GetGameModeID());
        if (Simulator::IsCellGame())
        {
            auto game = Simulator::Cell::cCellGame::Get();
            if (!game) return;
            auto player = GetLocalPlayerCell();
            if (!player) return;
            const auto& pos = player->GetPosition();
            App::ConsolePrintF("Cell avatar: %d, position %.2f %.2f %.2f, model 0x%x",
                player->Index(), pos.x, pos.y, pos.z, player->mModelKey.instanceID);
            const auto snapshot = CoopNet::GetSnapshot();
            char visualStatus[640]{};
            sprintf_s(visualStatus, "Cell render: role=%s owner=%s avatar=%d remoteCell=%d mirroredNpcs=%u scale=%.3f opacity=%.3f appearance=%llu",
                CoopNet::GetRole(), snapshot.inviteFrom.c_str(), player->Index(),
                gRemoteCellIndex, static_cast<unsigned>(gMirroredNpcs.size()),
                player->mTransform.GetScale(), player->mOpacity,
                static_cast<unsigned long long>(snapshot.remoteAppearanceSequence));
            App::ConsolePrintF("%s", visualStatus);
            WriteProbeLog(visualStatus);
        }
        else if (Simulator::IsCreatureGame())
        {
            auto manager = Simulator::cGameNounManager::Get();
            if (!manager) return;
            auto player = manager->GetAvatar();
            if (!player) return;
            const auto& pos = player->GetPosition();
            App::ConsolePrintF("Creature avatar: position %.2f %.2f %.2f, species 0x%x",
                pos.x, pos.y, pos.z, player->mSpeciesKey.instanceID);
        }
    }

    bool CacheVisualModel(const std::string& blob, const ResourceKey& sourceKey,
        ResourceKey& key, cEditorResourcePtr& cachedResource)
    {
        cEditorResourcePtr resource = new Editors::cEditorResource();
        if (!DeserializeEditorResource(blob, resource.get())) return false;
        // Foreign creations get a distinct key so matching numeric IDs cannot
        // replace a locally loaded creation from this profile.
        do {
            key = ResourceKey(0x5C0F0000u ^ (++gRemoteAppearanceCacheSerial * 0x9E3779B9u),
                sourceKey.typeID, sourceKey.groupID);
        } while (key.instanceID == 0);
        resource->SetResourceKey(key);
        if (!ResourceManager.CacheResource(resource.get(), true)) return false;
        cachedResource = resource;
        return true;
    }

    using RemoveCellFunction = void(__cdecl*)(int, bool, float, bool);

    RemoveCellFunction GetRemoveCellFunction()
    {
        static RemoveCellFunction function = []() -> RemoveCellFunction {
            const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            const auto address = base + CoopEngine::kRemoveCellRva;
            MEMORY_BASIC_INFORMATION memory{};
            if (!VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory)) ||
                memory.State != MEM_COMMIT || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
                reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize <
                    address + CoopEngine::kRemoveCellCodeSize) return nullptr;
            if (!CoopEngine::MatchesRemovalAbi(reinterpret_cast<const unsigned char*>(address),
                CoopEngine::kRemoveCellCodeSize, address,
                GetAddress(Simulator::Cell::cCellGame, _ptr),
                GetAddress(Simulator::cObjectPool_, GetIfNotDeleted),
                GetAddress(Simulator::cObjectPool_, DeleteObject))) return nullptr;
            return reinterpret_cast<RemoveCellFunction>(address);
        }();
        return function;
    }

    template<std::size_t N>
    void* VerifiedMotionCode(std::uintptr_t rva, std::size_t size, std::uint32_t hash,
        const std::size_t (&relocations)[N])
    {
        const auto base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        const auto address = base + rva;
        MEMORY_BASIC_INFORMATION memory{};
        if (!VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory)) ||
            memory.State != MEM_COMMIT || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
            reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize < address + size)
            return nullptr;
        return CoopEngine::MatchesMotionCode(reinterpret_cast<const unsigned char*>(address),
            size, base, size, hash, relocations) ? reinterpret_cast<void*>(address) : nullptr;
    }

    struct CellMotionFunctions
    {
        using Position = void(__cdecl*)(Simulator::Cell::cCellObjectData*, const Math::Vector3*);
        using Orientation = void(__cdecl*)(Simulator::Cell::cCellObjectData*, const Math::Quaternion*);
        Position position;
        Orientation orientation;
        bool Ready() const { return position && orientation; }
    };

    const CellMotionFunctions& GetCellMotionFunctions()
    {
        static const CellMotionFunctions functions{
            reinterpret_cast<CellMotionFunctions::Position>(VerifiedMotionCode(CoopEngine::kPositionRva,
                CoopEngine::kPositionSize, CoopEngine::kPositionHash, CoopEngine::kPositionRelocations)),
            reinterpret_cast<CellMotionFunctions::Orientation>(VerifiedMotionCode(CoopEngine::kOrientationRva,
                CoopEngine::kOrientationSize, CoopEngine::kOrientationHash, CoopEngine::kOrientationRelocations))
        };
        return functions;
    }

    struct CellProgressFunctions
    {
        void(__cdecl* addFood)(int, bool);
        void(__cdecl* unlockPart)(int);
        void(__cdecl* enterEditor)(bool);
        void(__cdecl* partCinematic)();
        bool Ready() const { return addFood && unlockPart && enterEditor && partCinematic; }
    };

    const CellProgressFunctions& GetCellProgressFunctions()
    {
        static const CellProgressFunctions functions{
            reinterpret_cast<void(__cdecl*)(int,bool)>(VerifiedMotionCode(CoopEngine::kAddFoodRva,
                CoopEngine::kAddFoodSize,CoopEngine::kAddFoodHash,CoopEngine::kAddFoodRelocations)),
            reinterpret_cast<void(__cdecl*)(int)>(VerifiedMotionCode(CoopEngine::kUnlockPartRva,
                CoopEngine::kUnlockPartSize,CoopEngine::kUnlockPartHash,CoopEngine::kUnlockPartRelocations)),
            reinterpret_cast<void(__cdecl*)(bool)>(VerifiedMotionCode(CoopEngine::kEnterCellEditorRva,
                CoopEngine::kEnterCellEditorSize,CoopEngine::kEnterCellEditorHash,CoopEngine::kEnterCellEditorRelocations)),
            reinterpret_cast<void(__cdecl*)()>(VerifiedMotionCode(CoopEngine::kPartCinematicRva,
                CoopEngine::kPartCinematicSize,CoopEngine::kPartCinematicHash,CoopEngine::kPartCinematicRelocations))
        };
        return functions;
    }

    bool MoveCellBody(Simulator::Cell::cCellObjectData* cell, const Math::Vector3& position,
        const Math::Quaternion& orientation)
    {
        const auto& functions = GetCellMotionFunctions();
        if (!cell || !functions.Ready()) return false;
        // Direct SetOffset moves only the object, not its articulated physics
        // nodes. E7C080 recomputes its position from those nodes next frame.
        // The native setters move/rotate the whole body and bump the transform
        // revision; field_C0 is a separate local model transform, not a pose.
        functions.position(cell, &position);
        functions.orientation(cell, &orientation);
        cell->mTargetPosition = position;
        cell->mTargetOrientation = orientation;
        cell->field_84 = position;
        cell->field_90 = Math::Vector3(0, 0, 0);
        return true;
    }

    extern RemoveCellFunction gWorldRemoveOriginal;

    void DestroyCell(Simulator::cObjectPoolIndex index)
    {
        auto game = Simulator::IsCellGame() ? Simulator::Cell::cCellGame::Get() : nullptr;
        auto remove = gWorldRemoveOriginal ? gWorldRemoveOriginal : GetRemoveCellFunction();
        if (game && remove && CoopVisual::HasCellIndex(index) &&
            index != game->mAvatarCellIndex && game->mCells.GetIfNotDeleted(index))
            remove(index, false, 0.0f, false);
    }

    RemoveCellFunction gWorldRemoveOriginal = nullptr;

    bool IsPartCinematic(Simulator::Cell::cCellGame* game)
    {
        // E79720 sets the byte at +518C; E7970D clears it on completion.
        return game && (game->field_518C & 0xff) != 0;
    }

    void __cdecl WorldRemoveHook(Simulator::cObjectPoolIndex index, bool effects, float size, bool immediate)
    {
        auto snapshot=CoopNet::GetSnapshot();
        if (snapshot.connected && snapshot.inviteAccepted && !snapshot.editorOpen &&
            !CoopSession::IsWorldOwner(snapshot,CoopNet::GetRole()) &&
            !IsPartCinematic(Simulator::Cell::cCellGame::Get()))
        {
            auto game=Simulator::Cell::cCellGame::Get();
            auto player=GetLocalPlayerCell();
            auto cell=game ? game->mCells.GetIfNotDeleted(index) : nullptr;
            for (auto& entry:gMirroredNpcs)
            {
                auto& mirror=entry.second;
                if (mirror.cellIndex!=index || !cell || !player || mirror.pendingRemoval) continue;
                cCellCellResourcePtr resource;
                auto data=Simulator::Cell::GetData(cell->mCellResource,resource);
                const auto& p=cell->GetPosition(); const auto& a=player->GetPosition();
                const float radius=std::max(2.0f,(cell->mTransform.GetScale()+player->mTransform.GetScale())*4);
                const bool pickup=data && (data->eat.foodValue>0 || static_cast<int>(data->unlockType)!=0) &&
                    (p.x-a.x)*(p.x-a.x)+(p.y-a.y)*(p.y-a.y)<=radius*radius;
                if (effects || cell->mHealthPoints<=0 || pickup)
                {
                    CoopNet::WorldAction action; action.id=entry.first;
                    action.resource=cell->mCellResource->mInstanceID;
                    action.removed=true; action.effects=effects;
                    mirror.pendingRemoval=CoopNet::SubmitWorldAction(action);
                    // Loot is generated once by the world owner. The guest's
                    // native pickup already contributes its progress delta.
                    effects=false;
                }
                break;
            }
        }
        gWorldRemoveOriginal(index,effects,size,immediate);
    }

    void ApplyWorldActions()
    {
        auto game=Simulator::Cell::cCellGame::Get();
        if (!game || !gWorldRemoveOriginal) return;
        for (const auto& action:CoopNet::TakeWorldActions())
        {
            if (action.sequence<=gWorldActionApplied) continue;
            auto cell=game->mCells.GetIfNotDeleted(static_cast<int>(action.id));
            if (cell && cell->Index()!=game->mAvatarCellIndex && cell->Index()!=gRemoteCellIndex &&
                cell->mCellResource && cell->mCellResource->mInstanceID==action.resource)
            {
                if (action.removed) gWorldRemoveOriginal(cell->Index(),action.effects,cell->mTransform.GetScale(),false);
                else if (action.damage) cell->mHealthPoints=std::max(0,cell->mHealthPoints-action.damage);
            }
            gWorldActionApplied=action.sequence;
        }
    }

    using UnregisterCollisionFunction = void(__cdecl*)(void*, int);

    UnregisterCollisionFunction GetUnregisterCollisionFunction()
    {
        static const auto function = []() -> UnregisterCollisionFunction {
            const auto address = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) +
                CoopEngine::kUnregisterCollisionRva;
            MEMORY_BASIC_INFORMATION memory{};
            if (!VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory)) ||
                memory.State != MEM_COMMIT || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
                reinterpret_cast<std::uintptr_t>(memory.BaseAddress) + memory.RegionSize <
                    address + CoopEngine::kUnregisterCollisionSize) return nullptr;
            return CoopEngine::MatchesStaticCode(reinterpret_cast<const unsigned char*>(address),
                CoopEngine::kUnregisterCollisionSize, CoopEngine::kUnregisterCollisionSize,
                CoopEngine::kUnregisterCollisionHash)
                ? reinterpret_cast<UnregisterCollisionFunction>(address) : nullptr;
        }();
        return function;
    }

    bool InitializeReplica(Simulator::Cell::cCellObjectData* cell, bool interactive = false)
    {
        auto game = Simulator::Cell::cCellGame::Get();
        auto unregister = GetUnregisterCollisionFunction();
        if (!cell || !game || cell->Index() == game->mAvatarCellIndex || !unregister) return false;
        void* broadphase = nullptr;
        static_assert(offsetof(Simulator::Cell::cCellGame, padding_4104) == 0x4104,
            "Cell collision broadphase ABI");
        std::memcpy(&broadphase, game->padding_4104, sizeof(broadphase));
        if (!broadphase) return false;
        const int collisionHandle = cell->field_364;
        if (!interactive && collisionHandle != -1)
        {
            // Unlink through the same engine routine used by DestroyCell.
            // Merely writing -1 leaks a live collider. The native update and
            // destructor both skip an already unregistered handle.
            unregister(broadphase, collisionHandle);
            cell->field_364 = -1;
        }
        cell->mIsIdle = true;
        cell->mIsInvulnerable = !interactive;
        // field_112 is a death flag, NOT a collision/friendly flag:
        // E7A1F6 requests DeathNpc and E660E0 starts cell_death_continuous.
        // Initialize only the new replica; never rewrite the live avatar.
        cell->field_112 = false;
        char line[320]{};
        sprintf_s(line, "ReplicaInit: cell=%d collider=%d->%d dead=%d anim=%d state118=%d health=%d avatar=%d",
            cell->Index(), collisionHandle, cell->field_364, cell->field_112,
            static_cast<int>(cell->mCurrentAnimation), cell->field_118,
            cell->mHealthPoints, game->mAvatarCellIndex);
        WriteProbeLog(line);
        return true;
    }

    Simulator::cObjectPoolIndex CreateCellFromResource(
        Simulator::Cell::cCellDataReference<Simulator::Cell::cCellCellResource>* reference,
        Simulator::Cell::CellStageScale stageScale,
        const Math::Vector3& position,
        const ResourceKey* modelOverride = nullptr, bool interactive = false)
    {
        auto game = Simulator::Cell::cCellGame::Get();
        if (!game || !game->mpCellQuery || !reference || !GetRemoveCellFunction() ||
            !GetCellMotionFunctions().Ready() || !gCellGraphicsHookAttached || !gCellGrowthHookAttached || !gWorldRemovalHookAttached ||
            !GetUnregisterCollisionFunction()) return -1;

        cCellCellResourcePtr cellReference;
        auto cellData = Simulator::Cell::GetData(reference, cellReference);
        if (!cellData || !cellData->structure)
        {
            WriteProbeLog("coop clone aborted: player cell structure is unavailable.");
            return -1;
        }

        cCellStructureResourcePtr structureReference;
        auto structureData = Simulator::Cell::GetData(cellData->structure, structureReference);
        if (!structureData || !structureData->attachments ||
            structureData->numAttachments <= 0)
        {
            WriteProbeLog("coop clone aborted: player model attachment is unavailable.");
            return -1;
        }

        using AttachmentType =
            Simulator::Cell::cCellStructureResource::cSPAttachment::Type;
        Simulator::Cell::cCellStructureResource::cSPAttachment* modelAttachment = nullptr;
        for (int i = 0; i < structureData->numAttachments; ++i)
        {
            auto& attachment = structureData->attachments[i];
            if (attachment.type == AttachmentType::Creature ||
                attachment.type == AttachmentType::RandomCreature ||
                attachment.type == AttachmentType::PlayerCreature)
            {
                modelAttachment = &attachment;
                break;
            }
        }
        if (!modelAttachment && modelOverride && modelOverride->instanceID)
        {
            WriteProbeLog("coop clone aborted: no creature attachment was found.");
            return -1;
        }

        const auto originalAttachmentType = modelAttachment ? modelAttachment->type : AttachmentType::Creature;
        auto serializable = game->mpSerializableData.get();
        ResourceKey originalPlayerKey{};
        if (modelOverride && serializable)
        {
            originalPlayerKey = serializable->mPlayerCreatureKey;
            serializable->mPlayerCreatureKey = *modelOverride;
        }
        if (modelAttachment) modelAttachment->type = AttachmentType::PlayerCreature;
        const auto avatarBefore = game->mAvatarCellIndex;
        const auto index = Simulator::Cell::CreateCellObject(game->mpCellQuery,
            position, 0.0f, reference, stageScale, 1.0f, 1.0f);
        game->mAvatarCellIndex = avatarBefore;
        if (modelAttachment) modelAttachment->type = originalAttachmentType;
        if (modelOverride && serializable)
            serializable->mPlayerCreatureKey = originalPlayerKey;
        auto cell = CoopVisual::HasCellIndex(index) ? game->mCells.GetIfNotDeleted(index) : nullptr;
        if (!InitializeReplica(cell, interactive))
        {
            DestroyCell(index);
            return -1;
        }
        return index;
    }

    Simulator::cObjectPoolIndex CreatePlayerCellClone(
        Simulator::Cell::cCellObjectData* player, const Math::Vector3& position,
        bool remoteControlled, const ResourceKey* modelOverride = nullptr)
    {
        (void)remoteControlled;
        return player ? CreateCellFromResource(player->mCellResource, player->mScale,
            position, modelOverride) : -1;
    }

    void RemoveRemoteCell(const char* reason)
    {
        const auto removed = gRemoteCellIndex;
        auto game = Simulator::Cell::cCellGame::Get();
        if (game && CoopVisual::HasCellIndex(gRemoteCellIndex) &&
            game->mCells.GetIfNotDeleted(gRemoteCellIndex))
            DestroyCell(gRemoteCellIndex);
        gRemoteCellIndex = -1;
        gRemoteCellModel = 0;
        gRemoteCellResource = 0;
        gRemoteRenderWaitSince = 0;
        gRemoteRenderDelayed = false;
        gRemoteCreateRetryAfter = GetTickCount64() + 750;
        if (CoopVisual::HasCellIndex(removed) && reason)
        {
            char line[384]{};
            sprintf_s(line, "Network player cell removed: index=%d reason=%s.", removed, reason);
            WriteProbeLog(line);
        }
    }

    CoopNet::CellPose ReadPlayerRenderPose(Simulator::Cell::cCellObjectData* player)
    {
        CoopNet::CellPose pose;
        const auto& position = player->GetPosition();
        pose.x = position.x; pose.y = position.y; pose.z = position.z;
        pose.scale = player->mTransform.GetScale();
        // A Cell can render entirely through mStructureGFXs. Requiring a main
        // AnimatedCreature incorrectly makes those perfectly visible cells
        // invisible on the peer. Native clones consume the cell transform,
        // including its orientation, rather than a nested model transform.
        pose.visible = player->mOpacity > 0;
        pose.animation = static_cast<uint32_t>(player->mCurrentAnimation);
        const auto orientation = player->mTransform.GetRotation().ToQuaternion();
        const float length = std::sqrt(orientation.x*orientation.x + orientation.y*orientation.y +
            orientation.z*orientation.z + orientation.w*orientation.w);
        if (length > 0.001f)
        {
            pose.qx = orientation.x/length; pose.qy = orientation.y/length;
            pose.qz = orientation.z/length; pose.qw = orientation.w/length;
        }
        return pose;
    }

    CoopNet::CellProgress ReadCellProgress()
    {
        CoopNet::CellProgress result;
        auto game = Simulator::Cell::cCellGame::Get();
        auto data = game ? game->mpSerializableData.get() : nullptr;
        if (!data) return result;
        result.food = std::max(0, data->mFoodProgression);
        result.plantFood = std::max(0, data->mPlantFoodProgression);
        result.overPlantFood = std::max(0, data->mOverPlantFoodProgression);
        result.overAnimalFood = std::max(0, data->mOverAnimalFoodProgression);
        result.spent = std::max(0, data->mEvolutionPointsSpent);
        for (size_t i = 0; i < result.unlocks.size(); ++i)
            result.unlocks[i] = std::max(0, data->mUnlockedParts[i]);
        for (size_t i = 0; i < 6; ++i)
        {
            // Some unused mission slots contain engine sentinel/garbage values.
            // Keep protocol values inside the same defensive range enforced by
            // the server instead of rejecting every later progress update.
            result.missions[i * 4] = std::clamp(data->missions[i].state, 0, 100000000);
            result.missions[i * 4 + 1] = std::clamp(data->missions[i].progress, 0, 100000000);
            result.missions[i * 4 + 2] = std::clamp(data->missions[i].plantProgress, 0, 100000000);
            // +0C is a local floating-point presentation timer, not an integer
            // mission counter. Sending its bit pattern kept restarting popups.
            result.missions[i * 4 + 3] = 0;
        }
        result.killCount = std::max(0, data->mKillCount);
        result.playerHasMoved = data->playerHasMoved;
        result.playerHasEaten = data->playerHasEaten;
        result.partCinematicPlayed = data->mPartCinematicPlayed;
        result.showMateButton = data->mShowMateButton;
        result.firstEditorEntry = data->mFirstEditorEntry;
        return result;
    }

    void ApplyCellProgress(const CoopNet::CellProgress& value)
    {
        auto game = Simulator::Cell::cCellGame::Get();
        auto data = game ? game->mpSerializableData.get() : nullptr;
        if (!data) return;
        const auto& native = GetCellProgressFunctions();
        if (!native.Ready()) return;
        const int oldFood = data->mFoodProgression;
        const int gainedFood = std::max(0, std::min(1000, value.food) - oldFood);
        if (gainedFood)
        {
            // This updates articulated body size, global scale and camera in
            // exactly the same path as a local pickup. A save-field assignment
            // alone leaves the guest avatar at its previous physical size.
            native.addFood(gainedFood, false);
            char line[160]{};
            sprintf_s(line,"Shared native growth: food=%d->%d scale=%.4f",oldFood,data->mFoodProgression,game->field_514C);
            WriteProbeLog(line);
        }
        if (value.partCinematicPlayed && !data->mPartCinematicPlayed)
            native.partCinematic();
        for (size_t i = 0; i < value.unlocks.size(); ++i)
            if (value.unlocks[i] > 0 && data->mUnlockedParts[i] == 0)
            {
                native.unlockPart(static_cast<int>(i));
                char line[100]{};
                sprintf_s(line,"Shared native part unlock: part=%u",static_cast<unsigned>(i));
                WriteProbeLog(line);
            }
        data->mFoodProgression = value.food;
        data->mPlantFoodProgression = value.plantFood;
        data->mOverPlantFoodProgression = value.overPlantFood;
        data->mOverAnimalFoodProgression = value.overAnimalFood;
        data->mEvolutionPointsSpent = value.spent;
        for (size_t i = 0; i < value.unlocks.size(); ++i)
            data->mUnlockedParts[i] = value.unlocks[i];
        for (size_t i = 0; i < 6; ++i)
        {
            data->missions[i].state = value.missions[i * 4];
            data->missions[i].progress = value.missions[i * 4 + 1];
            data->missions[i].plantProgress = value.missions[i * 4 + 2];
        }
        data->mKillCount = value.killCount;
        data->playerHasMoved = value.playerHasMoved;
        data->playerHasEaten = value.playerHasEaten;
        // The native cinematic marks its own completion. Do not suppress it
        // merely because the other window has already played it.
        data->mShowMateButton = value.showMateButton;
        data->mFirstEditorEntry = value.firstEditorEntry;
    }

    void UpdateSharedCellProgress(const CoopNet::Snapshot& snapshot)
    {
        if (!Simulator::IsCellGame() || Simulator::IsEditorMode() || snapshot.editorOpen ||
            !GetLocalPlayerCell()) return;
        auto game = Simulator::Cell::cCellGame::Get();
        auto data = game ? game->mpSerializableData.get() : nullptr;
        if (!data) return;
        const auto current = ReadCellProgress();
        if (!snapshot.progressInitialized)
        {
            if (CoopSession::IsWorldOwner(snapshot, CoopNet::GetRole()) && !gProgressSeedSent)
            {
                CoopNet::SeedProgress(current);
                gProgressSeedSent = true;
                gProgressSync.Initialize(current);
            }
            return;
        }
        if (!gProgressSync.IsInitialized())
        {
            gProgressSync.Initialize(snapshot.progress);
            ApplyCellProgress(snapshot.progress);
            gProgressSync.ObserveApplied(ReadCellProgress());
            return;
        }
        CoopProgress::Event event;
        if (gProgressSync.Capture(current, event, CoopSession::IsWorldOwner(snapshot,CoopNet::GetRole())))
            CoopNet::SubmitProgressDelta(event.sequence, event.delta, current.unlocks);
        ApplyCellProgress(gProgressSync.Reconcile(snapshot.progress, snapshot.progressAckSequence));
        gProgressSync.ObserveApplied(ReadCellProgress());
    }
    bool UpdateLocalCellStability(Simulator::Cell::cCellObjectData* player,
        const ResourceKey& speciesKey, ULONG64 now)
    {
        const uint32_t resource = player && player->mCellResource
            ? player->mCellResource->mInstanceID : 0;
        const auto index = player ? player->Index() : -1;
        const auto gfxIndex = player ? player->mGFXObjectIndex : -1;
        if (index != gLocalCellIndex || gfxIndex != gLocalCellGfxIndex ||
            resource != gLocalCellResource ||
            !SameResourceKey(speciesKey, gLocalSpeciesKey))
        {
            if (CoopVisual::HasCellIndex(gRemoteCellIndex))
                RemoveRemoteCell("local player cell was rebuilt");
            RemoveHostAppearanceProxy("Local player cell was rebuilt; resetting host appearance proxy.");
            gLastSubmittedAppearanceKey = ResourceKey{};
            gLocalCellIndex = index;
            gLocalCellGfxIndex = gfxIndex;
            gLocalCellResource = resource;
            gLocalSpeciesKey = speciesKey;
            gLocalCellStableSince = now;
            gLocalAppearanceStableSince = now;
            return false;
        }
        return now - gLocalCellStableSince >= 750;
    }

    void SubmitLocalAppearance(const ResourceKey& speciesKey, ULONG64 now)
    {
        if (speciesKey.instanceID == 0) return;
        if (SameResourceKey(speciesKey, gLastSubmittedAppearanceKey) &&
            gLocalCellResource == gLastSubmittedAppearanceCellResource) return;
        if (now - gLocalAppearanceStableSince < 750) return;
        const std::string blob = SerializeCreation(speciesKey);
        if (blob.empty())
        {
            gLocalAppearanceStableSince = now;
            WriteProbeLog("Could not serialize the local creature appearance; will retry.");
            return;
        }
        CoopNet::SubmitAppearance(speciesKey.instanceID, speciesKey.typeID,
            speciesKey.groupID, blob);
        gLastSubmittedAppearanceKey = speciesKey;
        gLastSubmittedAppearanceCellResource = gLocalCellResource;
        WriteProbeLog("Submitted the complete local creature appearance.");
    }

    void ApplyRemoteAppearance(const CoopNet::Snapshot& snapshot)
    {
        if (!CoopVisual::AppearanceMatchesPosition(snapshot) ||
            (gRemoteAppearanceAttempted && snapshot.remoteAppearanceSequence <= gAppliedRemoteAppearanceSequence)) return;
        gRemoteAppearanceAttempted = true;
        gAppliedRemoteAppearanceSequence = snapshot.remoteAppearanceSequence;
        RemoveRemoteCell("Peer appearance changed; replacing its render model.");
        RemoveHostAppearanceProxy("Host appearance changed; replacing the guest proxy.");
        gRemoteCreationKey = ResourceKey{};
        gRemoteAppearanceResource = nullptr;
        const ResourceKey sourceKey(snapshot.remoteAppearanceModelInstance,
            snapshot.remoteAppearanceModelType, snapshot.remoteAppearanceModelGroup);
        // Reuse an existing baked creation only after comparing the full resource.
        // Equal numeric IDs alone are not enough across isolated profiles.
        if (SerializeCreation(sourceKey) == snapshot.remoteAppearanceBlob)
        {
            gRemoteCreationKey = sourceKey;
            WriteProbeLog("Peer appearance matches a local creation exactly.");
        }
        else if (CacheVisualModel(snapshot.remoteAppearanceBlob, sourceKey,
            gRemoteCreationKey, gRemoteAppearanceResource))
        {
            WriteProbeLog("Received peer creation cached and submitted for baking.");
        }
        else WriteProbeLog("Peer appearance could not be decoded; local player remains unchanged.");
        // Cell gameplay can already be drawing a creation's eyes and other
        // attachments even when its separately loaded skin mesh has not been
        // baked yet.  Loading that resource immediately produces an
        // eyes-only peer.  Queue/check the complete bake for local matches as
        // well as newly cached remote resources.
        if (gRemoteCreationKey.instanceID)
            if (auto baker = Editors::IBakeManager::Get())
                if (!baker->IsBaked(gRemoteCreationKey, false))
                    baker->BakeModel(gRemoteCreationKey, Editors::BakeParameters(0));
    }

    void UpdateRemoteCell(const CoopNet::Snapshot& snapshot)
    {
        auto game = Simulator::Cell::cCellGame::Get();
        auto player = GetLocalPlayerCell();
        const auto now = GetTickCount64();
        if (!snapshot.inviteAccepted || !game || !player || !snapshot.hasRemotePosition ||
            now - snapshot.remotePositionReceivedTick > 2000)
        {
            if (CoopVisual::HasCellIndex(gRemoteCellIndex))
                RemoveRemoteCell("peer left or stopped publishing positions");
            return;
        }
        const bool owner = CoopSession::IsWorldOwner(snapshot, CoopNet::GetRole());
        const auto sharedSize = CoopVisual::SharedSize(owner, player->mTransform.GetScale(), player->mTargetSize, snapshot);
        // The avatar grows through native food progression. Writing only its
        // transform scale corrupts the relation to its collision/body nodes.
        ApplyRemoteAppearance(snapshot);
        auto serializable = game->mpSerializableData.get();
        const ResourceKey localSpecies = serializable
            ? serializable->mPlayerCreatureKey : player->mModelKey;
        // The invitation sender owns the species, regardless of profile number.
        const ResourceKey visualKey = owner
            ? localSpecies : gRemoteCreationKey;
        if (!visualKey.instanceID) return;
        // Native Cell construction loads its structure graphics itself. Waiting
        // for a standalone creature bake can block forever for playable cells.

        if (!gRemoteRenderWaitSince) gRemoteRenderWaitSince = now;
        if (!CoopVisual::HasCellIndex(gRemoteCellIndex) &&
            now - gRemoteRenderWaitSince > 15000 && !gRemoteRenderDelayed)
        {
            gRemoteRenderDelayed = true;
            WriteProbeLog("Peer cell renderer still not ready after 15 seconds.");
        }
        const auto& pose = snapshot.remotePose;
        // This is a native cell, so feed it simulation coordinates and scale.
        // The renderer applies the structure's own model transform afterward.
        const Math::Vector3 renderedPosition(snapshot.remoteX, snapshot.remoteY, snapshot.remoteZ);
        auto remote = CoopVisual::HasCellIndex(gRemoteCellIndex)
            ? game->mCells.GetIfNotDeleted(gRemoteCellIndex) : nullptr;
        if (remote && (gRemoteCellModel != visualKey.instanceID ||
            gRemoteCellResource != snapshot.remoteCellResource))
        {
            RemoveRemoteCell("peer appearance or cell body changed");
            remote = nullptr;
        }
        if (!remote)
        {
            if (now < gRemoteCreateRetryAfter) return;
            gRemoteCreateRetryAfter = now + 1500;
            gRemoteCellIndex = CreatePlayerCellClone(player, renderedPosition, true, &visualKey);
            gRemoteCellModel = visualKey.instanceID;
            gRemoteCellResource = snapshot.remoteCellResource;
            remote = CoopVisual::HasCellIndex(gRemoteCellIndex)
                ? game->mCells.GetIfNotDeleted(gRemoteCellIndex) : nullptr;
            if (!remote) return;
            gRemoteCellModel = visualKey.instanceID;
            gRemoteCellResource = snapshot.remoteCellResource;
            gRemoteRenderDelayed = false;
            char line[256]{};
            sprintf_s(line, "Full peer cell created: index=%d model=%08X eggState=%d animation=%d hatchTime=%.3f.",
                gRemoteCellIndex, visualKey.instanceID, remote->field_118,
                static_cast<int>(remote->mCurrentAnimation), remote->mSpawnTime);
            WriteProbeLog(line);
        }
        remote->mIsIdle = true;
        remote->mIsInvulnerable = true;
        MoveCellBody(remote, renderedPosition, Math::Quaternion(pose.qx, pose.qy, pose.qz, pose.qw));
        remote->mTransform.SetScale(snapshot.remoteScale);
        remote->mTargetSize = snapshot.remoteTargetSize;
        remote->mOpacity = pose.visible ? 1.0f : 0.0f;
        remote->mTargetOpacity = remote->mOpacity;
    }

    void RemoveHostAppearanceProxy(const char* reason)
    {
        auto game = Simulator::Cell::cCellGame::Get();
        auto player = GetLocalPlayerCell();
        if (player && gLocalPlayerHiddenByProxy && player->Index() == gHiddenPlayerIndex)
        {
            player->mOpacity = gSavedLocalOpacity;
            player->mTargetOpacity = gSavedLocalTargetOpacity;
        }
        const auto removed = gHostAppearanceProxyIndex;
        if (game && CoopVisual::HasCellIndex(removed) &&
            game->mCells.GetIfNotDeleted(removed))
            DestroyCell(removed);
        gHostAppearanceProxyIndex = -1;
        gHostAppearanceProxyModel = 0;
        gLocalPlayerHiddenByProxy = false;
        gHiddenPlayerIndex = -1;
        if (CoopVisual::HasCellIndex(removed) && reason) WriteProbeLog(reason);
    }


    void RemoveMirroredNpcs(const char* reason)
    {
        auto game = Simulator::Cell::cCellGame::Get();
        if (game)
            for (auto& item : gMirroredNpcs)
                if (CoopVisual::HasCellIndex(item.second.cellIndex) &&
                    game->mCells.GetIfNotDeleted(item.second.cellIndex))
                    DestroyCell(item.second.cellIndex);
        const auto count = gMirroredNpcs.size();
        gMirroredNpcs.clear();
        if (count && reason)
        {
            char line[256]{};
            sprintf_s(line, "Removed %u mirrored NPC cells: %s.",
                static_cast<unsigned>(count), reason);
            WriteProbeLog(line);
        }
    }

    std::vector<CoopNet::NpcState> ReadAuthoritativeNpcs(
        Simulator::Cell::cCellObjectData* player)
    {
        std::vector<CoopNet::NpcState> result;
        auto game = Simulator::Cell::cCellGame::Get();
        if (!game || !player) return result;
        Simulator::cObjectPool_::Iterator iterator{};
        while (auto cell = game->mCells.Iterate(iterator))
        {
            if (cell == player || cell->Index() == gRemoteCellIndex ||
                cell->Index() == gHostAppearanceProxyIndex || !cell->mCellResource ||
                cell->mCellResource->mInstanceID == 0) continue;
            const auto& position = cell->GetPosition();
            CoopNet::NpcState npc;
            npc.id = static_cast<std::uint32_t>(cell->Index());
            npc.cellResource = cell->mCellResource->mInstanceID;
            const auto net = gWorldCoordinates.Encode({position.x,position.y,position.z});
            npc.x = float(net.x); npc.y = float(net.y); npc.z = float(net.z);
            const auto orientation = cell->mTransform.GetRotation().ToQuaternion();
            const float planarLength = std::sqrt(orientation.z * orientation.z +
                orientation.w * orientation.w);
            if (planarLength > 0.001f)
            {
                npc.qz = orientation.z / planarLength;
                npc.qw = orientation.w / planarLength;
            }
            npc.scale = std::max(0.001f, cell->mTransform.GetScale()*float(gWorldCoordinates.unit));
            // Zero means no animated resizing for food and fixed scenery.
            npc.targetSize = std::max(0.0f, cell->mTargetSize*float(gWorldCoordinates.unit));
            npc.opacity = std::clamp(cell->mOpacity, 0.0f, 1.0f);
            npc.stageScale = std::clamp(static_cast<int>(cell->mScale), -1, 19);
            npc.modelInstance = cell->mModelKey.instanceID;
            npc.modelType = cell->mModelKey.typeID;
            npc.modelGroup = cell->mModelKey.groupID;
            npc.health=std::clamp(cell->mHealthPoints,0,1000000);
            npc.animation=std::clamp(static_cast<int>(cell->mCurrentAnimation),0,125);
            npc.dead=cell->field_112; npc.elevation=cell->mRelativeElevation;
            result.push_back(npc);
        }
        // The Cell pool holds 4096 entries; two slots belong to the players.
        // Send every loaded object, including food, unlocks and scenery.
        if (result.size() > 4094) result.resize(4094);
        return result;
    }

    void DeleteGuestLocalNpcs(Simulator::Cell::cCellObjectData* player)
    {
        auto game = Simulator::Cell::cCellGame::Get();
        if (!game || !player) return;
        std::set<Simulator::cObjectPoolIndex> retained;
        retained.insert(player->Index());
        if (CoopVisual::HasCellIndex(gRemoteCellIndex)) retained.insert(gRemoteCellIndex);
        if (CoopVisual::HasCellIndex(gHostAppearanceProxyIndex))
            retained.insert(gHostAppearanceProxyIndex);
        for (const auto& item : gMirroredNpcs)
            if (CoopVisual::HasCellIndex(item.second.cellIndex))
                retained.insert(item.second.cellIndex);
        std::vector<Simulator::cObjectPoolIndex> remove;
        Simulator::cObjectPool_::Iterator iterator{};
        while (auto cell = game->mCells.Iterate(iterator))
            if (retained.find(cell->Index()) == retained.end())
                remove.push_back(cell->Index());
        for (const auto index : remove)
            if (game->mCells.GetIfNotDeleted(index)) DestroyCell(index);
    }

    void UpdateHostAppearanceProxy(const CoopNet::Snapshot& snapshot,
        Simulator::Cell::cCellObjectData* player)
    {
        if (CoopSession::IsWorldOwner(snapshot, CoopNet::GetRole()) ||
            !snapshot.inviteAccepted || !snapshot.hasRemotePosition ||
            GetTickCount64() - snapshot.remotePositionReceivedTick > 2000 ||
            !player || !gRemoteCreationKey.instanceID)
        {
            if (CoopVisual::HasCellIndex(gHostAppearanceProxyIndex))
                RemoveHostAppearanceProxy("Host appearance proxy removed.");
            return;
        }
        auto game = Simulator::Cell::cCellGame::Get();
        if (!game) return;
        // Compare the actual avatar's full appearance, not just the save's key.
        // Equal IDs across profiles do not establish equal contents. Cache the
        // comparison until either avatar/graphics or the incoming resource changes.
        static Simulator::cObjectPoolIndex comparedCell = -1, comparedGfx = -1;
        static ResourceKey comparedKey{};
        static std::string comparedOwnerBlob;
        static bool sameAppearance = false;
        if (comparedCell != player->Index() || comparedGfx != player->mGFXObjectIndex ||
            !SameResourceKey(comparedKey, player->mModelKey) ||
            comparedOwnerBlob != snapshot.remoteAppearanceBlob)
        {
            comparedCell = player->Index();
            comparedGfx = player->mGFXObjectIndex;
            comparedKey = player->mModelKey;
            comparedOwnerBlob = snapshot.remoteAppearanceBlob;
            const auto localBlob = SerializeCreation(player->mModelKey);
            sameAppearance = !localBlob.empty() && localBlob == comparedOwnerBlob;
            char line[256]{};
            sprintf_s(line, "ProxyAppearance: avatar=%d localModel=%08X ownerModel=%08X localBytes=%u ownerBytes=%u equal=%d",
                player->Index(), player->mModelKey.instanceID, gRemoteCreationKey.instanceID,
                static_cast<unsigned>(localBlob.size()), static_cast<unsigned>(comparedOwnerBlob.size()), sameAppearance);
            WriteProbeLog(line);
        }
        if (sameAppearance)
        {
            RemoveHostAppearanceProxy("Local species already matches the world owner.");
            return;
        }
        auto proxy = CoopVisual::HasCellIndex(gHostAppearanceProxyIndex)
            ? game->mCells.GetIfNotDeleted(gHostAppearanceProxyIndex) : nullptr;
        if (proxy && gHostAppearanceProxyModel != gRemoteCreationKey.instanceID)
        {
            RemoveHostAppearanceProxy("Host appearance changed; rebuilding local proxy.");
            proxy = nullptr;
        }
        if (!proxy)
        {
            gHostAppearanceProxyIndex = CreatePlayerCellClone(player,
                player->GetPosition(), true, &gRemoteCreationKey);
            gHostAppearanceProxyModel = gRemoteCreationKey.instanceID;
            proxy = CoopVisual::HasCellIndex(gHostAppearanceProxyIndex)
                ? game->mCells.GetIfNotDeleted(gHostAppearanceProxyIndex) : nullptr;
            if (!proxy) return;
            gHostAppearanceProxyModel = gRemoteCreationKey.instanceID;
            WriteProbeLog("Full host appearance proxy created for the guest player.");
        }
        const auto position = player->GetPosition();
        proxy->mIsIdle = true;
        proxy->mIsInvulnerable = true;
        MoveCellBody(proxy, position, player->mTransform.GetRotation().ToQuaternion());
        proxy->mTransform.SetScale(player->mTransform.GetScale());
        proxy->mTargetSize = player->mTargetSize;
        proxy->mOpacity = 1.0f;
        proxy->mTargetOpacity = 1.0f;

        auto gfx = Simulator::Cell::cCellGFX::Get();
        auto visual = gfx
            ? gfx->mCellGFXObjects.GetIfNotDeleted(proxy->mGFXObjectIndex) : nullptr;
        const bool proxyReady = visual && visual->mCellIndex == proxy->Index() &&
            (visual->mpAnimatedCreature || visual->mpModel || visual->mNumStructureGFX > 0);
        if (!proxyReady && gLocalPlayerHiddenByProxy && player->Index() == gHiddenPlayerIndex)
        {
            player->mOpacity = gSavedLocalOpacity;
            player->mTargetOpacity = gSavedLocalTargetOpacity;
            gLocalPlayerHiddenByProxy = false;
            gHiddenPlayerIndex = -1;
        }
        if (proxyReady && !gLocalPlayerHiddenByProxy)
        {
            gSavedLocalOpacity = player->mOpacity;
            gSavedLocalTargetOpacity = player->mTargetOpacity;
            gLocalPlayerHiddenByProxy = true;
            gHiddenPlayerIndex = player->Index();
            WriteProbeLog("Guest local cell now uses the complete host appearance.");
        }
        // Gameplay fades and cell updates can restore the native avatar's
        // opacity. Keep it hidden for as long as the complete host proxy is
        // ready, otherwise two differently coloured bodies overlap.
        if (proxyReady && gLocalPlayerHiddenByProxy)
        {
            player->mOpacity = 0.0f;
            player->mTargetOpacity = 0.0f;
        }
    }

    void UpdateMirroredNpcs(const CoopNet::Snapshot& snapshot,
        Simulator::Cell::cCellObjectData* player)
    {
        auto game = Simulator::Cell::cCellGame::Get();
        const auto now = GetTickCount64();
        // Native first-part scenes own temporary actors and camera targets.
        // Deleting those as stray guest NPCs breaks the scene midway through.
        if (IsPartCinematic(game)) return;
        if (!game || !player || !GetRemoveCellFunction() || !snapshot.inviteAccepted ||
            !snapshot.npcReceivedTick || now - snapshot.npcReceivedTick > 2000)
        {
            RemoveMirroredNpcs("authoritative NPC stream stopped");
            return;
        }

        DeleteGuestLocalNpcs(player);
        for (const auto& state : snapshot.remoteNpcs)
        {
            auto& mirror = gMirroredNpcs[state.id];
            const ResourceKey modelKey(state.modelInstance, state.modelType, state.modelGroup);
            if (mirror.resource && (mirror.resource->mInstanceID != state.cellResource ||
                !SameResourceKey(mirror.modelKey, modelKey)))
            {
                if (CoopVisual::HasCellIndex(mirror.cellIndex) &&
                    game->mCells.GetIfNotDeleted(mirror.cellIndex))
                    DestroyCell(mirror.cellIndex);
                mirror = MirroredNpc{};
            }
            mirror.pendingDamage.erase(std::remove_if(mirror.pendingDamage.begin(), mirror.pendingDamage.end(),
                [&](const auto& event) { return event.first <= snapshot.worldActionAck; }), mirror.pendingDamage.end());
            if (mirror.pendingRemoval && mirror.pendingRemoval <= snapshot.worldActionAck) mirror.pendingRemoval=0;
            // Mark presence even while its engine resource is still loading.
            // Otherwise the next cleanup pass destroys an asynchronous spawn.
            mirror.seenSequence = snapshot.npcSequence;
            mirror.motion.Push(snapshot.npcSequence, snapshot.npcReceivedTick,
                {gWorldCoordinates.Encode({state.x,state.y,state.z}),state.qz,state.qw},
                state.scale*float(gWorldCoordinates.unit));
            if (mirror.pendingRemoval) continue;
            auto cell = CoopVisual::HasCellIndex(mirror.cellIndex)
                ? game->mCells.GetIfNotDeleted(mirror.cellIndex) : nullptr;
            if (!cell)
            {
                if (CoopVisual::HasCellIndex(mirror.cellIndex) && now - mirror.createdAt < 1500)
                    continue;
                cCellCellResourcePtr reference =
                    Simulator::Cell::cCellDataReference<Simulator::Cell::cCellCellResource>::Create(
                        state.cellResource);
                cCellCellResourcePtr loaded;
                if (!reference || !Simulator::Cell::GetData(reference.get(), loaded) || !loaded)
                    continue;
                const Math::Vector3 position(state.x, state.y, state.z);
                mirror.cellIndex = CreateCellFromResource(loaded.get(),
                    static_cast<Simulator::Cell::CellStageScale>(state.stageScale), position, modelKey.instanceID ? &modelKey : nullptr, true);
                mirror.resource = loaded;
                mirror.modelKey = modelKey;
                mirror.createdAt = now;
                cell = CoopVisual::HasCellIndex(mirror.cellIndex)
                    ? game->mCells.GetIfNotDeleted(mirror.cellIndex) : nullptr;
                if (!cell) continue;
                cell->mIsInvulnerable = false;
                mirror.appliedHealth = state.health;
            }
            if (cell->mHealthPoints < mirror.appliedHealth && !state.dead)
            {
                CoopNet::WorldAction action; action.id=state.id; action.resource=state.cellResource;
                action.damage=mirror.appliedHealth-std::max(0,cell->mHealthPoints);
                mirror.pendingDamage.emplace_back(CoopNet::SubmitWorldAction(action),action.damage);
            }
            int pending=0; for (const auto& event:mirror.pendingDamage) pending+=event.second;
            cell->mHealthPoints=std::max(0,state.health-pending);
            mirror.appliedHealth=cell->mHealthPoints;
            cell->field_112=state.dead;
            cell->mRelativeElevation=state.elevation;
            if (mirror.appliedAnimationSequence != snapshot.npcSequence &&
                static_cast<std::uint32_t>(cell->mCurrentAnimation)!=state.animation)
                cell->mCurrentAnimation=static_cast<Simulator::Cell::CellAnimations>(state.animation);
            mirror.appliedAnimationSequence=snapshot.npcSequence;
            mirror.seenSequence = snapshot.npcSequence;
            const auto pose = mirror.motion.Sample(now);
            const auto point = gWorldCoordinates.Decode(pose.position);
            const Math::Vector3 position(float(point.x), float(point.y), float(point.z));
            cell->mIsIdle = true;
            MoveCellBody(cell, position, Math::Quaternion(0, 0, pose.qz, pose.qw));
            cell->mTransform.SetScale(state.scale);
            cell->mTargetSize = state.targetSize;
            cell->mOpacity = state.opacity;
            cell->mTargetOpacity = state.opacity;
        }
        for (auto it = gMirroredNpcs.begin(); it != gMirroredNpcs.end();)
        {
            if (it->second.seenSequence == snapshot.npcSequence) { ++it; continue; }
            if (CoopVisual::HasCellIndex(it->second.cellIndex) &&
                game->mCells.GetIfNotDeleted(it->second.cellIndex))
                DestroyCell(it->second.cellIndex);
            it = gMirroredNpcs.erase(it);
        }
    }

    Simulator::Cell::cCellGFX::CellGFXObjectData* GetCellVisual(
        Simulator::Cell::cCellObjectData* cell)
    {
        auto gfx = Simulator::Cell::cCellGFX::Get();
        if (!gfx || !cell || !CoopVisual::HasCellIndex(cell->mGFXObjectIndex)) return nullptr;
        auto visual = gfx->mCellGFXObjects.GetIfNotDeleted(cell->mGFXObjectIndex);
        return visual && visual->mCellIndex == cell->Index() ? visual : nullptr;
    }

    struct VisualPosition
    {
        Math::Vector3 position{NAN, NAN, NAN};
        float scale = NAN;
        bool ready = false;
    };

    VisualPosition ReadVisualPosition(Simulator::Cell::cCellObjectData* cell)
    {
        auto visual = GetCellVisual(cell);
        if (!visual) return {};
        Graphics::Model* model = visual->mpModel.get();
        if (!model && visual->mpAnimatedCreature) model = visual->mpAnimatedCreature->GetModel();
        if (!model && visual->mpStructure)
        {
            cCellStructureResourcePtr reference;
            auto structure = Simulator::Cell::GetData(visual->mpStructure, reference);
            if (structure && structure->attachments)
            {
                const int count = std::min({10, visual->mNumStructureGFX, structure->numAttachments});
                using Type = Simulator::Cell::cCellStructureResource::cSPAttachment::Type;
                for (int i = 0; i < count && !model; ++i)
                {
                    auto object = visual->mStructureGFXs[i];
                    const auto type = structure->attachments[i].type;
                    if (!object) continue;
                    if (type == Type::Model) model = static_cast<Graphics::Model*>(object);
                    else if (type == Type::Creature || type == Type::RandomCreature || type == Type::PlayerCreature)
                        model = static_cast<Anim::AnimatedCreature*>(object)->GetModel();
                }
            }
        }
        if (!model) return {};
        const auto transform = model->GetTransform();
        return {transform.GetOffset(), transform.GetScale(), true};
    }

    void TraceReplicaGraphics(const char* role,
        Simulator::Cell::cCellObjectData* cell)
    {
        auto visual = GetCellVisual(cell);
        if (!visual) return;
        cCellStructureResourcePtr reference;
        auto structure = visual->mpStructure
            ? Simulator::Cell::GetData(visual->mpStructure, reference) : nullptr;
        char line[384]{};
        sprintf_s(line, "ReplicaGraphics: role=%s cell=%d anim=%d state118=%d health=%d hatchTime=%.3f gfx=%d attachments=%d onHatch=%08X onStartHatch=%08X dead=%d collider=%d requestedAnim=%d deathEffect=%08X",
            role, cell->Index(), static_cast<int>(cell->mCurrentAnimation),
            cell->field_118, cell->mHealthPoints, cell->mSpawnTime,
            cell->mGFXObjectIndex, visual->mNumStructureGFX,
            structure ? structure->onHatch : 0,
            structure ? structure->onStartHatch : 0, cell->field_112, cell->field_364,
            static_cast<int>(cell->field_18C), static_cast<unsigned>(visual->field_AC));
        WriteProbeLog(line);
        if (!structure || !structure->attachments) return;
        const int count = std::min({10, visual->mNumStructureGFX, structure->numAttachments});
        using Type = Simulator::Cell::cCellStructureResource::cSPAttachment::Type;
        for (int i = 0; i < count; ++i)
        {
            const auto& attachment = structure->attachments[i];
            void* object = visual->mStructureGFXs[i];
            if (!object) continue;
            if (attachment.type == Type::Effect)
            {
                auto effect = static_cast<Swarm::IVisualEffect*>(object);
                sprintf_s(line, "ReplicaEffect: role=%s cell=%d slot=%d resource=%08X actual=%08X running=%d hidden=%d",
                    role, cell->Index(), i, attachment.effectID,
                    effect->GetEffectID(), effect->IsRunning(), effect->GetIsHidden());
                WriteProbeLog(line);
            }
            else if (attachment.type == Type::Creature ||
                attachment.type == Type::RandomCreature ||
                attachment.type == Type::PlayerCreature)
            {
                uint32_t animationID = 0;
                auto creature = static_cast<Anim::AnimatedCreature*>(object);
                creature->GetCurrentAnimation(&animationID);
                sprintf_s(line, "ReplicaCreature: role=%s cell=%d slot=%d animationID=%08X",
                    role, cell->Index(), i, animationID);
                WriteProbeLog(line);
            }
        }
    }

    void __cdecl CellGraphicsUpdateHook()
    {
        const auto snapshot = LocalWorldSnapshot(CoopNet::GetSnapshot());
        auto player = GetLocalPlayerCell();
        auto game = player ? Simulator::Cell::cCellGame::Get() : nullptr;
        const auto now = GetTickCount64();
        const bool active = game && snapshot.connected && snapshot.inviteAccepted &&
            !snapshot.editorOpen && snapshot.hasRemotePosition &&
            snapshot.connectionGeneration == gObservedConnectionGeneration &&
            snapshot.worldGeneration == gObservedWorldGeneration &&
            now - snapshot.remotePositionReceivedTick <= 2000;
        Simulator::Cell::cCellObjectData* remote = nullptr;
        Simulator::Cell::cCellObjectData* proxy = nullptr;
        Math::Vector3 before{NAN, NAN, NAN};
        if (active)
        {
            remote = CoopVisual::HasCellIndex(gRemoteCellIndex)
                ? game->mCells.GetIfNotDeleted(gRemoteCellIndex) : nullptr;
            proxy = CoopVisual::HasCellIndex(gHostAppearanceProxyIndex)
                ? game->mCells.GetIfNotDeleted(gHostAppearanceProxyIndex) : nullptr;
            // This boundary is after native simulation and before structure
            // graphics. Reapply only existing bodies; no spawning, deletion or
            // resource loading takes place inside the rendering update.
            if (remote && remote != player)
            {
                before = remote->GetPosition();
                const auto& p = snapshot.remotePose;
                MoveCellBody(remote, Math::Vector3(snapshot.remoteX, snapshot.remoteY, snapshot.remoteZ),
                    Math::Quaternion(p.qx, p.qy, p.qz, p.qw));
                const auto size = CoopVisual::SharedSize(CoopSession::IsWorldOwner(snapshot, CoopNet::GetRole()),
                    player->mTransform.GetScale(), player->mTargetSize, snapshot);
                remote->mTransform.SetScale(size.scale);
                remote->mTargetSize = size.target;
                remote->mOpacity = remote->mTargetOpacity = p.visible ? 1.0f : 0.0f;
            }
            if (proxy && proxy != player && gLocalPlayerHiddenByProxy &&
                player->Index() == gHiddenPlayerIndex)
            {
                MoveCellBody(proxy, player->GetPosition(), player->mTransform.GetRotation().ToQuaternion());
                proxy->mTransform.SetScale(player->mTransform.GetScale());
                player->mOpacity = player->mTargetOpacity = 0.0f;
                proxy->mOpacity = proxy->mTargetOpacity = 1.0f;
            }
            if (!CoopSession::IsWorldOwner(snapshot, CoopNet::GetRole()) &&
                !IsPartCinematic(game) &&
                snapshot.npcReceivedTick && now - snapshot.npcReceivedTick <= 2000)
            {
                for (const auto& state : snapshot.remoteNpcs)
                {
                    auto it = gMirroredNpcs.find(state.id);
                    if (it == gMirroredNpcs.end() || !CoopVisual::HasCellIndex(it->second.cellIndex)) continue;
                    auto cell = game->mCells.GetIfNotDeleted(it->second.cellIndex);
                    if (cell && cell != player && cell != remote && cell != proxy && !it->second.pendingRemoval)
                    {
                        const auto pose = it->second.motion.Sample(now);
                        const auto point = gWorldCoordinates.Decode(pose.position);
                        MoveCellBody(cell, Math::Vector3(float(point.x),float(point.y),float(point.z)),
                            Math::Quaternion(0, 0, pose.qz, pose.qw));
                    }
                }
            }
        }
        gCellGraphicsUpdateOriginal();
        static ULONG64 lastTrace = 0;
        static const bool trace = GetEnvironmentVariableA("SPORE_COOP_TRACE_MOVEMENT", nullptr, 0) != 0;
        if (trace && active && now - lastTrace >= 2000)
        {
            lastTrace = now;
            const auto remoteVisual = ReadVisualPosition(remote);
            const auto localVisual = ReadVisualPosition(player);
            const auto proxyVisual = ReadVisualPosition(proxy);
            const Math::Vector3 absent{NAN, NAN, NAN};
            const auto remotePosition = remote ? remote->GetPosition() : absent;
            const auto proxyPosition = proxy ? proxy->GetPosition() : absent;
            char line[640]{};
            sprintf_s(line, "Sync: phase=after-cell-gfx seq=%llu age=%llu received=(%.3f,%.3f) before=(%.3f,%.3f) cell=(%.3f,%.3f) gfx=(%.3f,%.3f) gfxReady=%d nodes=%d scale=%.3f gfxScale=%.3f",
                static_cast<unsigned long long>(snapshot.remotePositionSequence),
                static_cast<unsigned long long>(now - snapshot.remotePositionReceivedTick),
                snapshot.remoteX, snapshot.remoteY, before.x, before.y, remotePosition.x, remotePosition.y,
                remoteVisual.position.x, remoteVisual.position.y, remoteVisual.ready,
                remote ? remote->field_250 : 0, remote ? remote->mTransform.GetScale() : 0, remoteVisual.scale);
            WriteProbeLog(line);
            sprintf_s(line, "LocalVisual: avatar=%d local=(%.3f,%.3f) localGfx=(%.3f,%.3f) hidden=%d proxy=%d proxyCell=(%.3f,%.3f) proxyGfx=(%.3f,%.3f) proxyReady=%d npcs=%u/%u",
                player->Index(), player->GetPosition().x, player->GetPosition().y,
                localVisual.position.x, localVisual.position.y, gLocalPlayerHiddenByProxy,
                gHostAppearanceProxyIndex, proxyPosition.x, proxyPosition.y,
                proxyVisual.position.x, proxyVisual.position.y, proxyVisual.ready,
                static_cast<unsigned>(gMirroredNpcs.size()), static_cast<unsigned>(snapshot.remoteNpcs.size()));
            WriteProbeLog(line);
            sprintf_s(line, "ReplicaPhysics: avatar=%d avatarCollider=%d proxy=%d proxyCollider=%d separation=%.4f impulse=(%.4f,%.4f)",
                player->Index(), player->field_364, gHostAppearanceProxyIndex,
                proxy ? proxy->field_364 : -1,
                proxy ? std::hypot(proxyPosition.x - player->GetPosition().x,
                    proxyPosition.y - player->GetPosition().y) : NAN,
                player->field_90.x, player->field_90.y);
            WriteProbeLog(line);
            TraceReplicaGraphics("remote", remote);
            TraceReplicaGraphics("local-proxy", proxy);
            if (!CoopSession::IsWorldOwner(snapshot, CoopNet::GetRole()))
            {
                int traced = 0;
                for (const auto& item : gMirroredNpcs)
                {
                    if (traced++ >= 2) break;
                    auto npc = CoopVisual::HasCellIndex(item.second.cellIndex)
                        ? game->mCells.GetIfNotDeleted(item.second.cellIndex) : nullptr;
                    TraceReplicaGraphics("npc", npc);
                }
            }
        }
    }

    std::string Base64Encode(const std::vector<unsigned char>& bytes)
    {
        static constexpr char alphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        result.reserve(((bytes.size() + 2) / 3) * 4);
        for (size_t i = 0; i < bytes.size(); i += 3)
        {
            const unsigned a = bytes[i];
            const unsigned b = i + 1 < bytes.size() ? bytes[i + 1] : 0;
            const unsigned c = i + 2 < bytes.size() ? bytes[i + 2] : 0;
            const unsigned value = (a << 16) | (b << 8) | c;
            result.push_back(alphabet[(value >> 18) & 63]);
            result.push_back(alphabet[(value >> 12) & 63]);
            result.push_back(i + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '=');
            result.push_back(i + 2 < bytes.size() ? alphabet[value & 63] : '=');
        }
        return result;
    }

    bool Base64Decode(const std::string& text, std::vector<unsigned char>& bytes)
    {
        if (text.empty() || text.size() % 4 != 0) return false;
        std::array<int, 256> table{};
        table.fill(-1);
        const char* alphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i)
            table[static_cast<unsigned char>(alphabet[i])] = i;

        std::vector<unsigned char> result;
        result.reserve((text.size() / 4) * 3);
        for (size_t i = 0; i < text.size(); i += 4)
        {
            const bool pad2 = text[i + 2] == '=';
            const bool pad3 = text[i + 3] == '=';
            if ((pad2 && !pad3) || (i + 4 != text.size() && (pad2 || pad3))) return false;
            const int a = table[static_cast<unsigned char>(text[i])];
            const int b = table[static_cast<unsigned char>(text[i + 1])];
            const int c = pad2 ? 0 : table[static_cast<unsigned char>(text[i + 2])];
            const int d = pad3 ? 0 : table[static_cast<unsigned char>(text[i + 3])];
            if (a < 0 || b < 0 || c < 0 || d < 0) return false;
            const unsigned value = (static_cast<unsigned>(a) << 18) |
                (static_cast<unsigned>(b) << 12) |
                (static_cast<unsigned>(c) << 6) | static_cast<unsigned>(d);
            result.push_back(static_cast<unsigned char>((value >> 16) & 0xff));
            if (!pad2) result.push_back(static_cast<unsigned char>((value >> 8) & 0xff));
            if (!pad3) result.push_back(static_cast<unsigned char>(value & 0xff));
        }
        bytes = std::move(result);
        return true;
    }

    template <typename T>
    void AppendBinary(std::vector<unsigned char>& bytes, const T& value)
    {
        const auto* start = reinterpret_cast<const unsigned char*>(&value);
        bytes.insert(bytes.end(), start, start + sizeof(T));
    }

    std::string SerializeEditorResource(Editors::cEditorResource* resource)
    {
        if (!resource) return {};
        const uint32_t count = static_cast<uint32_t>(resource->mBlocks.size());
        if (count > 512) return {};

        std::vector<unsigned char> bytes;
        bytes.reserve(8 + sizeof(resource->mProperties) +
            static_cast<size_t>(count) * sizeof(Editors::cEditorResourceBlock));
        const uint32_t magic = 0x31504353; // SCP1
        AppendBinary(bytes, magic);
        AppendBinary(bytes, count);
        AppendBinary(bytes, resource->mProperties);
        for (const auto& block : resource->mBlocks)
            AppendBinary(bytes, block);
        if (bytes.size() > 262144) return {};
        return Base64Encode(bytes);
    }

    bool DeserializeEditorResource(const std::string& blob,
        Editors::cEditorResource* resource)
    {
        if (!resource) return false;
        std::vector<unsigned char> bytes;
        if (!Base64Decode(blob, bytes) ||
            bytes.size() < 8 + sizeof(Editors::cEditorResourceProperties)) return false;

        uint32_t magic = 0;
        uint32_t count = 0;
        std::memcpy(&magic, bytes.data(), sizeof(magic));
        std::memcpy(&count, bytes.data() + 4, sizeof(count));
        const size_t expected = 8 + sizeof(Editors::cEditorResourceProperties) +
            static_cast<size_t>(count) * sizeof(Editors::cEditorResourceBlock);
        if (magic != 0x31504353 || count > 512 || bytes.size() != expected) return false;

        size_t offset = 8;
        std::memcpy(&resource->mProperties, bytes.data() + offset,
            sizeof(resource->mProperties));
        offset += sizeof(resource->mProperties);
        resource->mBlocks.resize(count);
        if (count)
            std::memcpy(resource->mBlocks.data(), bytes.data() + offset,
                static_cast<size_t>(count) * sizeof(Editors::cEditorResourceBlock));
        return true;
    }

    std::string SerializeCreation(const ResourceKey& key)
    {
        ResourceObjectPtr raw;
        if (!ResourceManager.GetResource(key, &raw) || !raw) return {};
        auto resource = object_cast<Editors::cEditorResource>(raw.get());
        return SerializeEditorResource(resource);
    }

    std::string SerializeEditorModel()
    {
        auto editor = Editors::GetEditor();
        if (!editor || !editor->IsActive() || !editor->GetEditorModel()) return {};

        // Native history is the completed transaction (including undo/redo).
        // A selected/hovered handle must not block sending an already placed
        // part, nor may a palette drag publish a half-constructed body.
        const int index = CoopEditor::HistorySlot(editor->mEditHistoryIndex,editor->mEditHistory.size());
        if (!gEditorHistoryPending && index >= 0 &&
            index < static_cast<int>(editor->mEditHistory.size()) && editor->mEditHistory[index])
            return SerializeEditorResource(editor->mEditHistory[index].get());
        if (!editor->GetEditorModel()->mbAllBlocksLoaded || editor->mpMovingPart ||
            editor->mMouseState.IsLeftButtonDown) return {};

        cEditorResourcePtr resource = new Editors::cEditorResource();
        editor->GetEditorModel()->Save(resource.get());
        return SerializeEditorResource(resource.get());
    }

    bool ApplyEditorModel(const std::string& blob)
    {
        auto editor = Editors::GetEditor();
        if (!editor || !editor->IsActive() || !editor->GetEditorModel() ||
            !editor->GetEditorModel()->mbAllBlocksLoaded || editor->mpMovingPart ||
            editor->mMouseState.IsLeftButtonDown)
            return false;
        cEditorResourcePtr resource = new Editors::cEditorResource();
        if (!DeserializeEditorResource(blob, resource.get()) || resource->mBlocks.empty()) return false;

        // SetEditorModel first calls Dispose on the CURRENT model (586BF7).
        // Loading into that same object then reattaching it destroys its body.
        // Load a separate model; SetEditorModel acquires its native reference.
        auto model = new Editors::EditorModel();
        auto previous = editor->GetEditorModel();
        model->mKey = previous->mKey;
        model->Load(resource.get());
        if (model->mRigblocks.empty()) { delete model; return false; }
        // SCP1 carries body and paint; names are local editor metadata. Load
        // clears them, so preserve them before the old model is disposed.
        model->mName = previous->mName;
        model->mDescription = previous->mDescription;
        model->mAcceptedName = previous->mAcceptedName;
        editor->SetEditorModel(model);
        model->mSkinNeedsUpdating = true;
        gEditorHistoryPending = true;
        gObservedEditorModel = model;
        gEditorModelReadySince = GetTickCount64();
        WriteProbeLog("Applied a remote editor model snapshot.");
        return true;
    }

    bool OpenMirroredEditor(const CoopNet::Snapshot& snapshot)
    {
        if (!Simulator::IsCellGame()) return false;
        auto game = Simulator::Cell::cCellGame::Get();
        auto data = game ? game->mpSerializableData.get() : nullptr;
        const auto& native = GetCellProgressFunctions();
        if (!data || !GetLocalPlayerCell() || !native.Ready() || snapshot.speciesBlob.empty()) return false;

        ResourceKey sharedKey;
        if (!CacheVisualModel(snapshot.speciesBlob, data->mPlayerCreatureKey, sharedKey, gEditorEntryResource))
            return false;
        data->mPlayerCreatureKey = sharedKey;
        gEditorEntrySpeciesSequence = snapshot.speciesSequence;
        gEditorInitialModelPending = true;
        gSuppressEditorRelay = true;
        // Cell bypasses kMsgEnterEditor. Its native campaign entry prepares
        // the palette, DNA budget, editor settings and return-to-Cell callback.
        native.enterEditor(false);
        gSuppressEditorRelay = false;
        WriteProbeLog("Requested native Cell campaign editor with the complete shared species.");
        return true;
    }

    class EditorRelayListener : public App::DefaultMessageListener
    {
    public:
        bool HandleMessage(uint32_t messageID, void* value) override
        {
            if (messageID != Simulator::kMsgEnterEditor || !value || gSuppressEditorRelay)
                return false;
            const auto snapshot = CoopNet::GetSnapshot();
            if (!snapshot.enabled || !snapshot.connected || !snapshot.inviteAccepted) return false;
            const auto* message = static_cast<Simulator::EnterEditorMessage*>(value);
            CoopNet::SubmitEditorOpen(message->mEditorID, SerializeCreation(message->mCreationName));
            WriteProbeLog("Relayed local editor entry to the cooperative peer.");
            return false;
        }
    };

    void UpdateSharedEditor(const CoopNet::Snapshot& snapshot, ULONG64 now)
    {
        if (!snapshot.inviteAccepted)
        {
            gWasEditorMode = false;
            gObservedEditorModel = nullptr;
            gEditorHistoryPending = false;
            gEditorInitialModelPending = false;
            gMirroredEditorRequestID = 0;
            gMirroredEditorRequestTick = 0;
            return;
        }
        auto editor = Editors::GetEditor();
        const bool editorActive = Simulator::IsEditorMode() && editor && editor->IsActive();
        auto model = editorActive ? editor->GetEditorModel() : nullptr;
        if (model != gObservedEditorModel)
        {
            gObservedEditorModel = model;
            gEditorModelReadySince = now;
        }
        const bool editorReady = model && model->mbAllBlocksLoaded && !model->mRigblocks.empty() &&
            now - gEditorModelReadySince >= 750;
        static ULONG64 lastEditorTrace = 0;
        if (editorActive && now-lastEditorTrace >= 2000)
        {
            char line[384]{};
            sprintf_s(line,"EditorSync: ready=%d blocks=%u loaded=%d moving=%d handle=%d mouse=%u history=%d/%u initial=%d open=%d seq=%llu applied=%llu pending=%llu ack=%llu",
                editorReady,model ? unsigned(model->mRigblocks.size()) : 0,
                model ? model->mbAllBlocksLoaded : 0,editor->mpMovingPart ? 1 : 0,
                editor->mpActiveHandle ? 1 : 0,editor->mMouseState.value,
                editor->mEditHistoryIndex,unsigned(editor->mEditHistory.size()),gEditorInitialModelPending,
                snapshot.editorOpen,snapshot.speciesSequence,gAppliedSpeciesSequence,
                gPendingSpeciesSequence,snapshot.speciesAck);
            WriteProbeLog(line);
            lastEditorTrace = now;
        }

        if (editorActive || !snapshot.editorOpen)
        {
            gMirroredEditorRequestID = 0;
            gMirroredEditorRequestTick = 0;
        }

        if (snapshot.editorOpen && snapshot.editorRole != CoopNet::GetRole() &&
            !editorActive && Simulator::IsCellGame() && snapshot.editorID != 0)
        {
            // Entering the editor takes several frames.  Re-sending the engine
            // message every frame restarts the transition and can prevent it
            // from ever completing.
            if (gMirroredEditorRequestID != snapshot.editorID ||
                now - gMirroredEditorRequestTick >= 5000)
            {
                gMirroredEditorRequestID = snapshot.editorID;
                gMirroredEditorRequestTick = now;
                OpenMirroredEditor(snapshot);
            }
            return;
        }

        // IsActive becomes true before the body has finished loading.
        if (editorActive && !editorReady) return;
        if (editorActive && !gWasEditorMode && snapshot.editorOpen &&
            snapshot.editorRole != CoopNet::GetRole())
            gEditorInitialModelPending = true;
        if (editorReady && gEditorHistoryPending)
        {
            editor->CommitEditHistory(true);
            gEditorHistoryPending = false;
        }
        // A cache key handed to the campaign entry is not proof that the
        // editor loaded it. Cell can fall back to a default green body. Apply
        // the authoritative resource explicitly before this peer can publish.
        if (editorActive && gEditorInitialModelPending)
        {
            if (snapshot.speciesBlob.empty() || !ApplyEditorModel(snapshot.speciesBlob)) return;
            gAppliedSpeciesSequence = snapshot.speciesSequence;
            gEditorEntrySpeciesSequence = snapshot.speciesSequence;
            gLastLocalSpecies = snapshot.speciesBlob;
            gPendingSpeciesSequence = 0; gPendingSpecies.clear();
            gEditorInitialModelPending = false;
            gWasEditorMode = true;
            WriteProbeLog("EditorSync: authoritative entry body installed; waiting for native body load.");
            return;
        }
        if (editorActive && !gWasEditorMode)
        {
            gLastLocalSpecies=SerializeEditorModel();
            gPendingSpeciesSequence=0; gPendingSpecies.clear();
            if (snapshot.editorOpen)
                gAppliedSpeciesSequence = snapshot.editorRole == CoopNet::GetRole()
                    ? snapshot.speciesSequence : gEditorEntrySpeciesSequence;
            if (!snapshot.editorOpen)
            {
                // Until the server acknowledges this entry, its closed-editor
                // species belongs to the previous visit. Do not reload that
                // old body/paint while the new editor is opening.
                gAppliedSpeciesSequence = snapshot.speciesSequence;
                CoopNet::SubmitEditorOpen(editor->mEditorName,gLastLocalSpecies);
            }
        }

        // A final model must still arrive while the other window is finishing
        // its native save/exit transition. Do not stop receiving it merely
        // because the server has already released its editor flag.
        if (editorActive && !snapshot.editorOpen && gWasEditorMode &&
            snapshot.speciesSequence > gAppliedSpeciesSequence && !snapshot.speciesBlob.empty() &&
            !editor->mpMovingPart && !editor->mMouseState.IsLeftButtonDown)
        {
            if (ApplyEditorModel(snapshot.speciesBlob))
            {
                gLastLocalSpecies=snapshot.speciesBlob;
                gAppliedSpeciesSequence=snapshot.speciesSequence;
                gPendingSpeciesSequence=0; gPendingSpecies.clear();
            }
        }

        if (editorActive && snapshot.editorOpen && now-gLastSpeciesTick>=100)
        {
            auto current=SerializeEditorModel();
            if (gPendingSpeciesSequence && snapshot.speciesAck>=gPendingSpeciesSequence)
            {
                if (!snapshot.speciesConflict) gLastLocalSpecies=gPendingSpecies;
                gPendingSpeciesSequence=0; gPendingSpecies.clear();
            }
            if (!gPendingSpeciesSequence && !current.empty())
            {
                if (snapshot.speciesSequence>gAppliedSpeciesSequence && !snapshot.speciesBlob.empty())
                {
                    std::vector<unsigned char> base,local,remote,merged;
                    Base64Decode(gLastLocalSpecies,base); Base64Decode(current,local);
                    Base64Decode(snapshot.speciesBlob,remote);
                    if (CoopEditor::Merge(base,local,remote,merged))
                    {
                        const auto value=Base64Encode(merged);
                        if (value==current || ApplyEditorModel(value))
                        {
                            current=value; gLastLocalSpecies=snapshot.speciesBlob;
                            gAppliedSpeciesSequence=snapshot.speciesSequence;
                        }
                    }
                    else WriteProbeLog("Editor topology conflict: preserving local model; finish the current edit before retrying.");
                }
                if (gAppliedSpeciesSequence==snapshot.speciesSequence && current!=gLastLocalSpecies)
                {
                    gPendingSpecies=current;
                    gPendingSpeciesSequence=CoopNet::SubmitSpecies(current,gAppliedSpeciesSequence);
                    char line[160]{};
                    sprintf_s(line,"EditorSync: sent committed model client=%llu base=%llu bytes=%u",
                        gPendingSpeciesSequence,gAppliedSpeciesSequence,unsigned(current.size()));
                    WriteProbeLog(line);
                }
            }
            gLastSpeciesTick=now;
        }

        if (!editorActive && gWasEditorMode)
        {
            auto game=Simulator::Cell::cCellGame::Get();
            auto data=game ? game->mpSerializableData.get() : nullptr;
            auto saved=data ? SerializeCreation(data->mPlayerCreatureKey) : std::string{};
            CoopNet::SubmitEditorClose(saved.empty() ? (gPendingSpecies.empty() ? gLastLocalSpecies : gPendingSpecies) : saved);
            gPendingSpeciesSequence=0; gPendingSpecies.clear();
            gJoinedPlayerPlaced=false;
            gWorldCoordinates = CoopWorld::Coordinates{};
            gEditorEntryResource = nullptr;
            gLastLocalSpecies.clear();
            gAppliedSpeciesSequence = 0;
            gLastSubmittedAppearanceKey = ResourceKey{};
            gLastSubmittedAppearanceCellResource = 0;
            gLocalAppearanceStableSince = now;
        }
        gWasEditorMode = editorActive;
    }

    constexpr uint32_t kInviteButtonID = 0x5C0F1001;
    constexpr uint32_t kAcceptInviteButtonID = 0x5C0F1002;
    constexpr uint32_t kDeclineInviteButtonID = 0x5C0F1003;
    constexpr uint32_t kInvitePickerTitleID = 0x5C0F1004;
    constexpr uint32_t kInvitePlayerButtonID = 0x5C0F1005;
    constexpr uint32_t kInvitePickerCancelButtonID = 0x5C0F1006;
    constexpr uint32_t kSessionEndedOkID = 0x5C0F1013;

    const char16_t* CoopText(const char16_t* english, const char16_t* russian)
    {
        return gUiUsesRussian ? russian : english;
    }

    UTFWin::IWindow* FindNativeMenuActionStyle();

    class InviteWinProc : public UTFWin::DefaultWinProc<>
    {
    public:
        bool HandleUIMessage(UTFWin::IWindow* window,
            const UTFWin::Message& message) override
        {
            if (!window || !message.IsType(UTFWin::kMsgButtonClick)) return false;
            switch (window->GetControlID())
            {
            case kSessionEndedOkID:
                CoopNet::AcknowledgeSessionEnd();
                return true;
            case kInviteButtonID:
                // Do not send immediately.  Even the local two-window test has
                // a real recipient picker, which makes the later VPN flow match
                // the same interaction.
                gInvitePickerOpen = true;
                WriteProbeLog("Co-op player picker opened.");
                return true;
            case kInvitePlayerButtonID:
                if (gInviteRequestSent) return true;
                CoopNet::SubmitInvite();
                gInviteRequestSent = true;
                gInvitePickerOpen = false;
                gReturnAfterInvite = true;
                WriteProbeLog("Co-op invitation sent to the selected peer.");
                return true;
            case kInvitePickerCancelButtonID:
                gInvitePickerOpen = false;
                WriteProbeLog("Co-op player picker closed.");
                return true;
            case kAcceptInviteButtonID:
                if (gInviteResponseSent) return true;
                {
                    const auto invite = CoopNet::GetSnapshot();
                    if (!ValidateIncomingSavedWorld(invite.inviteFrom.c_str()))
                    {
                        gJoinFailed = true;
                        CoopNet::SubmitInviteResponse(false);
                        gInviteResponseSent = true;
                        return true;
                    }
                }
                gJoinFailed = false;
                CoopNet::SubmitInviteResponse(true);
                gInviteResponseSent = true;
                WriteProbeLog("Guest accepted the cooperative invitation.");
                return true;
            case kDeclineInviteButtonID:
                if (gInviteResponseSent) return true;
                CoopNet::SubmitInviteResponse(false);
                gInviteResponseSent = true;
                WriteProbeLog("Guest declined the cooperative invitation.");
                return true;
            default:
                return false;
            }
        }
    };

    IWindowPtr CreateCoopButton(uint32_t controlID, const char16_t* caption,
        CoopUi::Style style = CoopUi::Style::Primary)
    {
        auto root = WindowManager.GetMainWindow();
        if (!root) return nullptr;
        auto window = static_cast<UTFWin::IWindow*>(ClassManager.Create(UTFWin::IButton::WinButton_ID));
        if (!window) return nullptr;
        window->SetControlID(controlID);
        window->SetCommandID(controlID);
        window->SetCaption(caption);
        auto nativeStyle = FindNativeMenuActionStyle();
        window->SetTextFontID(nativeStyle ? nativeStyle->GetTextFontID() : 0x00AEBB69);
        window->SetFillColor(Math::Color(0xFFFFFFFF));
        window->SetDrawable(new CoopUi::Drawable(style));
        // Avoid transparent caption colours in any of the native button states.
        if (auto button = object_cast<UTFWin::IButton>(window))
        {
            for (int state = 0; state < 8; ++state)
            {
                const bool disabled = state == 1 || state == 5;
                const uint32_t color = style == CoopUi::Style::Label ? 0xFFF0F5F8 :
                    disabled ? 0xFFA6B6BF : style == CoopUi::Style::Primary ? 0xFF273442 : 0xFFF0F5F8;
                button->SetCaptionColor(UTFWin::StateIndices(state), Math::Color(color));
            }
        }
        window->SetFlag(UTFWin::kWinFlagVisible, true);
        window->SetFlag(UTFWin::kWinFlagEnabled, style != CoopUi::Style::Label);
        window->SetFlag(UTFWin::kWinFlagAlwaysInFront, true);
        window->SetFlag(UTFWin::kWinFlagIgnoreMouse, style == CoopUi::Style::Label);
        if (style != CoopUi::Style::Label) window->AddWinProc(gInviteWinProc.get());
        root->AddWindow(window);
        root->BringToFront(window);
        return IWindowPtr(window);
    }

    IWindowPtr CreateCoopPanel(uint32_t id)
    {
        auto root = WindowManager.GetMainWindow();
        if (!root) return nullptr;
        auto panel = new UTFWin::Window();
        panel->SetControlID(id);
        panel->SetDrawable(new CoopUi::Drawable(CoopUi::Style::Panel));
        panel->SetFillColor(Math::Color(0xFFFFFFFF));
        panel->SetFlag(UTFWin::kWinFlagAlwaysInFront, true);
        panel->SetFlag(UTFWin::kWinFlagIgnoreMouse, true);
        root->AddWindow(panel);
        return IWindowPtr(panel);
    }

    void PositionCoopButton(UTFWin::IWindow* window, float yOffset)
    {
        if (!window) return;
        auto mainWindow = WindowManager.GetMainWindow();
        if (!mainWindow) return;
        const auto& area = mainWindow->GetArea();
        const float width = 260.0f;
        const float height = 42.0f;
        const float left = (area.GetWidth() - width) * 0.5f;
        const float top = area.GetHeight() * 0.5f + yOffset;
        window->SetArea({ left, top, left + width, top + height });
    }

    void SetButtonVisible(UTFWin::IWindow* window, bool visible)
    {
        if (!window) return;
        window->SetFlag(UTFWin::kWinFlagVisible, visible);
        // A hidden co-op control must never capture gameplay mouse input.
        window->SetFlag(UTFWin::kWinFlagIgnoreMouse, !visible ||
            window == gInvitePickerTitle.get() || window == gInvitePromptTitle.get() ||
            window == gInvitePromptDetail.get() || window == gSessionStatus.get() ||
            window == gInvitePickerBackdrop.get() || window == gInvitePromptBackdrop.get());
    }

    void SetButtonEnabled(UTFWin::IWindow* window, bool enabled)
    {
        if (window) window->SetFlag(UTFWin::kWinFlagEnabled, enabled);
    }

    struct PausePanelCandidate
    {
        UTFWin::IWindow* window = nullptr;
        int score = -1;
        int visibleButtons = 0;
        int disabledButtons = 0;
    };

    bool CaptionEquals(const char16_t* left, const char16_t* right)
    {
        if (!left || !right) return false;
        while (*left && *right)
        {
            if (*left++ != *right++) return false;
        }
        return *left == 0 && *right == 0;
    }

    bool IsReturnToGameCaption(const char16_t* caption)
    {
        // The game uses a plain UTFWin::Window for these menu actions, not an
        // IButton.  Match the stable first action from the Russian and English
        // pause menus, then use its panel-sized ancestor as the anchor.
        return CaptionEquals(caption, u"\u0412\u0435\u0440\u043d\u0443\u0442\u044c\u0441\u044f \u0432 \u0438\u0433\u0440\u0443") ||
            CaptionEquals(caption, u"Return to Game");
    }

    bool IsRussianReturnToGameCaption(const char16_t* caption)
    {
        return CaptionEquals(caption,
            u"\u0412\u0435\u0440\u043d\u0443\u0442\u044c\u0441\u044f \u0432 \u0438\u0433\u0440\u0443");
    }

    bool IsRussianUiCaption(const char16_t* caption)
    {
        return IsRussianReturnToGameCaption(caption) ||
            CaptionEquals(caption, u"\u041d\u0430\u0447\u0430\u0442\u044c \u0438\u0433\u0440\u0443") ||
            CaptionEquals(caption, u"\u041a\u043e\u0441\u043c\u0438\u0447\u0435\u0441\u043a\u0438\u0435 \u043f\u0440\u0438\u043a\u043b\u044e\u0447\u0435\u043d\u0438\u044f");
    }

    bool DetectRussianGameUi(UTFWin::IWindow* window, int depth = 0)
    {
        if (!window || depth > 12 || !window->IsVisible()) return false;
        if (IsRussianUiCaption(window->GetCaption())) return true;
        for (auto child : window->children())
            if (DetectRussianGameUi(child, depth + 1)) return true;
        return false;
    }

    bool IsNativeMenuActionCaption(const char16_t* caption)
    {
        return IsReturnToGameCaption(caption) ||
            CaptionEquals(caption, u"\u041d\u0430\u0447\u0430\u0442\u044c \u0438\u0433\u0440\u0443") ||
            CaptionEquals(caption, u"Start Game");
    }

    UTFWin::IWindow* FindNativeMenuActionStyleRecursive(UTFWin::IWindow* window,
        int depth)
    {
        if (!window || depth > 14 || !window->IsVisible()) return nullptr;
        if (IsNativeMenuActionCaption(window->GetCaption()))
        {
            for (auto candidate = window; candidate; candidate = candidate->GetParent())
            {
                if (candidate->GetDrawable()) return candidate;
            }
        }
        for (auto child : window->children())
            if (auto style = FindNativeMenuActionStyleRecursive(child, depth + 1)) return style;
        return nullptr;
    }

    UTFWin::IWindow* FindNativeMenuActionStyle()
    {
        return FindNativeMenuActionStyleRecursive(WindowManager.GetMainWindow(), 0);
    }

    UTFWin::IWindow* FindPausePanelByReturnAction(UTFWin::IWindow* window,
        UTFWin::IWindow* root, float rootWidth, float rootHeight, int depth)
    {
        if (!window || depth > 16 || !window->IsVisible()) return nullptr;
        if (IsReturnToGameCaption(window->GetCaption()))
        {
            for (auto candidate = window->GetParent(); candidate && candidate != root;
                candidate = candidate->GetParent())
            {
                const auto& area = candidate->GetArea();
                const float width = area.GetWidth();
                const float height = area.GetHeight();
                if (width < 220.0f || height < 240.0f ||
                    width > rootWidth * 0.80f || height > rootHeight * 0.95f)
                    continue;
                const auto origin = candidate->ToGlobalCoordinates(Math::Point(0.0f, 0.0f));
                const float centerX = origin.x + width * 0.5f;
                const float centerY = origin.y + height * 0.5f;
                if (std::abs(centerX - rootWidth * 0.5f) <= rootWidth * 0.30f &&
                    std::abs(centerY - rootHeight * 0.5f) <= rootHeight * 0.30f)
                    return candidate;
            }
        }
        for (auto child : window->children())
        {
            if (auto panel = FindPausePanelByReturnAction(child, root, rootWidth,
                rootHeight, depth + 1)) return panel;
        }
        return nullptr;
    }

    void CountVisibleButtons(UTFWin::IWindow* window, int depth,
        int& visibleButtons, int& disabledButtons)
    {
        if (!window || depth > 16 || !window->IsVisible()) return;
        const char* component = window->GetComponentName();
        if (component && std::strstr(component, "Button"))
        {
            ++visibleButtons;
            if (!window->IsEnabled()) ++disabledButtons;
        }
        for (auto child : window->children())
            CountVisibleButtons(child, depth + 1, visibleButtons, disabledButtons);
    }

    void FindVisiblePausePanelRecursive(UTFWin::IWindow* window,
        UTFWin::IWindow* root, float rootWidth, float rootHeight, int depth,
        PausePanelCandidate& best)
    {
        if (!window || depth > 12 || !window->IsVisible()) return;
        if (window != root)
        {
            const auto& area = window->GetArea();
            const float width = area.GetWidth();
            const float height = area.GetHeight();
            if (width >= 220.0f && height >= 240.0f &&
                width <= rootWidth * 0.80f && height <= rootHeight * 0.95f)
            {
                const auto origin = window->ToGlobalCoordinates(Math::Point(0.0f, 0.0f));
                const float centerX = origin.x + width * 0.5f;
                const float centerY = origin.y + height * 0.5f;
                if (std::abs(centerX - rootWidth * 0.5f) <= rootWidth * 0.30f &&
                    std::abs(centerY - rootHeight * 0.5f) <= rootHeight * 0.30f)
                {
                    int visibleButtons = 0;
                    int disabledButtons = 0;
                    CountVisibleButtons(window, 0, visibleButtons, disabledButtons);
                    if (visibleButtons >= 5 && disabledButtons >= 1)
                    {
                        const int score = visibleButtons * 100 + disabledButtons * 25 -
                            static_cast<int>((width * height) / 10000.0f);
                        if (score > best.score)
                        {
                            best.window = window;
                            best.score = score;
                            best.visibleButtons = visibleButtons;
                            best.disabledButtons = disabledButtons;
                        }
                    }
                }
            }
        }
        for (auto child : window->children())
            FindVisiblePausePanelRecursive(child, root, rootWidth, rootHeight,
                depth + 1, best);
    }

    UTFWin::IWindow* FindVisiblePausePanel()
    {
        auto root = WindowManager.GetMainWindow();
        if (!root) return nullptr;
        const auto& area = root->GetArea();
        if (auto panel = FindPausePanelByReturnAction(root, root, area.GetWidth(),
            area.GetHeight(), 0)) return panel;
        PausePanelCandidate best;
        FindVisiblePausePanelRecursive(root, root, area.GetWidth(), area.GetHeight(),
            0, best);
        return best.window;
    }

    void PositionInviteButton(UTFWin::IWindow* button, UTFWin::IWindow* panel)
    {
        if (!button || !panel) return;
        auto root = WindowManager.GetMainWindow();
        if (!root) return;
        const auto& rootArea = root->GetArea();
        const auto& panelArea = panel->GetArea();
        const auto origin = panel->ToGlobalCoordinates(Math::Point(0.0f, 0.0f));
        const float width = 260.0f;
        const float height = 42.0f;
        float left = origin.x + (panelArea.GetWidth() - width) * 0.5f;
        float top = origin.y + panelArea.GetHeight() + 8.0f;
        if (top + height > rootArea.GetHeight() - 8.0f)
            top = origin.y - height - 8.0f;
        left = std::max(8.0f,
            std::min(left, rootArea.GetWidth() - width - 8.0f));
        top = std::max(8.0f,
            std::min(top, rootArea.GetHeight() - height - 8.0f));
        button->SetArea({ left, top, left + width, top + height });
    }

    void PositionInvitePicker()
    {
        auto root = WindowManager.GetMainWindow();
        if (!root) return;
        const auto& area = root->GetArea();
        const float width = std::min(360.0f, area.GetWidth() - 24.0f);
        const float left = (area.GetWidth() - width) * 0.5f;
        const float top = (area.GetHeight() - 190.0f) * 0.5f;
        if (gInvitePickerBackdrop) gInvitePickerBackdrop->SetArea({left,top,left+width,top+190});
        if (gInvitePickerTitle) gInvitePickerTitle->SetArea({left+20,top+14,left+width-20,top+48});
        if (gInvitePlayerButton) gInvitePlayerButton->SetArea({left+20,top+65,left+width-20,top+110});
        if (gInvitePickerCancelButton) gInvitePickerCancelButton->SetArea({left+90,top+128,left+width-90,top+168});
    }

    void PositionInvitePrompt()
    {
        auto root = WindowManager.GetMainWindow();
        if (!root) return;
        const auto& area = root->GetArea();
        const float width = std::min(380.0f, area.GetWidth() - 24.0f);
        const float left = (area.GetWidth() - width) * 0.5f;
        const float top = (area.GetHeight() - 194.0f) * 0.5f;
        if (gInvitePromptBackdrop) gInvitePromptBackdrop->SetArea({left,top,left+width,top+194});
        if (gInvitePromptTitle) gInvitePromptTitle->SetArea({left+16,top+17,left+width-16,top+51});
        if (gInvitePromptDetail) gInvitePromptDetail->SetArea({left+16,top+57,left+width-16,top+88});
        if (gDeclineInviteButton) gDeclineInviteButton->SetArea({left+20,top+124,left+145,top+169});
        if (gAcceptInviteButton) gAcceptInviteButton->SetArea({left+157,top+124,left+width-20,top+169});
    }

    void HideCoopUI()
    {
        for (auto window : {gInviteButton.get(), gInvitePickerBackdrop.get(),
            gInvitePickerTitle.get(), gInvitePlayerButton.get(), gInvitePickerCancelButton.get(),
            gInvitePromptBackdrop.get(), gInvitePromptTitle.get(), gInvitePromptDetail.get(),
            gAcceptInviteButton.get(), gDeclineInviteButton.get(), gSessionStatus.get()})
            SetButtonVisible(window, false);
        gInvitePickerOpen = false;
    }

    UTFWin::IWindow* FindReturnAction(UTFWin::IWindow* window, int depth = 0)
    {
        if (!window || depth > 14 || !window->IsVisible()) return nullptr;
        if (IsReturnToGameCaption(window->GetCaption())) return window;
        for (auto child : window->children())
            if (auto action = FindReturnAction(child, depth + 1)) return action;
        return nullptr;
    }

    void ReturnToGameAfterInvite()
    {
        gReturnAfterInvite = false;
        if (!Simulator::IsCellGame()) return;
        auto action = FindReturnAction(WindowManager.GetMainWindow());
        if (!action) return;
        // Use the game's own Return-to-Game action, which releases its menu
        // pause and input state together. Never clear arbitrary pause counters.
        IWindowPtr retained = action;
        UTFWin::Message click{};
        click.eventType = UTFWin::kMsgButtonClick;
        click.source = action;
        action->SendMsg(click);
    }

    bool IsCoopUiControl(const UTFWin::IWindow* window)
    {
        if (!window) return false;
        const auto id = window->GetControlID();
        return id >= 0x5C0F1001 && id <= 0x5C0F100B;
    }

    void MixUiHash(uint64_t& hash, const void* data, size_t size)
    {
        const auto bytes = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i)
        {
            hash ^= bytes[i];
            hash *= 1099511628211ULL;
        }
    }

    void HashUiTree(UTFWin::IWindow* window, int depth, uint64_t& hash)
    {
        if (!window || depth > 12 || IsCoopUiControl(window)) return;
        const auto controlID = window->GetControlID();
        const auto commandID = window->GetCommandID();
        const char* component = window->GetComponentName();
        const auto flags = window->GetFlags();
        const auto& area = window->GetArea();
        MixUiHash(hash, &depth, sizeof(depth));
        MixUiHash(hash, &controlID, sizeof(controlID));
        MixUiHash(hash, &commandID, sizeof(commandID));
        MixUiHash(hash, &flags, sizeof(flags));
        MixUiHash(hash, &area, sizeof(area));
        if (component) MixUiHash(hash, component, std::strlen(component));
        int childCount = 0;
        for (auto child : window->children())
            if (!IsCoopUiControl(child)) ++childCount;
        MixUiHash(hash, &childCount, sizeof(childCount));
        for (auto child : window->children())
            HashUiTree(child, depth + 1, hash);
    }

    std::string Utf16ToUtf8(const char16_t* text)
    {
        if (!text || !text[0]) return {};
        const auto wide = reinterpret_cast<const wchar_t*>(text);
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide, -1,
            nullptr, 0, nullptr, nullptr);
        if (size <= 1) return {};
        std::vector<char> buffer(static_cast<size_t>(size));
        if (!WideCharToMultiByte(CP_UTF8, 0, wide, -1, buffer.data(), size,
            nullptr, nullptr)) return {};
        return std::string(buffer.data());
    }

    std::string JsonEscape(std::string value)
    {
        std::string result;
        result.reserve(value.size());
        for (unsigned char ch : value)
        {
            if (ch == '\\' || ch == '"')
            {
                result.push_back('\\');
                result.push_back(static_cast<char>(ch));
            }
            else if (ch >= 0x20)
                result.push_back(static_cast<char>(ch));
        }
        return result;
    }

    void AppendUiTreeLog(const std::string& line)
    {
        char tempPath[MAX_PATH]{};
        char logPath[MAX_PATH]{};
        if (!GetTempPathA(MAX_PATH, tempPath)) return;
        if (sprintf_s(logPath, "%sSporeCoop.UI.ndjson", tempPath) <= 0) return;
        HANDLE file = CreateFileA(logPath, FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        DWORD written = 0;
        WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        static const char newline[] = "\r\n";
        WriteFile(file, newline, 2, &written, nullptr);
        CloseHandle(file);
    }

    void DumpUiTree(UTFWin::IWindow* window, int depth, uint32_t snapshot)
    {
        if (!window || depth > 12 || IsCoopUiControl(window)) return;
        const auto& area = window->GetArea();
        const char* rawComponent = window->GetComponentName();
        auto component = JsonEscape(rawComponent ? rawComponent : "");
        auto caption = JsonEscape(Utf16ToUtf8(window->GetCaption()));
        if (caption.size() > 256) caption.resize(256);
        char prefix[768]{};
        sprintf_s(prefix,
            "{\"kind\":\"node\",\"snapshot\":%u,\"depth\":%d,\"controlID\":%u,\"commandID\":%u,\"visible\":%d,\"enabled\":%d,\"area\":[%.1f,%.1f,%.1f,%.1f],\"component\":\"%s\",\"caption\":\"",
            snapshot, depth, window->GetControlID(), window->GetCommandID(),
            window->IsVisible() ? 1 : 0, window->IsEnabled() ? 1 : 0,
            area.left, area.top, area.right, area.bottom, component.c_str());
        AppendUiTreeLog(std::string(prefix) + caption + "\"}");
        for (auto child : window->children())
            DumpUiTree(child, depth + 1, snapshot);
    }

    void TraceUiTreeChanges()
    {
        static const bool enabled = GetEnvironmentVariableA("SPORE_COOP_TRACE_UI", nullptr, 0) > 0;
        if (!enabled) return;
        auto root = WindowManager.GetMainWindow();
        if (!root) return;
        uint64_t fingerprint = 1469598103934665603ULL;
        HashUiTree(root, 0, fingerprint);
        static uint64_t previousFingerprint = 0;
        static uint32_t snapshot = 0;
        if (fingerprint == previousFingerprint) return;
        previousFingerprint = fingerprint;
        if (snapshot >= 32) return;
        ++snapshot;
        char header[256]{};
        sprintf_s(header,
            "{\"kind\":\"snapshot\",\"snapshot\":%u,\"fingerprint\":\"%016llX\",\"tick\":%llu}",
            snapshot, static_cast<unsigned long long>(fingerprint),
            static_cast<unsigned long long>(GetTickCount64()));
        AppendUiTreeLog(header);
        DumpUiTree(root, 0, snapshot);
    }

    void UpdateInviteUI(const CoopNet::Snapshot& snapshot)
    {
        const bool isHost = std::strcmp(CoopNet::GetRole(), "host") == 0;
        const bool isInviter = snapshot.inviteFrom == CoopNet::GetRole();
        const bool stageGame = Simulator::IsStageGameMode();
        const bool editorMode = Simulator::IsEditorMode();
        auto pausePanel = stageGame && !editorMode
            ? FindVisiblePausePanel() : nullptr;
        const bool pauseMenuOpen = pausePanel != nullptr;
        if (auto root = WindowManager.GetMainWindow())
            gUiUsesRussian = DetectRussianGameUi(root);
        // This control belongs to the Esc panel.  It is deliberately hidden
        // while playing so a mouse click cannot send an accidental invitation.
        const bool canOpenPicker = pauseMenuOpen && !editorMode &&
            !snapshot.invitePending && !snapshot.inviteAccepted;
        if (!canOpenPicker) gInvitePickerOpen = false;
        const bool showHostInvite = canOpenPicker && !gInvitePickerOpen;
        if (stageGame)
            TraceUiTreeChanges();
        if (showHostInvite && !gInviteButton)
            gInviteButton = CreateCoopButton(kInviteButtonID,
                CoopText(u"Invite friend", u"\u041f\u0440\u0438\u0433\u043b\u0430\u0441\u0438\u0442\u044c \u0434\u0440\u0443\u0433\u0430"));
        if (pausePanel) PositionInviteButton(gInviteButton.get(), pausePanel);
        SetButtonVisible(gInviteButton.get(), showHostInvite);
        if (gInviteButton)
        {
            if (snapshot.inviteAccepted && isInviter)
                gInviteButton->SetCaption(CoopText(u"Friend is joining your world",
                    u"\u0414\u0440\u0443\u0433 \u043f\u0440\u0438\u0441\u043e\u0435\u0434\u0438\u043d\u044f\u0435\u0442\u0441\u044f \u043a \u0432\u0430\u0448\u0435\u043c\u0443 \u043c\u0438\u0440\u0443"));
            else if (snapshot.inviteAccepted)
                gInviteButton->SetCaption(CoopText(u"Joining friend's world",
                    u"\u041f\u043e\u0434\u043a\u043b\u044e\u0447\u0435\u043d\u0438\u0435 \u043a \u043c\u0438\u0440\u0443 \u0434\u0440\u0443\u0433\u0430"));
            else if (snapshot.invitePending && isInviter)
                gInviteButton->SetCaption(CoopText(u"Invitation sent",
                    u"\u041f\u0440\u0438\u0433\u043b\u0430\u0448\u0435\u043d\u0438\u0435 \u043e\u0442\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u043e"));
            else if (snapshot.invitePending || gInviteRequestSent)
                gInviteButton->SetCaption(CoopText(u"Invitation sent",
                    u"\u041f\u0440\u0438\u0433\u043b\u0430\u0448\u0435\u043d\u0438\u0435 \u043e\u0442\u043f\u0440\u0430\u0432\u043b\u0435\u043d\u043e"));
            else if (!snapshot.remotePeerConnected)
                gInviteButton->SetCaption(CoopText(u"Waiting for friend to connect",
                    u"\u041e\u0436\u0438\u0434\u0430\u043d\u0438\u0435 \u0434\u0440\u0443\u0433\u0430"));
            else
                gInviteButton->SetCaption(CoopText(u"Invite friend",
                    u"\u041f\u0440\u0438\u0433\u043b\u0430\u0441\u0438\u0442\u044c \u0434\u0440\u0443\u0433\u0430"));
        }
        SetButtonEnabled(gInviteButton.get(), snapshot.remotePeerConnected && canOpenPicker);
        auto mainWindow = WindowManager.GetMainWindow();
        if (showHostInvite && mainWindow && gInviteButton)
            mainWindow->BringToFront(gInviteButton.get());

        // The two local profiles are deliberately named as separate people in
        // the picker.  The protocol currently permits exactly two peers, so the
        // list contains the one connected recipient; it is ready to be replaced
        // with a VPN peer list without changing the invitation flow.
        const bool showPicker = pauseMenuOpen && gInvitePickerOpen &&
            !snapshot.invitePending && !snapshot.inviteAccepted;
        if (showPicker && !gInvitePickerBackdrop && mainWindow)
        {
            gInvitePickerBackdrop = CreateCoopPanel(0x5C0F1007);
            gInvitePickerTitle = CreateCoopButton(kInvitePickerTitleID,
                CoopText(u"Invite a friend", u"\u041f\u0440\u0438\u0433\u043b\u0430\u0441\u0438\u0442\u044c \u0434\u0440\u0443\u0433\u0430"), CoopUi::Style::Label);
            gInvitePlayerButton = CreateCoopButton(kInvitePlayerButtonID,
                isHost ? CoopText(u"Invite Player 2", u"\u041f\u0440\u0438\u0433\u043b\u0430\u0441\u0438\u0442\u044c \u0438\u0433\u0440\u043e\u043a\u0430 2") :
                CoopText(u"Invite Player 1", u"\u041f\u0440\u0438\u0433\u043b\u0430\u0441\u0438\u0442\u044c \u0438\u0433\u0440\u043e\u043a\u0430 1"));
            gInvitePickerCancelButton = CreateCoopButton(kInvitePickerCancelButtonID,
                CoopText(u"Back", u"\u041d\u0430\u0437\u0430\u0434"), CoopUi::Style::Secondary);
        }

        PositionInvitePicker();
        SetButtonVisible(gInvitePickerBackdrop.get(), showPicker);
        SetButtonVisible(gInvitePickerTitle.get(), showPicker);
        SetButtonVisible(gInvitePlayerButton.get(), showPicker);
        SetButtonVisible(gInvitePickerCancelButton.get(), showPicker);
        SetButtonEnabled(gInvitePlayerButton.get(), snapshot.remotePeerConnected && showPicker);
        if (showPicker && mainWindow)
        {
            if (gInvitePickerBackdrop) mainWindow->BringToFront(gInvitePickerBackdrop.get());
            if (gInvitePickerTitle) mainWindow->BringToFront(gInvitePickerTitle.get());
            if (gInvitePlayerButton) mainWindow->BringToFront(gInvitePlayerButton.get());
            if (gInvitePickerCancelButton) mainWindow->BringToFront(gInvitePickerCancelButton.get());
        }

        const bool showGuestPrompt = snapshot.invitePending && !isInviter &&
            !gInviteResponseSent;
        if (showGuestPrompt && !gInvitePromptBackdrop)
        {
            gInvitePromptBackdrop = CreateCoopPanel(0x5C0F1008);
            gInvitePromptTitle = CreateCoopButton(0x5C0F1009,
                CoopText(u"Play together", u"\u0421\u044b\u0433\u0440\u0430\u0435\u043c \u0432\u043c\u0435\u0441\u0442\u0435?"), CoopUi::Style::Label);
            gInvitePromptDetail = CreateCoopButton(0x5C0F100A,
                CoopText(u"A friend invites you to their world", u"\u0414\u0440\u0443\u0433 \u043f\u0440\u0438\u0433\u043b\u0430\u0448\u0430\u0435\u0442 \u0432\u0430\u0441 \u0432 \u0441\u0432\u043e\u0439 \u043c\u0438\u0440"), CoopUi::Style::Label);
            gAcceptInviteButton = CreateCoopButton(kAcceptInviteButtonID,
                CoopText(u"Join", u"\u041f\u0440\u0438\u0441\u043e\u0435\u0434\u0438\u043d\u0438\u0442\u044c\u0441\u044f"));
            gDeclineInviteButton = CreateCoopButton(kDeclineInviteButtonID,
                CoopText(u"Not now", u"\u041d\u0435 \u0441\u0435\u0439\u0447\u0430\u0441"), CoopUi::Style::Secondary);
        }
        PositionInvitePrompt();
        for (auto window : {gInvitePromptBackdrop.get(), gInvitePromptTitle.get(),
            gInvitePromptDetail.get(), gDeclineInviteButton.get(), gAcceptInviteButton.get()})
        {
            SetButtonVisible(window, showGuestPrompt);
            if (showGuestPrompt && mainWindow && window) mainWindow->BringToFront(window);
        }
        const bool showStatus = gJoinFailed || (snapshot.inviteAccepted &&
            (gRemoteRenderDelayed || (snapshot.hostPaused && !isInviter && stageGame)));
        if (showStatus && !gSessionStatus)
            gSessionStatus = CreateCoopButton(0x5C0F100B,
                CoopText(u"World owner paused the game", u"\u0425\u043e\u0437\u044f\u0438\u043d \u043c\u0438\u0440\u0430 \u043f\u043e\u0441\u0442\u0430\u0432\u0438\u043b \u0438\u0433\u0440\u0443 \u043d\u0430 \u043f\u0430\u0443\u0437\u0443"), CoopUi::Style::Label);
        if (gSessionStatus)
            gSessionStatus->SetCaption(gJoinFailed
                ? CoopText(u"Worlds differ. Restart both windows with SPORE Coop.",
                    u"\u041c\u0438\u0440\u044b \u0440\u0430\u0437\u043b\u0438\u0447\u0430\u044e\u0442\u0441\u044f. \u041f\u0435\u0440\u0435\u0437\u0430\u043f\u0443\u0441\u0442\u0438\u0442\u0435 \u043e\u0431\u0430 \u043e\u043a\u043d\u0430 \u0447\u0435\u0440\u0435\u0437 SPORE Coop.")
                : gRemoteRenderDelayed
                    ? CoopText(u"Friend's creature is not ready. Details are in the mod log.",
                        u"\u041c\u043e\u0434\u0435\u043b\u044c \u0434\u0440\u0443\u0433\u0430 \u0435\u0449\u0451 \u043d\u0435 \u0433\u043e\u0442\u043e\u0432\u0430. \u041f\u043e\u0434\u0440\u043e\u0431\u043d\u043e\u0441\u0442\u0438 \u0432 \u0436\u0443\u0440\u043d\u0430\u043b\u0435 \u043c\u043e\u0434\u0430.")
                    : CoopText(u"World owner paused the game",
                        u"\u0425\u043e\u0437\u044f\u0438\u043d \u043c\u0438\u0440\u0430 \u043f\u043e\u0441\u0442\u0430\u0432\u0438\u043b \u0438\u0433\u0440\u0443 \u043d\u0430 \u043f\u0430\u0443\u0437\u0443"));
        SetButtonVisible(gSessionStatus.get(), showStatus);
        if (showStatus && mainWindow && gSessionStatus)
        {
            const float width = mainWindow->GetArea().GetWidth();
            gSessionStatus->SetArea({12, 45, width-12, 83});
            mainWindow->BringToFront(gSessionStatus.get());
        }


        if (!snapshot.invitePending) gInviteResponseSent = false;
        if (!snapshot.invitePending && !snapshot.inviteAccepted)
        {
            gInviteRequestSent = false;
            gAutoJoinAttempted = false;
            gJoinFailed = false;
            gAutoJoinRetryAfter = 0;
        }

        const int state = (isHost ? 1 : 0) | (pauseMenuOpen ? 2 : 0) |
            (stageGame ? 4 : 0) | (showHostInvite ? 8 : 0) |
            (gInviteButton ? 16 : 0);
        static int previousState = -1;
        if (state != previousState)
        {
            previousState = state;
            char line[256]{};
            sprintf_s(line,
                "Invite UI: role=%s panel=%d stage=%d visible=%d button=%d panelId=0x%08X panelType=%s.",
                CoopNet::GetRole(), pauseMenuOpen ? 1 : 0, stageGame ? 1 : 0,
                showHostInvite ? 1 : 0, gInviteButton ? 1 : 0,
                pausePanel ? pausePanel->GetControlID() : 0,
                pausePanel && pausePanel->GetComponentName()
                    ? pausePanel->GetComponentName() : "none");
            WriteProbeLog(line);
        }
    }

    void ReleaseCoopPause()
    {
        auto manager = Simulator::cGameTimeManager::Get();
        if (!manager) { gCoopPause.applied = false; return; }
        gCoopPause.Set(false, [] {}, [manager] {
            manager->Resume(Simulator::TimeManagerPause::Gameplay);
        });
    }

    void UpdateAuthoritativePause(const CoopNet::Snapshot& snapshot)
    {
        const bool owner = CoopSession::IsWorldOwner(snapshot, CoopNet::GetRole());
        if (owner)
        {
            ReleaseCoopPause();
            const bool paused = snapshot.inviteAccepted && Simulator::IsCellGame() &&
                FindVisiblePausePanel() != nullptr;
            if (snapshot.inviteAccepted && int(paused) != gLastHostPauseSent)
            {
                CoopNet::SubmitHostPause(paused);
                gLastHostPauseSent = int(paused);
            }
            return;
        }
        gLastHostPauseSent = -1;
        auto manager = Simulator::cGameTimeManager::Get();
        if (!manager) return;
        gCoopPause.Set(CoopSession::ShouldMirrorPause(snapshot, CoopNet::GetRole(),
            Simulator::IsCellGame()), [manager] {
                manager->Pause(Simulator::TimeManagerPause::Gameplay);
                WriteProbeLog("World owner paused the session.");
            }, [manager] {
                manager->Resume(Simulator::TimeManagerPause::Gameplay);
                WriteProbeLog("World owner resumed the session.");
            });
    }

    void UpdateSessionEndedUI(const CoopNet::Snapshot& snapshot)
    {
        auto root = WindowManager.GetMainWindow();
        if (snapshot.sessionEnded && root && !gSessionEndedPanel)
        {
            gSessionEndedPanel = CreateCoopPanel(0x5C0F1011);
            gSessionEndedTitle = CreateCoopButton(0x5C0F1012, u"", CoopUi::Style::Label);
            gSessionEndedOk = CreateCoopButton(kSessionEndedOkID, u"OK");
        }
        if (snapshot.sessionEnded && root)
        {
            const auto area = root->GetArea();
            const float left = (area.GetWidth() - 390) * 0.5f;
            const float top = (area.GetHeight() - 150) * 0.5f;
            if (gSessionEndedPanel) gSessionEndedPanel->SetArea({left,top,left+390,top+150});
            if (gSessionEndedTitle)
            {
                gSessionEndedTitle->SetArea({left+15,top+20,left+375,top+66});
                gSessionEndedTitle->SetCaption(snapshot.disconnectReason == "host_left"
                    ? u"\u0425\u043e\u0441\u0442 \u0432\u044b\u0448\u0435\u043b"
                    : u"\u0421\u043e\u0435\u0434\u0438\u043d\u0435\u043d\u0438\u0435 \u043f\u043e\u0442\u0435\u0440\u044f\u043d\u043e");
            }
            if (gSessionEndedOk) gSessionEndedOk->SetArea({left+120,top+89,left+270,top+130});
        }
        for (auto window : {gSessionEndedPanel.get(),gSessionEndedTitle.get(),gSessionEndedOk.get()})
        {
            SetButtonVisible(window,snapshot.sessionEnded);
            if (window && root && snapshot.sessionEnded) root->BringToFront(window);
        }
    }

    void CoopUpdate()
    {
        const auto snapshot = LocalWorldSnapshot(CoopNet::GetSnapshot());
        UpdateSessionEndedUI(snapshot);
        if (!snapshot.enabled || !snapshot.connected)
        {
            SetButtonVisible(gInviteButton.get(), false);
            SetButtonVisible(gAcceptInviteButton.get(), false);
            SetButtonVisible(gDeclineInviteButton.get(), false);
            HideCoopUI();
            ReleaseCoopPause();
            gObservedInviteAccepted = false;
            gReturnAfterInvite = false;

            if (CoopVisual::HasCellIndex(gRemoteCellIndex))
                RemoveRemoteCell("cooperative peer disconnected");
            RemoveHostAppearanceProxy("Cooperative peer disconnected; restored local appearance.");
            RemoveMirroredNpcs("cooperative peer disconnected");
            return;
        }

        if (snapshot.connectionGeneration != gObservedConnectionGeneration)
        {
            if (CoopVisual::HasCellIndex(gRemoteCellIndex))
                RemoveRemoteCell("cooperative connection restarted");
            RemoveHostAppearanceProxy("Cooperative connection restarted; restored local appearance.");
            RemoveMirroredNpcs("cooperative connection restarted");
            gObservedConnectionGeneration = snapshot.connectionGeneration;
            gLastSubmittedAppearanceKey = ResourceKey{};
            gLastSubmittedAppearanceCellResource = 0;
            gAppliedRemoteAppearanceSequence = 0;
            gRemoteAppearanceAttempted = false;
            gRemoteCreationKey = ResourceKey{};
            gRemoteAppearanceResource = nullptr;
            gProgressSeedSent = false;
            gProgressSync.Reset();
            gAppliedSpeciesSequence = 0;
            gPendingSpeciesSequence=0; gPendingSpecies.clear();
            gLastLocalSpecies.clear();
            gLastSpeciesTick = 0;
            gInviteResponseSent = false;
            gInviteRequestSent = false;
            gAutoJoinAttempted = false;
            gAutoJoinRetryAfter = 0;
            gJoinedPlayerPlaced = false;
            gLastHostPauseSent = -1;
            ReleaseCoopPause();
            gObservedInviteAccepted = false;
            gReturnAfterInvite = false;
            gLastNetworkError.clear();
            WriteProbeLog("Cooperative connection state reset after handshake.");
        }

        if (snapshot.remotePeerGeneration != gObservedRemotePeerGeneration)
        {
            RemoveRemoteCell("cooperative peer reconnected");
            RemoveHostAppearanceProxy("Cooperative peer reconnected; restored local appearance.");
            RemoveMirroredNpcs("cooperative peer reconnected");
            gObservedRemotePeerGeneration = snapshot.remotePeerGeneration;
            gRemoteAppearanceAttempted = false;
            gAppliedRemoteAppearanceSequence = 0;
            gRemoteCreationKey = ResourceKey{};
            gRemoteAppearanceResource = nullptr;
            gLastSubmittedAppearanceKey = ResourceKey{};
        }

        if (!snapshot.lastError.empty() && snapshot.lastError != gLastNetworkError)
        {
            gLastNetworkError = snapshot.lastError;
            const std::string line = "Cooperative server error: " + snapshot.lastError;
            WriteProbeLog(line.c_str());
        }

        if (snapshot.worldGeneration != gObservedWorldGeneration)
        {
            gObservedWorldGeneration = snapshot.worldGeneration;
            gWorldActionApplied = 0;
            gProgressSync.Reset();
            gAppliedSpeciesSequence=0; gPendingSpeciesSequence=0; gPendingSpecies.clear();
            gEditorInitialModelPending = false;
            gEditorEntrySpeciesSequence = 0;
            gWasEditorMode = false;
            gLastLocalSpecies.clear();
            gProgressSeedSent = false;
            gJoinedPlayerPlaced = false;
            gAutoJoinAttempted = false;
            gLastHostPauseSent = -1;
            RemoveRemoteCell("another cooperative world selected");
            RemoveHostAppearanceProxy("Another cooperative world selected.");
            RemoveMirroredNpcs("another cooperative world selected");
        }
        if (!gObservedInviteAccepted && snapshot.inviteAccepted)
        {
            gReturnAfterInvite = true;
            gJoinedPlayerPlaced = false;
            // Accepting while already in a stage must not auto-rejoin on exit.
            if (Simulator::IsCellGame()) gAutoJoinAttempted = true;
        }
        gObservedInviteAccepted = snapshot.inviteAccepted;
        if (gReturnAfterInvite) ReturnToGameAfterInvite();
        UpdateInviteUI(snapshot);
        UpdateAuthoritativePause(snapshot);

        const ULONG64 now = GetTickCount64();
        // Only load a campaign fully prepared before either process started.
        if (snapshot.inviteAccepted && snapshot.inviteFrom != CoopNet::GetRole() &&
            snapshot.hasRemotePosition && !gAutoJoinAttempted && now >= gAutoJoinRetryAfter &&
            !Simulator::IsStageGameMode() && !Simulator::IsLoadingGameMode() &&
            !Simulator::IsEditorMode())
        {
            // The accept handler already verified the launch-prepared saves.
            // Rechecking after telling the peer we joined could strand an
            // accepted session if the owner saved in the meantime.
            gAutoJoinAttempted = JoinSavedWorld();
            if (!gAutoJoinAttempted) gAutoJoinRetryAfter = now + 3000;
            return;
        }

        UpdateSharedEditor(snapshot, now);

        if (Simulator::IsCellGame() && !snapshot.editorOpen)
        {
            if ((!snapshot.inviteAccepted || !snapshot.hasRemotePosition) &&
                CoopVisual::HasCellIndex(gRemoteCellIndex))
                RemoveRemoteCell("cooperative peer left");
            auto player = GetLocalPlayerCell();
            if (player && now - gLastPositionTick >= 50)
            {
                auto game = Simulator::Cell::cCellGame::Get();
                auto data = game ? game->mpSerializableData.get() : nullptr;
                const ResourceKey speciesKey = data
                    ? data->mPlayerCreatureKey : player->mModelKey;
                if (snapshot.inviteAccepted && !gJoinedPlayerPlaced && snapshot.hasRemotePosition &&
                    now - snapshot.remotePositionReceivedTick < 2000)
                {
                    if (!CoopSession::IsWorldOwner(snapshot, CoopNet::GetRole()))
                    {
                        const Math::Vector3 spawn(snapshot.remoteX + std::max(2.0f, snapshot.remoteScale * 4.0f),
                            snapshot.remoteY, snapshot.remoteZ);
                        if (!MoveCellBody(player, spawn, player->mTransform.GetRotation().ToQuaternion()))
                            return;
                        WriteProbeLog("Joining player and physics body placed beside owner; local input remains native.");
                    }
                    gJoinedPlayerPlaced = true;
                }
                UpdateLocalCellStability(player, speciesKey, now);
                SubmitLocalAppearance(speciesKey, now);
                const auto& position = player->GetPosition();
                static ULONG64 lastMovementTrace = 0;
                static const bool traceMovement = GetEnvironmentVariableA(
                    "SPORE_COOP_TRACE_MOVEMENT", nullptr, 0) != 0;
                if (traceMovement && now - lastMovementTrace >= 2000)
                {
                    char line[512]{};
                    auto time = Simulator::cGameTimeManager::Get();
                    DWORD foregroundPID = 0;
                    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPID);
                    sprintf_s(line, "Movement: role=%s avatar=%d x=%.3f y=%.3f z=%.3f target=(%.3f,%.3f) impulse=(%.3f,%.3f) focused=%d accepted=%d pause=%d ownedPause=%d menuPause=%d tutorialPause=%d remoteLoaded=%d remoteRegistered=%d scale=%.3f worldUnit=%.6f",
                        CoopNet::GetRole(), player->Index(), position.x, position.y, position.z,
                        player->mTargetPosition.x, player->mTargetPosition.y,
                        player->field_90.x, player->field_90.y,
                        foregroundPID == GetCurrentProcessId(),
                        snapshot.inviteAccepted, time ? time->IsPaused() : -1, gCoopPause.applied,
                        time ? time->GetPauseCount(Simulator::TimeManagerPause::UIToggle) : -1,
                        time ? time->GetPauseCount(Simulator::TimeManagerPause::Tutorial) : -1,
                        CoopVisual::HasCellIndex(gRemoteCellIndex) && game->mCells.GetIfNotDeleted(gRemoteCellIndex) ? 1 : 0,
                        CoopVisual::HasCellIndex(gRemoteCellIndex) && GetCellVisual(game->mCells.GetIfNotDeleted(gRemoteCellIndex)) ? 1 : 0,
                        player->mTransform.GetScale(),gWorldCoordinates.unit);
                    WriteProbeLog(line);
                    lastMovementTrace = now;
                }
                auto renderPose = ReadPlayerRenderPose(player);
                const auto networkPosition = gWorldCoordinates.Encode({position.x,position.y,position.z});
                renderPose.x=float(networkPosition.x); renderPose.y=float(networkPosition.y); renderPose.z=float(networkPosition.z);
                renderPose.scale *= float(gWorldCoordinates.unit);
                if (gLocalPlayerHiddenByProxy) renderPose.visible = true;
                CoopNet::SubmitPosition(float(networkPosition.x), float(networkPosition.y), float(networkPosition.z),
                    speciesKey.instanceID, speciesKey.typeID,
                    speciesKey.groupID,
                    player->mCellResource ? player->mCellResource->mInstanceID : 0,
                    player->mTransform.GetScale()*float(gWorldCoordinates.unit), player->mTargetSize*float(gWorldCoordinates.unit),
                    gLocalPlayerHiddenByProxy ? 1.0f : player->mOpacity, &renderPose);
                gLastPositionTick = now;
            }
            UpdateRemoteCell(snapshot);
            UpdateHostAppearanceProxy(snapshot, player);
            if (snapshot.inviteAccepted &&
                CoopSession::IsWorldOwner(snapshot, CoopNet::GetRole()))
            {
                ApplyWorldActions();
                if (now - gLastNpcSnapshotTick >= 100 &&
                    !IsPartCinematic(Simulator::Cell::cCellGame::Get()))
                {
                    CoopNet::SubmitNpcSnapshot(ReadAuthoritativeNpcs(player),gWorldActionApplied);
                    gLastNpcSnapshotTick = now;
                }
                RemoveMirroredNpcs("this window owns the world");
            }
            else if (snapshot.inviteAccepted)
                UpdateMirroredNpcs(snapshot, player);
        }
        else
        {
            if (CoopVisual::HasCellIndex(gRemoteCellIndex))
            {
                RemoveRemoteCell("left cell gameplay or entered editor");
            }
            RemoveHostAppearanceProxy("Left cell gameplay; restored the native local appearance.");
            RemoveMirroredNpcs("left cell gameplay or entered editor");
        }

        // The host may seed the campaign before the guest accepts; all later
        // progress application is gated by the accepted invitation.
        if (now - gLastProgressTick >= 200 && (snapshot.inviteAccepted ||
            (!snapshot.progressInitialized && snapshot.inviteFrom == CoopNet::GetRole())))
        {
            UpdateSharedCellProgress(snapshot);
            gLastProgressTick = now;
        }
    }

    void Spawn()
    {
        if (Simulator::IsCellGame())
        {
            auto game = Simulator::Cell::cCellGame::Get();
            if (!game || !game->mpCellQuery) return;
            auto player = GetLocalPlayerCell();
            if (!player || !player->mCellResource) return;

            auto index = CreatePlayerCellClone(player,
                player->GetPosition() + Math::Vector3(4.0f, 0.0f, 0.0f), false);

            auto second = game->mCells.GetIfNotDeleted(index);
            if (!second)
            {
                App::ConsolePrintF("NPC cell creation failed (index %d).", index);
                return;
            }

            // CreateCellObject uses the resource's default size. The player can be
            // at an intermediate growth size, so copy both current and target size.
            second->mTransform.SetScale(player->mTransform.GetScale());
            second->mTargetSize = player->mTargetSize;
            second->mTargetOpacity = player->mTargetOpacity;

            char logLine[512]{};
            sprintf_s(logLine,
                "coopSpawn cell index=%d playerModel=%08X npcModel=%08X playerScale=%.4f npcScale=%.4f playerTargetSize=%.4f npcTargetSize=%.4f",
                index, player->mModelKey.instanceID, second->mModelKey.instanceID,
                player->mTransform.GetScale(), second->mTransform.GetScale(),
                player->mTargetSize, second->mTargetSize);
            WriteProbeLog(logLine);
            App::ConsolePrintF(
                "NPC cell %d cloned: player model 0x%x, NPC model 0x%x, scale %.3f.",
                index, player->mModelKey.instanceID, second->mModelKey.instanceID,
                second->mTransform.GetScale());
        }
        else if (Simulator::IsCreatureGame())
        {
            auto manager = Simulator::cGameNounManager::Get();
            if (!manager) return;
            auto player = manager->GetAvatar();
            if (!player || !player->mpSpeciesProfile) return;
            cCreatureAnimalPtr second = Simulator::cCreatureAnimal::Create(
                player->GetPosition() + Math::Vector3(4.0f, 0.0f, 0.0f),
                player->mpSpeciesProfile, player->mAge, nullptr, false, false);
            App::ConsolePrintF(second ? "NPC creature created. Check its species and AI manually."
                : "NPC creature creation failed.");
        }
        else App::ConsolePrintF("Load a disposable Cell or Creature campaign before using coopSpawn.");
    }

    bool ValidateIncomingSavedWorld(const char* inviterRole)
    {
        if (!inviterRole || (std::strcmp(inviterRole, "host") != 0 &&
            std::strcmp(inviterRole, "guest") != 0)) return false;
        char server[512]{};
        GetEnvironmentVariableA("SPORE_COOP_SERVER", server, sizeof(server));
        // Shared-disk verification applies only to this PC's local profiles.
        if (server[0] && std::strcmp(server, "127.0.0.1") != 0) return false;
        wchar_t roamingData[MAX_PATH]{};
        if (!GetEnvironmentVariableW(L"APPDATA", roamingData, MAX_PATH)) return false;
        const std::filesystem::path roaming(roamingData);
        const auto source = roaming / (std::strcmp(inviterRole, "host") == 0 ? L"Spore" : L"SporeCoop2") / L"Games" / L"Game0";
        const auto target = roaming / (IsProfile2() ? L"SporeCoop2" : L"Spore") / L"Games" / L"Game0";
        const bool ready = CoopSaves::Compatible(source, target);
        WriteProbeLog(ready ? "Join: campaign files match; no live save writes." :
            "Join refused: campaigns differ. Close both games and use Start-TwoSpore.ps1.");
        return ready;
    }

    bool FindNewestSavedGame(eastl::string16& gameName)
    {
        wchar_t appData[MAX_PATH]{};
        if (!GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH)) return false;

        std::wstring pattern(appData);
        pattern += IsProfile2()
            ? L"\\SporeCoop2\\Games\\Game0\\*.spo"
            : L"\\Spore\\Games\\Game0\\*.spo";

        WIN32_FIND_DATAW current{};
        HANDLE search = FindFirstFileW(pattern.c_str(), &current);
        if (search == INVALID_HANDLE_VALUE) return false;

        std::wstring newest;
        FILETIME newestTime{};
        do
        {
            if ((current.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) continue;
            if (newest.empty() || CompareFileTime(&current.ftLastWriteTime, &newestTime) > 0)
            {
                newest = current.cFileName;
                newestTime = current.ftLastWriteTime;
            }
        } while (FindNextFileW(search, &current));
        FindClose(search);

        if (newest.size() <= 4) return false;
        newest.resize(newest.size() - 4); // Remove .spo.
        gameName.clear();
        for (wchar_t character : newest)
            gameName.push_back(static_cast<char16_t>(character));
        return !gameName.empty();
    }

    bool JoinSavedWorld()
    {
        if (Simulator::IsStageGameMode())
        {
            App::ConsolePrintF("coopJoin: this window is already inside a world.");
            return false;
        }
        if (Simulator::IsLoadingGameMode() || Simulator::IsEditorMode())
        {
            App::ConsolePrintF("coopJoin: wait until loading or the editor has finished.");
            return false;
        }

        eastl::string16 gameName;
        if (!FindNewestSavedGame(gameName))
        {
            App::ConsolePrintF("coopJoin: no .spo campaign was found in this profile.");
            return false;
        }

        Simulator::GameLoadParameters parameters{};
        parameters.mGameName = gameName;
        GameNounManager.EnsurePlayer();
        const bool accepted = GamePersistenceManager.LoadGame(parameters);
        WriteProbeLog(accepted
            ? "coopJoin: newest saved campaign accepted by GamePersistenceManager."
            : "coopJoin: GamePersistenceManager refused the saved campaign.");
        App::ConsolePrintF(accepted
            ? "coopJoin: loading the newest saved campaign..."
            : "coopJoin: Spore refused to load the saved campaign.");
        return accepted;
    }

    class ProbeCommand : public ArgScript::ICommand
    {
        bool mSpawn;
    public:
        explicit ProbeCommand(bool spawn) : mSpawn(spawn) {}
        void ParseLine(const ArgScript::Line&) override
        {
            if (mSpawn) Spawn();
            else Status();
        }
        const char* GetDescription(ArgScript::DescriptionMode) const override
        {
            return mSpawn ? "Experimental creation of a same-species NPC. Test only on a disposable campaign."
                : "Print current Cell or Creature avatar state.";
        }
    };

    class JoinCommand : public ArgScript::ICommand
    {
    public:
        void ParseLine(const ArgScript::Line&) override
        {
            JoinSavedWorld();
        }
        const char* GetDescription(ArgScript::DescriptionMode) const override
        {
            return "Load the newest campaign in this window, then attach it to the cooperative session.";
        }
    };

    void Initialize()
    {
        CheatManager.AddCheat("coopStatus", new ProbeCommand(false));
        CheatManager.AddCheat("coopSpawn", new ProbeCommand(true));
        CheatManager.AddCheat("coopJoin", new JoinCommand());
        gInviteWinProc = new InviteWinProc();
        gUpdateListener = App::AddUpdateFunction(CoopUpdate);
        gEditorListener = new EditorRelayListener();
        MessageManager.AddListener(gEditorListener.get(), Simulator::kMsgEnterEditor);
        const bool networkEnabled = CoopNet::StartFromEnvironment();
        WriteProbeLog("Native build: " __DATE__ " " __TIME__ "; registered peer renderer.");
        WriteProbeLog(GetRemoveCellFunction() ? "Verified native Cell removal ABI; complete cell cleanup enabled."
            : "Unsupported Cell removal ABI; network cell creation is disabled for this executable.");
        WriteProbeLog(GetCellMotionFunctions().Ready() && gCellGraphicsHookAttached
            ? "Verified native Cell movement ABI; articulated body movement and pre-graphics synchronization enabled."
            : "Unsupported Cell movement ABI or graphics hook; network cell creation is disabled.");
        WriteProbeLog(GetUnregisterCollisionFunction()
            ? "Verified native replica collision isolation; synthetic cells keep death flag clear."
            : "Unsupported collision isolation ABI; network cell creation is disabled.");
        WriteProbeLog(gCellGrowthHookAttached && gWorldRemovalHookAttached
            ? "Protocol 4: verified world-growth coordinates and shared object interactions enabled."
            : "World growth/removal hook unavailable; shared world adapter cannot run.");
        WriteProbeLog(GetCellProgressFunctions().Ready()
            ? "Verified native shared growth, part/quest notifications, cinematic and campaign editor entry."
            : "Unsupported Cell progress ABI; shared native progression and mirrored editor entry disabled.");
        WriteProbeLog(networkEnabled
            ? "Initialize completed; commands registered and cooperative client started."
            : "Initialize completed; commands registered; cooperative client disabled (no environment)."
        );
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        PrepareDetours(module);
        if (GetCellMotionFunctions().Ready())
        {
            gCellGraphicsUpdateOriginal = reinterpret_cast<CellGraphicsUpdateFunction>(VerifiedMotionCode(
                CoopEngine::kGraphicsRva, CoopEngine::kGraphicsSize, CoopEngine::kGraphicsHash,
                CoopEngine::kGraphicsRelocations));
            if (gCellGraphicsUpdateOriginal)
                gCellGraphicsHookAttached = DetourAttach(reinterpret_cast<PVOID*>(&gCellGraphicsUpdateOriginal),
                    CellGraphicsUpdateHook) == NO_ERROR;
        }
        gWorldRemoveOriginal = GetRemoveCellFunction();
        if (gWorldRemoveOriginal)
            gWorldRemovalHookAttached = DetourAttach(reinterpret_cast<PVOID*>(&gWorldRemoveOriginal), WorldRemoveHook) == NO_ERROR;
        gCellGrowthOriginal = VerifiedMotionCode(CoopEngine::kGrowthRva,
            CoopEngine::kGrowthSize, CoopEngine::kGrowthHash, CoopEngine::kGrowthRelocations);
        if (gCellGrowthOriginal)
            gCellGrowthHookAttached = DetourAttach(reinterpret_cast<PVOID*>(&gCellGrowthOriginal), CellGrowthHook) == NO_ERROR;
        if (IsProfile2())
        {
            gCreateMutexAOriginal = reinterpret_cast<CreateMutexAFunction>(
                GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateMutexA"));
            if (gCreateMutexAOriginal)
            {
                gMutexDetourAttached = DetourAttach(
                    reinterpret_cast<PVOID*>(&gCreateMutexAOriginal),
                    CreateMutexAHook) == NO_ERROR;
            }
        }
        ProfilePathsDetour::attach(GetAddress(App::cAppSystem, SetUserDirNames));
        if (CommitDetours() != NO_ERROR) { gCellGraphicsHookAttached = false; gCellGrowthHookAttached = false; gWorldRemovalHookAttached = false; }
        ModAPI::AddPostInitFunction(Initialize);
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        PrepareDetours(module);
        if (gCellGraphicsHookAttached)
            DetourDetach(reinterpret_cast<PVOID*>(&gCellGraphicsUpdateOriginal), CellGraphicsUpdateHook);
        if (gWorldRemovalHookAttached)
            DetourDetach(reinterpret_cast<PVOID*>(&gWorldRemoveOriginal), WorldRemoveHook);
        if (gCellGrowthHookAttached)
            DetourDetach(reinterpret_cast<PVOID*>(&gCellGrowthOriginal), CellGrowthHook);
        if (gMutexDetourAttached)
        {
            DetourDetach(reinterpret_cast<PVOID*>(&gCreateMutexAOriginal),
                CreateMutexAHook);
        }
        ProfilePathsDetour::detach();
        CommitDetours();
    }
    return TRUE;
}
