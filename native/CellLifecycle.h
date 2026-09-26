// SPDX-License-Identifier: GPL-3.0-or-later
// Included by Probe.cpp after the body/replica helpers.
using ResetCellWorldFunction=void(__cdecl*)(int,int,int,int,bool,int,bool);
ResetCellWorldFunction gResetCellWorldOriginal=nullptr;
bool gCellLifecycleHook=false;

bool RespawnCooperativeAvatar(const CoopNet::Snapshot& state)
{
    auto game=Simulator::Cell::cCellGame::Get();
    auto player=GetLocalPlayerCell();
    if (!game || !player || !game->mpSerializableData || !gWorldRemoveOriginal ||
        !GetCellMotionFunctions().Ready()) return false;
    // The pending-reset loop also handles non-death requests (E50920/E72200).
    // Those must still run the original full reset.
    if (!player->field_112 && !player->field_113) return false;
    using Spawn=void(__cdecl*)(const Math::Vector3*,float);
    using Build=void(__cdecl*)(int,int);
    static auto spawn=reinterpret_cast<Spawn>(VerifiedMotionCode(CoopEngine::kSpawnAvatarRva,
        CoopEngine::kSpawnAvatarSize,CoopEngine::kSpawnAvatarHash,CoopEngine::kSpawnAvatarRelocations));
    static auto build=reinterpret_cast<Build>(VerifiedMotionCode(CoopEngine::kBuildCellBodyRva,
        CoopEngine::kBuildCellBodySize,CoopEngine::kBuildCellBodyHash,CoopEngine::kBuildCellBodyRelocations));
    static auto hatch=VerifiedMotionCode(CoopEngine::kHatchAvatarRva,CoopEngine::kHatchAvatarSize,
        CoopEngine::kHatchAvatarHash,CoopEngine::kHatchAvatarRelocations);
    if (!spawn || !build || !hatch || !LoadCellCreature(game->mpSerializableData->mPlayerCreatureKey)) return false;
    auto position=player->GetPosition();
    const float scale=player->mTransform.GetScale();
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
    spawn(&position,scale);
    player=GetLocalPlayerCell();
    if (!player) {
        WriteProbeLog("Cooperative avatar spawn failed; falling back to the native world reset.");
        return false;
    }
    build(player->Index(),0);
    MoveCellBody(player,position,player->mTransform.GetRotation().ToQuaternion());
    CoopEngine::HatchAvatar(hatch,player);
    gJoinedPlayerPlaced=true;
    gLastSubmittedAppearanceKey=ResourceKey{};
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
    gResetCellWorldOriginal(food,plants,overPlants,overMeat,mode,world,first);
    gWorldCoordinates=CoopWorld::Coordinates{};
    gJoinedPlayerPlaced=false;
}
