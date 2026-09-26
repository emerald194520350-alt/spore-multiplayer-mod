// SPDX-License-Identifier: GPL-3.0-or-later
// Native timeline category 12 contains 27 DWORD payloads (E394A0).
// Transfer logical values only; lists, vectors and resource pointers stay local.
using TimelineShowFunction = void(__thiscall*)(void*,bool);
TimelineShowFunction gTimelineShowOriginal=nullptr;
bool gTimelineHook=false, gTimelineVisible=false;
using HistoryEventFunction = void*(__cdecl*)(unsigned,const ResourceKey*,const ResourceKey*,const ResourceKey*,const ResourceKey*);
HistoryEventFunction gHistoryEventOriginal=nullptr;
bool gDietHistoryHook=false;
std::uint64_t gHistoryWorld=0, gHistoryConnection=0, gHistorySequence=0, gHistoryTick=0;
std::string gHistorySent;
std::map<CoopHistory::Key,CoopHistory::Key> gHistoryModels;

void* __cdecl HistoryEventHook(unsigned eventID,const ResourceKey* a,const ResourceKey* b,
    const ResourceKey* c,const ResourceKey* d)
{
    auto result=gHistoryEventOriginal(eventID,a,b,c,d);
    // E7D2F9 = plant pickup; E7D343 = meat pickup. AddFood itself only
    // updates counters/growth: replaying progress never records these events.
    if (Simulator::IsCellGame() && (eventID==0x9ef61113u || eventID==0xac7161b5u))
    {
        const auto state=CoopNet::GetSnapshot();
        if (state.connected && state.inviteAccepted && gProgressSync.IsInitialized() &&
            !CoopSession::IsWorldOwner(state,CoopNet::GetRole()))
        {
            // Queue the earned counters before their history event. Capture is
            // read-only for the engine; do not replay growth inside this hook.
            CoopProgress::Event gain;
            const auto current=ReadCellProgress();
            if (gProgressSync.Capture(current,gain,false))
                CoopNet::SubmitProgressDelta(gain.sequence,gain.delta,current.unlocks);
            CoopNet::SubmitDietEvent(eventID);
        }
    }
    return result;
}

bool ReadableHistoryMemory(const void* ptr,size_t size)
{
    if (!ptr) return false;
    MEMORY_BASIC_INFORMATION info{};
    const auto at=reinterpret_cast<std::uintptr_t>(ptr);
    return VirtualQuery(ptr,&info,sizeof(info)) && info.State==MEM_COMMIT &&
        !(info.Protect&(PAGE_NOACCESS|PAGE_GUARD)) && size<=info.RegionSize &&
        at>=reinterpret_cast<std::uintptr_t>(info.BaseAddress) &&
        at-reinterpret_cast<std::uintptr_t>(info.BaseAddress)<=info.RegionSize-size;
}

struct HistoryFunctions
{
    void*(__cdecl* get)();
    void(__thiscall* clear)(void*,unsigned);
    void*(__thiscall* create)(void*,unsigned,unsigned);
    void(__thiscall* resize)(void*,unsigned);
    bool Ready() const {return get && clear && create && resize;}
};

const HistoryFunctions& GetHistoryFunctions()
{
    static const auto functions=[] {
        using namespace CoopEngine;
        const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        auto staticCode=[base](std::uintptr_t rva,size_t size,std::uint32_t hash)->void* {
            auto ptr=reinterpret_cast<unsigned char*>(base+rva);
            return ReadableHistoryMemory(ptr,size) && MatchesStaticCode(ptr,size,size,hash) ? ptr : nullptr;
        };
        return HistoryFunctions{
            reinterpret_cast<void*(__cdecl*)()>(VerifiedMotionCode(kHistoryGetRva,kHistoryGetSize,kHistoryGetHash,kHistoryGetRelocations)),
            reinterpret_cast<void(__thiscall*)(void*,unsigned)>(staticCode(0x3ebc30,0x35,0x8bd1df60)),
            reinterpret_cast<void*(__thiscall*)(void*,unsigned,unsigned)>(VerifiedMotionCode(kHistoryCreateRva,kHistoryCreateSize,kHistoryCreateHash,kHistoryCreateRelocations)),
            reinterpret_cast<void(__thiscall*)(void*,unsigned)>(staticCode(0x3eb8f0,0x4b,0x46b40d3c))};
    }();
    return functions;
}

bool CaptureHistory(CoopHistory::Snapshot& result)
{
    const auto& functions=GetHistoryFunctions();
    if (!functions.Ready()) return false;
    auto manager=static_cast<unsigned char*>(functions.get());
    auto game=Simulator::Cell::cCellGame::Get();
    if (!ReadableHistoryMemory(manager,0x1c) || !game || !game->mpSerializableData) return false;
    const auto key=game->mpSerializableData->mPlayerCreatureKey;
    result.model={key.instanceID,key.typeID,key.groupID}; result.events.clear();
    auto buckets=*reinterpret_cast<unsigned char***>(manager+0x10);
    const auto count=*reinterpret_cast<unsigned*>(manager+0x14);
    if (!count || count>65536 || !ReadableHistoryMemory(buckets,count*sizeof(void*))) return false;
    // Native hash_map<uint32,list<Record>> hashes the category by identity.
    auto entry=buckets[12%count];
    for (unsigned attempts=0;entry && attempts<1024;++attempts)
    {
        if (!ReadableHistoryMemory(entry,0x14)) return false;
        if (*reinterpret_cast<unsigned*>(entry)==12)
        {
            return CoopHistory::ReadList(entry+4,result.events,ReadableHistoryMemory);
        }
        entry=*reinterpret_cast<unsigned char**>(entry+0x10);
    }
    return entry==nullptr;
}

void ApplyOwnerHistory(const CoopNet::Snapshot& state)
{
    if (!gTimelineHook || !state.connected || !state.inviteAccepted ||
        CoopSession::IsWorldOwner(state,CoopNet::GetRole()) || state.historyBlob.empty()) return;
    const auto& functions=GetHistoryFunctions();
    auto game=Simulator::Cell::cCellGame::Get();
    if (!functions.Ready() || !Simulator::IsCellGame() || !game || !game->mpSerializableData) return;
    std::vector<unsigned char> bytes; CoopHistory::Snapshot history;
    if (!Base64Decode(state.historyBlob,bytes) || !CoopHistory::Decode(bytes,history)) return;
    const auto local=game->mpSerializableData->mPlayerCreatureKey;
    gHistoryModels[history.model]={local.instanceID,local.typeID,local.groupID};
    auto manager=functions.get();
    if (!manager) return;
    functions.clear(manager,12);
    for (auto event:history.events)
    {
        for (const auto& model:gHistoryModels) CoopHistory::Remap(event,model.first,model.second);
        auto record=static_cast<unsigned char*>(functions.create(manager,12,event[2]));
        if (!record) return;
        functions.resize(record,27);
        std::memcpy(record,event.data(),12);
        auto data=*reinterpret_cast<void**>(record+0xc);
        std::memcpy(data,event.data()+3,108);
    }
}

void __fastcall TimelineShowHook(void* self,void*,bool visible)
{
    // Rebuild before the native UI takes references to the event records.
    if (visible && !gTimelineVisible) ApplyOwnerHistory(CoopNet::GetSnapshot());
    gTimelineVisible=visible;
    gTimelineShowOriginal(self,visible);
}

void UpdateSharedHistory(const CoopNet::Snapshot& state,std::uint64_t now)
{
    if (state.worldGeneration!=gHistoryWorld || state.connectionGeneration!=gHistoryConnection)
    {
        gHistoryWorld=state.worldGeneration; gHistoryConnection=state.connectionGeneration;
        gHistorySequence=0; gHistoryTick=0; gHistorySent.clear(); gHistoryModels.clear();
    }
    if (!state.connected || !state.inviteAccepted || !Simulator::IsCellGame() ||
        state.editorOpen || now-gHistoryTick<500) return;
    gHistoryTick=now;
    if (CoopSession::IsWorldOwner(state,CoopNet::GetRole()))
    {
        if (gDietHistoryHook)
        {
            // Event coordinates are calculated from the current shared diet.
            // A network event can arrive between the regular 200 ms updates.
            UpdateSharedCellProgress(state);
            auto game=Simulator::Cell::cCellGame::Get();
            if (game && game->mpSerializableData)
            {
                const ResourceKey empty{};
                for (auto eventID:CoopNet::TakeDietEvents(state.revision))
                    gHistoryEventOriginal(eventID,&game->mpSerializableData->mPlayerCreatureKey,&empty,&empty,&empty);
            }
        }
        CoopHistory::Snapshot history;
        if (!CaptureHistory(history)) return;
        auto bytes=CoopHistory::Encode(history);
        const auto blob=Base64Encode(bytes);
        // Periodic resend also covers a peer that reconnects between changes.
        if (blob!=gHistorySent || now%5000<600)
        { CoopNet::SubmitHistory(blob,++gHistorySequence,state.worldGeneration); gHistorySent=blob; }
    }
    else if (!gTimelineVisible) ApplyOwnerHistory(state);
}

bool AttachHistoryHook()
{
    using namespace CoopEngine;
    if (!GetHistoryFunctions().Ready()) return false;
    gTimelineShowOriginal=reinterpret_cast<TimelineShowFunction>(VerifiedMotionCode(
        kTimelineShowRva,kTimelineShowSize,kTimelineShowHash,kTimelineShowRelocations));
    return gTimelineShowOriginal && DetourAttach(reinterpret_cast<PVOID*>(&gTimelineShowOriginal),TimelineShowHook)==NO_ERROR;
}

bool AttachDietHistoryHook()
{
    using namespace CoopEngine;
    gHistoryEventOriginal=reinterpret_cast<HistoryEventFunction>(VerifiedMotionCode(
        kHistoryEventRva,kHistoryEventSize,kHistoryEventHash,kHistoryEventRelocations));
    return gHistoryEventOriginal &&
        DetourAttach(reinterpret_cast<PVOID*>(&gHistoryEventOriginal),HistoryEventHook)==NO_ERROR;
}
