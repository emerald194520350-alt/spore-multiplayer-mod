#pragma once
#include "../native/CellReplicaAbi.h"
#include <array>

// Run actual engine broadphase insertion/removal on private fixture memory.
// No game startup, allocator, graphics, or live process is involved.
inline int TestEngineReplicaCollision(std::uintptr_t base)
{
    auto code = [base](std::uintptr_t rva) { return reinterpret_cast<unsigned char*>(base + rva); };
    int checks = 0;
    auto require = [&checks](bool ok, const char* message) {
        if (!ok) throw std::runtime_error(message);
        ++checks;
    };
    require(CoopEngine::MatchesStaticCode(code(CoopEngine::kUnregisterCollisionRva),
        CoopEngine::kUnregisterCollisionSize, CoopEngine::kUnregisterCollisionSize,
        CoopEngine::kUnregisterCollisionHash), "Collision removal ABI mismatch");
    require(CoopEngine::MatchesStaticCode(code(0x7bb290), 0xef, 0xef, 0xb01bc54au),
        "Collision insertion fixture ABI mismatch");
    std::array<unsigned char, CoopEngine::kUnregisterCollisionSize> changed;
    std::memcpy(changed.data(), code(CoopEngine::kUnregisterCollisionRva), changed.size());
    changed[0x30] ^= 1;
    require(!CoopEngine::MatchesStaticCode(changed.data(), changed.size(), changed.size(),
        CoopEngine::kUnregisterCollisionHash), "Modified collision code accepted");
    require(!CoopEngine::MatchesStaticCode(changed.data(), changed.size() - 1, changed.size(),
        CoopEngine::kUnregisterCollisionHash), "Truncated collision code accepted");

    struct Endpoint { std::int16_t kind; std::uint16_t object; float position; };
    struct Entry { int object, group; std::uint16_t minX, minY, maxX, maxY; };
    struct Broadphase {
        int unused[4], count;
        Entry* entries;
        int allocated, nextFree, activeEndpoints;
        Endpoint *activeX, *activeY;
        int pendingEndpoints;
        Endpoint *pendingX, *pendingY;
    };
    static_assert(sizeof(Broadphase) == 0x38 && sizeof(Entry) == 16 && sizeof(Endpoint) == 8,
        "Native broadphase fixture layout");
    using Insert = int(__cdecl*)(void*, int, const float*, const float*, int);
    using Remove = void(__cdecl*)(void*, int);
    auto insert = reinterpret_cast<Insert>(code(0x7bb290));
    auto remove = reinterpret_cast<Remove>(code(CoopEngine::kUnregisterCollisionRva));
    const float min[2] = {99,99}, max[2] = {101,101};
    for (bool active : {false, true})
    {
        std::array<Entry, 4> entries{};
        std::array<Endpoint, 8> pendingX{}, pendingY{}, activeX{}, activeY{};
        Broadphase world{};
        world.entries = entries.data(); world.nextFree = -1;
        world.activeX = activeX.data(); world.activeY = activeY.data();
        world.pendingX = pendingX.data(); world.pendingY = pendingY.data();
        const int avatar = insert(&world, 123, min, max, 0);
        const int proxy = insert(&world, 456, min, max, 0);
        require(avatar == 0 && proxy == 1 && world.count == 2,
            "Overlapping avatar/proxy colliders were not registered");
        if (active)
        {
            // Exercise both native endpoint storage branches after promotion.
            activeX = pendingX; activeY = pendingY;
            world.activeEndpoints = world.pendingEndpoints;
            world.pendingEndpoints = 0;
            for (int i = 0; i < 2; ++i) {
                entries[i].minX &= 0x7fff; entries[i].maxX &= 0x7fff;
                entries[i].minY &= 0x7fff; entries[i].maxY &= 0x7fff;
            }
        }
        const auto savedAvatar = entries[avatar];
        remove(&world, proxy);
        auto& x = active ? activeX : pendingX;
        auto& y = active ? activeY : pendingY;
        require(world.count == 1 && entries[proxy].group == -1 && world.nextFree == proxy,
            "Replica collision entry was not released");
        require(x[2].kind == -2 && x[3].kind == -2 && y[2].kind == -2 && y[3].kind == -2,
            "Replica endpoints remain in collision broadphase");
        require(std::memcmp(&entries[avatar], &savedAvatar, sizeof(Entry)) == 0 &&
            x[0].kind == 0 && x[1].kind == 1 && y[0].kind == 0 && y[1].kind == 1,
            "Removing proxy changed the real avatar collider");
        require(insert(&world, 789, min, max, 0) == proxy && world.count == 2,
            "Unregistered collider slot cannot be reused");
    }
    return checks;
}
