#pragma once
#include "../native/HistoryData.h"
#include "../native/SessionRules.h"
#include "../native/CampaignAbi.h"

inline void TestCampaignData()
{
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    CoopSession::WorldSaveLease lease;
    CoopNet::Snapshot state;
    lease.Observe(state,"host",false);
    require(!lease.blocked,"Single-player saves must remain enabled");
    state.connected=state.inviteAccepted=true; state.inviteFrom="guest";
    lease.Observe(state,"host",false);
    require(lease.blocked,"Invitation owner takes precedence over socket host role");
    state.connected=state.inviteAccepted=false; state.inviteFrom.clear();
    lease.Observe(state,"host",false);
    require(lease.blocked,"Disconnect must not enable guest world saving");
    lease.Observe(state,"host",true);
    require(!lease.blocked,"Leaving the shared campaign restores ordinary saves");
    state.connected=state.invitePending=true; state.inviteFrom="guest";
    lease.Observe(state,"guest",false);
    require(!lease.blocked,"Inviter may save even with the guest socket role");
    lease.Observe(state,"host",false);
    require(!lease.blocked,"Unaccepted invitation cannot disable saving the current solo world");
    state.invitePending=false;state.inviteAccepted=true;
    lease.Observe(state,"host",false);
    require(lease.blocked,"Protection is armed on acceptance before loading the invited world");

    CoopSession::WorldLifecycle lifecycle;
    state.worldGeneration=12; state.connectionGeneration=3; state.sessionEnded=false;
    lifecycle.Observe(state,"host",true);
    require(lifecycle.guest && !lifecycle.ShouldAnnounceExit(state,true),"Invited player cannot announce owner exit");
    state.connected=state.inviteAccepted=false; state.inviteFrom.clear(); state.sessionEnded=true;
    lifecycle.Observe(state,"host",true);
    require(lifecycle.exitPending,"Guest remembers its role after EOF clears network state");
    state.connected=state.inviteAccepted=true; state.inviteFrom="host"; state.sessionEnded=false;
    state.worldGeneration=13;
    lifecycle.Observe(state,"host",false);
    require(!lifecycle.exitPending && !lifecycle.ShouldAnnounceExit(state,true),"Fresh invitation in menu must not end before loading");
    lifecycle.Observe(state,"host",true);
    require(!lifecycle.ShouldAnnounceExit(state,false),"Editor/loading are not a world exit");
    require(lifecycle.ShouldAnnounceExit(state,true),"Owner return to galaxy menu ends the session");
    lifecycle.leaveSent=true;
    require(!lifecycle.ShouldAnnounceExit(state,true),"World leave is submitted once");

    CoopHistory::Snapshot history, decoded;
    history.model={12,34,56}; history.events.resize(2);
    history.events[0][3]=0x3e2a3040; history.events[1][5]=3600;
    history.events[0][6]=34;history.events[0][7]=12;history.events[0][8]=56;
    auto bytes=CoopHistory::Encode(history);
    require(CoopHistory::Decode(bytes,decoded) && decoded.model==history.model && decoded.events==history.events,
        "History round trip preserves generation, diet and event values");
    CoopHistory::Remap(decoded.events[0],history.model,{78,90,56});
    require(decoded.events[0][6]==90 && decoded.events[0][7]==78 && decoded.events[0][3]==0x3e2a3040,
        "Timeline thumbnails remap the model without changing the event");
    bytes.pop_back(); require(!CoopHistory::Decode(bytes,decoded),"Truncated history rejected");
    bytes=CoopHistory::Encode(history); bytes[0]=0;
    require(!CoopHistory::Decode(bytes,decoded),"Unknown history schema rejected");
    history.events[0][4]=0x7fc00000; bytes=CoopHistory::Encode(history);
    require(!CoopHistory::Decode(bytes,decoded),"NaN evolutionary time rejected");
    history.events.resize(321);require(CoopHistory::Encode(history).empty(),"Oversized timeline rejected");
    history.events.clear();bytes=CoopHistory::Encode(history);
    require(CoopHistory::Decode(bytes,decoded) && decoded.events.empty(),"An empty authoritative timeline clears stale events");

    // Match the real 32-bit record/list layout, including its separate vector.
    static_assert(sizeof(void*)==4,"Native history fixture requires x86");
    alignas(4) unsigned char head[8]{}, node[0x28]{}, payload[108]{};
    auto set=[](unsigned char* p,auto value){std::memcpy(p,&value,sizeof(value));};
    set(head,node);set(node,head);set(node+4,head);set(node+8,std::uint32_t(123));
    set(node+0x14,payload);set(node+0x18,payload+108);set(payload,std::uint32_t(456));
    auto readable=[&](const void* p,size_t n) {
        auto a=reinterpret_cast<std::uintptr_t>(p);
        for (auto block:{std::pair<const void*,size_t>{head,sizeof(head)},{node,sizeof(node)},{payload,sizeof(payload)}})
        {auto b=reinterpret_cast<std::uintptr_t>(block.first);if(a>=b && n<=block.second && a-b<=block.second-n)return true;}
        return false;
    };
    std::vector<CoopHistory::Event> events;
    require(CoopHistory::ReadList(head,events,readable) && events.size()==1 && events[0][0]==123 && events[0][3]==456,
        "Read actual native list and vector offsets without transmitting pointers");
    set(node,node);require(!CoopHistory::ReadList(head,events,readable),"Cyclic list is bounded");
    set(node,head);set(node+0x18,payload+104);
    require(!CoopHistory::ReadList(head,events,readable),"Unexpected native event size rejected");
}

inline int TestCampaignAbi(std::uintptr_t base)
{
    int checks=0;
    auto require=[&](bool ok){if(!ok)throw std::runtime_error("Native campaign/history ABI mismatch");++checks;};
    using namespace CoopEngine;
#define VERIFY_CAMPAIGN(name) require(MatchesMotionCode(reinterpret_cast<unsigned char*>(base+k##name##Rva),k##name##Size,base,k##name##Size,k##name##Hash,k##name##Relocations))
    VERIFY_CAMPAIGN(SavePrepare); VERIFY_CAMPAIGN(SaveWorld); VERIFY_CAMPAIGN(SaveCleanup);
    VERIFY_CAMPAIGN(ModelAttachments);
    VERIFY_CAMPAIGN(TimelineShow); VERIFY_CAMPAIGN(HistoryCreate); VERIFY_CAMPAIGN(HistoryGet); VERIFY_CAMPAIGN(HistoryEvent);
#undef VERIFY_CAMPAIGN
    require(MatchesStaticCode(reinterpret_cast<unsigned char*>(base+0x3ebc30),0x35,0x35,0x8bd1df60));
    require(MatchesStaticCode(reinterpret_cast<unsigned char*>(base+0x3eb8f0),0x4b,0x4b,0x46b40d3c));
    return checks;
}
