#pragma once
#include "../native/CellMotionAbi.h"
#include "../native/CellGrowthAbi.h"
#include "../native/CellProgressAbi.h"
#include "../native/CellAnimationAbi.h"
#include <array>
#include <cmath>
#include "engine-replica.test.h"
#include "peer-indicator.test.h"
#include "engine-editor.test.h"
#include "campaign.test.h"
#include "engine-cell-body.test.h"

// Execute only the verified, self-contained movement routines against fixture
// memory. DONT_RESOLVE_DLL_REFERENCES never calls the game's entry point.
inline int TestEngineMotion(const char* executable)
{
    const auto module = LoadLibraryExA(executable, nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!module) throw std::runtime_error("Cannot map game image for movement tests");
    struct Unload { HMODULE module; ~Unload() { FreeLibrary(module); } } unload{module};
    const auto base = reinterpret_cast<std::uintptr_t>(module);
    auto code = [base](std::uintptr_t rva) { return reinterpret_cast<unsigned char*>(base+rva); };
    auto require = [](bool value, const char* message) { if (!value) throw std::runtime_error(message); };
    require(CoopEngine::MatchesMotionCode(code(CoopEngine::kPositionRva), CoopEngine::kPositionSize,
        base, CoopEngine::kPositionSize, CoopEngine::kPositionHash, CoopEngine::kPositionRelocations), "Position ABI mismatch");
    require(CoopEngine::MatchesMotionCode(code(CoopEngine::kOrientationRva), CoopEngine::kOrientationSize,
        base, CoopEngine::kOrientationSize, CoopEngine::kOrientationHash, CoopEngine::kOrientationRelocations), "Orientation ABI mismatch");
    require(CoopEngine::MatchesMotionCode(code(CoopEngine::kGraphicsRva), CoopEngine::kGraphicsSize,
        base, CoopEngine::kGraphicsSize, CoopEngine::kGraphicsHash, CoopEngine::kGraphicsRelocations), "Graphics ABI mismatch");
    std::array<unsigned char, CoopEngine::kPositionSize> changed;
    std::memcpy(changed.data(), code(CoopEngine::kPositionRva), changed.size());
    changed[0x100] ^= 1;
    require(!CoopEngine::MatchesMotionCode(changed.data(), changed.size(), base,
        CoopEngine::kPositionSize, CoopEngine::kPositionHash, CoopEngine::kPositionRelocations), "Changed engine code accepted");
    require(!CoopEngine::MatchesMotionCode(code(CoopEngine::kPositionRva), CoopEngine::kPositionSize-1, base,
        CoopEngine::kPositionSize, CoopEngine::kPositionHash, CoopEngine::kPositionRelocations), "Truncated engine code accepted");

    struct Vec { float x, y, z; };
    struct Quat { float x, y, z, w; };
    struct Physics { int count; Vec position[1500]; Vec velocity[1500]; } physics{};
    static_assert(offsetof(Physics, velocity) == 0x4654, "Native node array layout");
    alignas(4) std::array<unsigned char, 0x51e4> game{};
    alignas(4) std::array<unsigned char, 0x398> cell{};
    auto write = [](auto& bytes, std::size_t offset, const auto& value) {
        std::memcpy(bytes.data()+offset, &value, sizeof(value));
    };
    auto* physicsPointer = &physics;
    write(game, 0x4108, physicsPointer);
    auto** gameGlobal = reinterpret_cast<void**>(base + 0x12b3c04);
    auto* oldGame = *gameGlobal;
    *gameGlobal = game.data();
    struct Restore { void** slot; void* value; ~Restore() { *slot = value; } } restore{gameGlobal, oldGame};
    using SetPosition = void(__cdecl*)(void*, const Vec*);
    using SetOrientation = void(__cdecl*)(void*, const Quat*);
    auto move = reinterpret_cast<SetPosition>(code(CoopEngine::kPositionRva));
    auto rotate = reinterpret_cast<SetOrientation>(code(CoopEngine::kOrientationRva));
    const std::uint16_t flags = 7, revision = 1;
    write(cell, 0x48, flags); write(cell, 0x4a, revision);
    const float scale = 0.55f, identity[9] = {1,0,0,0,1,0,0,0,1};
    write(cell, 0x58, scale); write(cell, 0x5c, identity);
    const Vec start{100,100,0}; write(cell,0x4c,start);
    const int gfx = 123, count = 4, indices[4] = {0, 1, 1498, 1499};
    write(cell,0x248,gfx); write(cell,0x250,count); write(cell,0x254,indices);
    physics.position[0] = {99,99,0}; physics.position[1] = {101,99,0};
    physics.position[1498] = {99,101,0}; physics.position[1499] = {101,101,0};
    physics.position[42] = {-123,456,7};
    auto center = [&]() { Vec v{}; for(int id:indices) {v.x+=physics.position[id].x/4;v.y+=physics.position[id].y/4;v.z+=physics.position[id].z/4;} return v; };
    auto closeEnough=[](float a,float b) {return std::abs(a-b)<0.002f;};
    require(CoopEngine::MatchesMotionCode(code(CoopEngine::kGrowthRva),CoopEngine::kGrowthSize,base,
        CoopEngine::kGrowthSize,CoopEngine::kGrowthHash,CoopEngine::kGrowthRelocations),"Growth world rebase ABI mismatch");
    require(CoopEngine::MatchesMotionCode(code(CoopEngine::kAddFoodRva),CoopEngine::kAddFoodSize,base,
        CoopEngine::kAddFoodSize,CoopEngine::kAddFoodHash,CoopEngine::kAddFoodRelocations),"Native food/growth ABI mismatch");
    require(CoopEngine::MatchesMotionCode(code(CoopEngine::kUnlockPartRva),CoopEngine::kUnlockPartSize,base,
        CoopEngine::kUnlockPartSize,CoopEngine::kUnlockPartHash,CoopEngine::kUnlockPartRelocations),"Native unlock/quest ABI mismatch");
    require(CoopEngine::MatchesMotionCode(code(CoopEngine::kEnterCellEditorRva),CoopEngine::kEnterCellEditorSize,base,
        CoopEngine::kEnterCellEditorSize,CoopEngine::kEnterCellEditorHash,CoopEngine::kEnterCellEditorRelocations),"Cell campaign editor ABI mismatch");
    require(CoopEngine::MatchesMotionCode(code(CoopEngine::kPartCinematicRva),CoopEngine::kPartCinematicSize,base,
        CoopEngine::kPartCinematicSize,CoopEngine::kPartCinematicHash,CoopEngine::kPartCinematicRelocations),"First-part cinematic ABI mismatch");
    require(CoopEngine::MatchesMotionCode(code(CoopEngine::kPlayCellAnimationRva),CoopEngine::kPlayCellAnimationSize,base,
        CoopEngine::kPlayCellAnimationSize,CoopEngine::kPlayCellAnimationHash,CoopEngine::kPlayCellAnimationRelocations),"Mouth animation transition ABI mismatch");
    int checks=11;
    // Reproduce the old bug: changing the transform leaves the physics center
    // behind; the next simulation frame restores that old center.
    const Vec target{25,-40,0}; write(cell,0x4c,target);
    require(closeEnough(center().x,100) && closeEnough(center().y,100), "Old transform-only bug was not reproduced"); ++checks;
    write(cell,0x4c,start);
    move(cell.data(), &target);
    require(closeEnough(center().x,target.x) && closeEnough(center().y,target.y), "Native teleport must move the body center"); ++checks;
    require(closeEnough(physics.position[0].x,24) && closeEnough(physics.position[0].y,-41), "Teleport must preserve body shape and scale"); ++checks;
    const Quat turn{0,0,0.70710678f,0.70710678f};
    rotate(cell.data(), &turn);
    require(closeEnough(center().x,target.x) && closeEnough(center().y,target.y), "Rotation must preserve the body center"); ++checks;
    require(closeEnough(physics.position[0].x,26) && closeEnough(physics.position[0].y,-41), "Native rotation must rotate body nodes"); ++checks;
    for(int frame=0;frame<1000;++frame) {
        const Vec next{25+frame*.25f,-40+std::sin(frame*.05f)*10,0};
        move(cell.data(), &next);
        const Vec simulated=center();
        write(cell,0x4c,simulated); // the same node-average feedback used by SPORE
        require(closeEnough(simulated.x,next.x) && closeEnough(simulated.y,next.y), "Repeated network movement drifted back to the spawn");
    }
    ++checks;
    require(physics.position[42].x == -123 && physics.position[42].y == 456, "Movement changed another cell's node"); ++checks;
    // The single-node/no-renderer branch must also keep ordinary cells movable.
    const int one=1, zero=0; write(cell,0x250,one); write(cell,0x248,zero);
    move(cell.data(), &start);
    Vec result; std::memcpy(&result,cell.data()+0x4c,sizeof(result));
    require(closeEnough(result.x,100) && closeEnough(result.y,100), "Simple cell teleport failed"); ++checks;
    return checks + TestEngineReplicaCollision(base) + TestPeerProjectionAgainstEngine(base) + TestEngineEditorDispatch(base) + TestCampaignAbi(base) + TestEngineCellBody(base);
}
