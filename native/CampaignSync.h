// SPDX-License-Identifier: GPL-3.0-or-later
// Included inside Probe.cpp's namespace, after the binary codec helpers.

using SaveWorldFunction = void(__thiscall*)(void*, const char16_t*, bool);
using SaveFilesFunction = bool(__thiscall*)(void*);
SaveWorldFunction gSaveWorldOriginal = nullptr;
SaveFilesFunction gSavePrepareOriginal = nullptr, gSaveCleanupOriginal = nullptr;
bool gCampaignSaveHooks = false;
CoopSession::WorldSaveLease gWorldSaveLease;
CoopSession::WorldLifecycle gWorldLifecycle;
bool gGuestExitInProgress=false;

void ObserveWorldSaveOwner(const CoopNet::Snapshot& state)
{
    // The Cell singleton can retain serializable data on the galaxy screen.
    // Use the completed mode transition instead of probing a stale world pool.
    const bool leftWorld = Simulator::GetGameModeID()==GameModeIDs::kGGEMode &&
        !state.inviteAccepted && !gGuestExitInProgress;
    gWorldSaveLease.Observe(state, CoopNet::GetRole(), leftWorld);
}

bool BlockGuestWorldSave()
{
    ObserveWorldSaveOwner(CoopNet::GetSnapshot());
    return gGuestExitInProgress || gWorldSaveLease.blocked;
}

bool __fastcall SavePrepareHook(void* self, void*)
{
    if (!BlockGuestWorldSave()) return gSavePrepareOriginal(self);
    WriteProbeLog("Guest campaign file preparation blocked: the world is saved by its owner.");
    return true;
}

bool __fastcall SaveCleanupHook(void* self, void*)
{
    return BlockGuestWorldSave() ? true : gSaveCleanupOriginal(self);
}

void __fastcall SaveWorldHook(void* self, void*, const char16_t* name, bool update)
{
    if (!BlockGuestWorldSave()) { gSaveWorldOriginal(self, name, update); return; }
    // Keep the native save state machine's start/end notifications. The outer
    // handler still restores the cursor, pause lease and Save-and-Quit flow.
    // Preparation and cleanup are also intercepted: both modify Game0 files.
    App::StandardMessage message;
    message.params[0].int32 = 1;
    MessageManager.MessageSend(0x01A0219E, &message);
    message.params[0].int32 = 6;
    MessageManager.MessageSend(0x01A0219E, &message);
    WriteProbeLog("Guest campaign save skipped: only the invitation's world owner writes the world.");
}

bool AttachCampaignSaveHooks()
{
    using namespace CoopEngine;
    gSaveWorldOriginal = reinterpret_cast<SaveWorldFunction>(VerifiedMotionCode(
        kSaveWorldRva,kSaveWorldSize,kSaveWorldHash,kSaveWorldRelocations));
    gSavePrepareOriginal = reinterpret_cast<SaveFilesFunction>(VerifiedMotionCode(
        kSavePrepareRva,kSavePrepareSize,kSavePrepareHash,kSavePrepareRelocations));
    gSaveCleanupOriginal = reinterpret_cast<SaveFilesFunction>(VerifiedMotionCode(
        kSaveCleanupRva,kSaveCleanupSize,kSaveCleanupHash,kSaveCleanupRelocations));
    if (!gSaveWorldOriginal || !gSavePrepareOriginal || !gSaveCleanupOriginal) return false;
    if (DetourAttach(reinterpret_cast<PVOID*>(&gSaveWorldOriginal),SaveWorldHook)!=NO_ERROR) return false;
    if (DetourAttach(reinterpret_cast<PVOID*>(&gSavePrepareOriginal),SavePrepareHook)!=NO_ERROR)
    {
        DetourDetach(reinterpret_cast<PVOID*>(&gSaveWorldOriginal),SaveWorldHook); return false;
    }
    if (DetourAttach(reinterpret_cast<PVOID*>(&gSaveCleanupOriginal),SaveCleanupHook)!=NO_ERROR)
    {
        DetourDetach(reinterpret_cast<PVOID*>(&gSavePrepareOriginal),SavePrepareHook);
        DetourDetach(reinterpret_cast<PVOID*>(&gSaveWorldOriginal),SaveWorldHook); return false;
    }
    return true;
}
