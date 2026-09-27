// SPDX-License-Identifier: GPL-3.0-or-later
// Included by Probe.cpp after the body/replica helpers.
using ResetCellWorldFunction=void(__cdecl*)(int,int,int,int,bool,int,bool);
ResetCellWorldFunction gResetCellWorldOriginal=nullptr;
bool gCellLifecycleHook=false;
struct CellResetScope {
    bool previous=gResettingCellWorld;
    CellResetScope(){gResettingCellWorld=true;}
    ~CellResetScope(){gResettingCellWorld=previous;}
};
std::uint64_t gRestoredProgressWorld=0;
struct AvatarDeathPose {
    bool pending=false;
    int index=-1;
    Math::Vector3 position;
    float scale=0, target=0;
} gAvatarDeath;

void FinishRestoredEntry()
{
    using Finish=void(__cdecl*)();
    static auto finish=reinterpret_cast<Finish>(VerifiedMotionCode(CoopEngine::kFinishCinematicRva,
        CoopEngine::kFinishCinematicSize,CoopEngine::kFinishCinematicHash,CoopEngine::kFinishCinematicRelocations));
    // Same completion path used by the engine's skip-cinematic command.
    // End the restored hatch/egg sequence and release its input/camera state.
    if (finish) finish();
}

void RememberAvatarDeath(Simulator::Cell::cCellObjectData* cell,bool accepted)
{
    if (!accepted) {
        // The native death animation changes size before the reset callback.
        if (!cell->field_112 && !cell->field_113) {
            gAvatarDeath={false,cell->Index(),cell->GetPosition(),cell->mTransform.GetScale(),cell->mTargetSize};
        }
    } else if (gAvatarDeath.index==cell->Index()) gAvatarDeath.pending=true;
}

bool RestoreInvitedCellProgress(const CoopNet::Snapshot& state)
{
    if (gRestoredProgressWorld==state.worldGeneration) return true;
    auto game=Simulator::Cell::cCellGame::Get();
    if (!game || !game->mpSerializableData || !gResetCellWorldOriginal || !state.progressInitialized) return false;
    // Accepting from an already open stage also needs a silent native bootstrap.
    RemoveRemoteCell("Restoring invited campaign progress");
    RemoveHostAppearanceProxy("Restoring invited campaign progress");
    RemoveMirroredNpcs("Restoring invited campaign progress");
    const auto& p=state.progress;
    ApplyCellProgress(p,true);
    bool mode=false; int world=0;
    std::memcpy(&mode,reinterpret_cast<unsigned char*>(game)+0x410c,1);
    std::memcpy(&world,reinterpret_cast<unsigned char*>(game)+0x4110,4);
    CellResetScope resetting;
    gResetCellWorldOriginal(p.food,p.plantFood,p.overPlantFood,p.overAnimalFood,mode,world,false);
    FinishRestoredEntry();
    ApplyCellProgress(p,true);
    gWorldCoordinates=CoopWorld::Coordinates{};
    gJoinedPlayerPlaced=false;
    gRestoredProgressWorld=state.worldGeneration;
    WriteProbeLog("Invited campaign restored directly at shared growth; old part notifications suppressed.");
    return GetLocalPlayerCell()!=nullptr;
}

bool RespawnCooperativeAvatar(const CoopNet::Snapshot& state)
{
    auto game=Simulator::Cell::cCellGame::Get();
    auto player=GetLocalPlayerCell();
    if (!game || !player || !game->mpSerializableData || !gWorldRemoveOriginal ||
        !GetCellMotionFunctions().Ready()) return false;
    // The pending-reset loop also handles non-death requests (E50920/E72200).
    // Those must still run the original full reset.
    if (!gAvatarDeath.pending && !player->field_112 && !player->field_113) return false;
    using Spawn=void(__cdecl*)(const Math::Vector3*,float);
    using Build=void(__cdecl*)(int,int);
    static auto spawn=reinterpret_cast<Spawn>(VerifiedMotionCode(CoopEngine::kSpawnAvatarRva,
        CoopEngine::kSpawnAvatarSize,CoopEngine::kSpawnAvatarHash,CoopEngine::kSpawnAvatarRelocations));
    static auto build=reinterpret_cast<Build>(VerifiedMotionCode(CoopEngine::kBuildCellBodyRva,
        CoopEngine::kBuildCellBodySize,CoopEngine::kBuildCellBodyHash,CoopEngine::kBuildCellBodyRelocations));
    static auto hatch=VerifiedMotionCode(CoopEngine::kHatchAvatarRva,CoopEngine::kHatchAvatarSize,
        CoopEngine::kHatchAvatarHash,CoopEngine::kHatchAvatarRelocations);
    if (!spawn || !build || !hatch || !LoadCellCreature(game->mpSerializableData->mPlayerCreatureKey)) return false;
    const bool remembered=gAvatarDeath.index==player->Index() && gAvatarDeath.scale>0;
    auto position=remembered ? gAvatarDeath.position : player->GetPosition();
    const float scale=remembered ? gAvatarDeath.scale : player->mTransform.GetScale();
    const float target=remembered ? gAvatarDeath.target : player->mTargetSize;
    if (!std::isfinite(scale) || scale<=0) return false;
    if (state.hasRemotePosition && GetTickCount64()-state.remotePositionReceivedTick<2000) {
        const auto point=gWorldCoordinates.Decode({state.remoteX,state.remoteY,state.remoteZ});
        position=Math::Vector3(float(point.x)+std::max(2.0f,scale*4),float(point.y),float(point.z));
    }
    RemoveHostAppearanceProxy("Local avatar respawn.");
    // The native death callback has completed. Delete only that avatar; never
    // clear the cell/action/spawner pools as E7FD00 does in single player.
    gWorldRemoveOriginal(player->Index(),false,0,true);
    game->field_51D8 &= ~0xffff;
    // SpawnAvatar takes the world-size factor, NOT the actor transform scale.
    // Passing the latter made respawns 1/150 or 1/500 of their proper size.
    spawn(&position,game->field_514C*2.0f);
    player=GetLocalPlayerCell();
    if (!player) {
        WriteProbeLog("Cooperative avatar spawn failed; falling back to the native world reset.");
        return false;
    }
    player->mTransform.SetScale(scale);
    player->mTargetSize=target;
    build(player->Index(),0);
    MoveCellBody(player,position,player->mTransform.GetRotation().ToQuaternion());
    CoopEngine::HatchAvatar(hatch,player);
    gJoinedPlayerPlaced=true;
    gLastSubmittedAppearanceKey=ResourceKey{};
    gAvatarDeath=AvatarDeathPose{};
    WriteProbeLog("Cooperative respawn: replaced only the local avatar; NPCs, loot and world coordinates retained.");
    return true;
}

void __cdecl ResetCellWorldHook(int food,int plants,int overPlants,int overMeat,bool mode,int world,bool first)
{
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    const auto base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    const auto state=CoopNet::GetSnapshot();
    // E8088C is the pending-death reset. Loading, editor return and new games
    // call the same function and must retain their complete native reset.
    if (caller==base+0xa80891 && state.connected && state.inviteAccepted &&
        !state.editorOpen && Simulator::IsCellGame() && RespawnCooperativeAvatar(state)) return;
    const bool invited=state.connected && state.inviteAccepted && state.progressInitialized &&
        !state.editorOpen && !CoopSession::IsWorldOwner(state,CoopNet::GetRole());
    if (invited) {
        const auto& p=state.progress;
        ApplyCellProgress(p,true);
        food=p.food; plants=p.plantFood; overPlants=p.overPlantFood; overMeat=p.overAnimalFood;
        first=false;
    }
    CellResetScope resetting;
    gResetCellWorldOriginal(food,plants,overPlants,overMeat,mode,world,first);
    if (invited) {
        FinishRestoredEntry();
        ApplyCellProgress(state.progress,true);
        gRestoredProgressWorld=state.worldGeneration;
        WriteProbeLog("Native invited load uses shared growth immediately; historical unlocks restored silently.");
    }
    gAvatarDeath=AvatarDeathPose{};
    gWorldCoordinates=CoopWorld::Coordinates{};
    gJoinedPlayerPlaced=false;
}
