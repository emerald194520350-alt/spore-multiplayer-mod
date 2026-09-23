#pragma once
#include "../native/WorldCoordinates.h"
#include "../native/EditorSync.h"
#include "../native/ProgressSync.h"
#include "../native/NpcMotion.h"
#include "../native/EditorBudget.h"
#include "../native/CellAnimationSync.h"
#include <memory>
#include <stdexcept>

inline bool TestSharedWorld()
{
    auto require=[](bool ok,const char* why){if(!ok)throw std::runtime_error(why);};
    CoopWorld::Coordinates host,guest;
    const CoopWorld::Point h{100,100,0},g{105,102,0},food{104,103,0};
    require(host.Rebase(h,{0,0,0},2),"Host growth frame");
    require(guest.Rebase(g,{0,0,0},2),"Guest growth frame");
    auto atHost=host.Decode(guest.Encode({0,0,0}));
    require(std::abs(atHost.x-2.5)<1e-6 && std::abs(atHost.y-1)<1e-6,"Growth must preserve player separation across distinct cameras");
    auto foodHost=host.Decode(food), foodGuest=guest.Decode(food);
    require(std::abs(foodHost.x-2)<1e-6 && std::abs(foodGuest.x+0.5)<1e-6,"Food must use the same fixed world point");
    for(int i=0;i<1000;++i)
    {
        const auto before=guest.Decode(g);
        require(guest.Rebase(before,{0.1,-0.2,0},1.001),"Repeated camera rebase");
        const auto result=guest.Encode({0.1,-0.2,0});
        require(std::abs(result.x-g.x)<1e-8 && std::abs(result.y-g.y)<1e-8,"Repeated growth must not drift players");
    }
    require(!guest.Rebase(g,g,0),"Invalid growth rejected");
    CoopProgress::Reconciler a,b;
    CoopNet::CellProgress seed; seed.missions[1]=3; a.Initialize(seed); b.Initialize(seed);
    auto av=seed,bv=seed; av.missions[1]++; bv.missions[1]+=2;
    CoopProgress::Event ae,be;
    require(a.Capture(av,ae)&&b.Capture(bv,be)&&ae.delta.missions[1]==1&&be.delta.missions[1]==2,"Shared quest gains are increments");
    auto server=seed;server.missions[1]++;
    require(b.Reconcile(server,0).missions[1]==6,"Concurrent quest gains survive older snapshots");
    server.missions[1]=6;
    auto confirmed=b.Reconcile(server,be.sequence);
    require(confirmed.missions[1]==6&&!b.Capture(confirmed,be),"Quest acknowledgements never echo");
    using namespace CoopEditor;
    int budget=0;
    require(MergeBudget(16,16,6,false,budget) && budget==6,"Peer receives the exact native balance: 16 becomes 6");
    require(MergeBudget(6,6,6,false,budget) && budget==6,"Repeated model/budget snapshots never double-charge");
    require(MergeBudget(6,6,16,false,budget) && budget==16,"Undo and deletion restore the transmitted balance");
    require(MergeBudget(36,26,26,true,budget) && budget==16,"Concurrent native purchases retain both ten-DNA charges");
    require(MergeBudget(16,6,6,false,budget) && budget==6,"An identical merged edit is not charged twice");
    require(!MergeBudget(16,6,6,true,budget),"Unfunded concurrent edits cannot produce a negative balance");
    require(!MergeBudget(-1,6,6,true,budget) && !MergeBudget(0,100000000,100000000,true,budget),"Invalid and overflowing budget rebase rejected");
    std::uint32_t animation=0;
    require(CoopVisual::MouthAnimationId(79)==0xAAAA0075 &&
        CoopVisual::MouthAnimationId(0xAAAA0075)==0xAAAA0075 &&
        CoopVisual::MouthAnimationId(13)==0xAAAA0007 &&
        CoopVisual::MouthAnimationId(UINT32_MAX)==0,
        "Mouth replication accepts actual visual TLSA groups as well as native indices, and rejects other animations");
    require(CoopVisual::MouthTransition(79,0,animation) && animation==79 &&
        !CoopVisual::MouthTransition(79,79,animation), "Chewing starts once, without restarting each graphics frame");
    require(CoopVisual::MouthTransition(4,79,animation) && animation==0,
        "Mouth returns to idle when the player finishes eating");
    require(!CoopVisual::MouthTransition(UINT32_MAX,0,animation) &&
        !CoopVisual::MouthTransition(9,0,animation), "Invalid and death animations are not replayed");
    require(HistorySlot(1,1)==0 && HistorySlot(2,2)==1,
        "Native editor history cursor is one-based after initial load and part placement");
    require(HistorySlot(2,3)==1 && HistorySlot(3,3)==2,
        "Undo selects the current committed model instead of a future redo entry");
    require(HistorySlot(0,1)==-1 && HistorySlot(-1,1)==-1 && HistorySlot(4,3)==-1,
        "Unavailable editor history cannot be indexed");
    Bytes base(Header+Block,0); SetWord(base,0,0x31504353);SetWord(base,4,1);
    SetWord(base,Header,123);SetWord(base,Header+4,456);
    auto local=base,remote=base; SetWord(local,Header+16,100);SetWord(remote,8,200);
    Bytes merged;
    require(Merge(base,local,remote,merged)&&Word(merged,Header+16)==100&&Word(merged,8)==200,"Peer paint and local part edits must both survive");
    local=base;remote=base;
    local.resize(Header+2*Block);remote.resize(Header+2*Block);
    SetWord(local,4,2);SetWord(remote,4,2);SetWord(local,Header+Block+4,700);SetWord(remote,Header+Block+4,800);
    SetWord(local,Header+Block+8,0);SetWord(local,Header+Block+12,1);
    require(Merge(base,local,remote,merged)&&Word(merged,4)==3&&Word(merged,Header+2*Block+4)==700&&
        Word(merged,Header+2*Block+12)==2,"Concurrent appended mouths keep both branches and remap symmetry");
    local.pop_back();require(!Merge(base,local,remote,merged),"Truncated editor resources rejected");
    auto two=remote;
    SetWord(two,Header+Block+20,10); // Distinct position for the second part.
    SetWord(two,Header+8,UINT32_MAX); SetWord(two,Header+12,UINT32_MAX);
    auto deleted=two;deleted.resize(Header+Block);SetWord(deleted,4,1);
    auto painted=two;SetWord(painted,40,123);
    require(Merge(two,deleted,painted,merged)&&Word(merged,4)==1&&Word(merged,40)==123,
        "Deleting a part preserves the peer's simultaneous paint edit");
    CoopProgress::Reconciler guestBudget;guestBudget.Initialize(seed);
    auto spent=seed;spent.spent=5;CoopProgress::Event cost;
    require(!guestBudget.Capture(spent,cost,false),"Saving the same shared species on the guest must not charge twice");
    auto replayed=seed;replayed.food=20;replayed.missions[5]=1;replayed.unlocks[4]=2;
    replayed.partCinematicPlayed=true;
    guestBudget.ObserveApplied(replayed);
    require(!guestBudget.Capture(replayed,cost),"Native growth/unlock presentation must not echo as a second pickup");

    CoopWorld::NpcMotion motion;
    motion.Push(0,1000,{{0,0,0},0,1},1);
    motion.Push(1,1100,{{10,0,0},0,-1},1);
    motion.Push(1,1190,{{100,0,0},0,1},1); // Same packet on another render frame.
    auto pose=motion.Sample(1175);
    require(std::abs(pose.position.x-5)<1e-6 && std::abs(pose.qw-1)<1e-6,
        "NPC movement interpolates between packets and equivalent quaternion signs never flip the body");
    require(motion.Sample(5000).position.x==10,"A stalled NPC stream must not extrapolate through obstacles");
    CoopWorld::Coordinates camera;camera.Rebase({0,0,0},{20,0,0},2);
    require(std::abs(camera.Decode(pose.position).x-22.5)<1e-6,
        "NPC interpolation history survives growth in fixed world coordinates");
    motion.Push(2,1200,{{500,0,0},0,1},1);
    require(motion.Sample(1200).position.x==500,"NPC teleports reset history instead of sweeping across the world");
    motion.Push(3,3000,{{510,0,0},0,0},1);
    require(motion.Sample(3000).position.x==510 && motion.Sample(3000).qw==1,
        "Long gaps reset interpolation and zero rotations remain finite");
    CoopWorld::NpcMotion playerMotion;
    for (int packet=0; packet<20; ++packet)
        playerMotion.Push(packet,1000+packet*50,{{double(packet),0,0},0,1,1,0},1);
    double previousX=-1;
    for (int frame=0; frame<10; ++frame)
    {
        auto p=playerMotion.Sample(1880+frame*5,75);
        require(std::abs(p.position.x-(1880+frame*5-75-1000)/50.0)<1e-6 && p.position.x>previousX,
            "Player poses advance every rendered frame between 20Hz network samples");
        require(std::abs(p.qx-0.70710678f)<1e-6 && std::abs(p.qw-0.70710678f)<1e-6,
            "Player smoothing preserves all four quaternion components");
        previousX=p.position.x;
    }
    require(playerMotion.Sample(6000,75).position.x==19,"A disconnected player cannot extrapolate away");
    return true;
}
