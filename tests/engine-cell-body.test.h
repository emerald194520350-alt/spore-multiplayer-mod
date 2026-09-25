#pragma once
#include "../native/CellBodyAbi.h"
#include <array>

namespace CellBodyFixture
{
    inline void* releasedVisual = nullptr;
    inline bool immediateRemoval = false;
    inline std::array<int, 4> releasedNodes{};
    inline int releasedCount = 0;
    inline void __fastcall ReleaseVisual(void* visual, void*, bool immediate)
    { releasedVisual = visual; immediateRemoval = immediate; }
    inline void __cdecl ReleaseNode(void*, int index)
    { if (releasedCount < 4) releasedNodes[releasedCount++] = index; }
    struct Key { unsigned instance, type, group; };
    inline std::array<unsigned char, 0x1250> originalStats{}, sharedStats{};
    inline void* __cdecl ModelStats(Key key)
    { return key.instance == 222 ? sharedStats.data() : originalStats.data(); }

    // Patches only our private, uninitialized image; never a running game.
    struct Patch
    {
        unsigned char* address;
        DWORD protection = 0;
        std::array<unsigned char, 5> original{};
        Patch(unsigned char* where, void* replacement) : address(where)
        {
            if (!VirtualProtect(address, 5, PAGE_EXECUTE_READWRITE, &protection))
                throw std::runtime_error("Cannot patch private body fixture");
            std::memcpy(original.data(), address, 5);
            const auto offset = static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(replacement)
                - reinterpret_cast<std::uintptr_t>(address + 5));
            address[0] = 0xe9; std::memcpy(address + 1, &offset, 4);
            FlushInstructionCache(GetCurrentProcess(), address, 5);
        }
        ~Patch()
        {
            std::memcpy(address, original.data(), 5);
            DWORD ignored; VirtualProtect(address, 5, protection, &ignored);
            FlushInstructionCache(GetCurrentProcess(), address, 5);
        }
    };
}

inline int TestEngineCellBody(std::uintptr_t base)
{
    using namespace CellBodyFixture;
    int checks = 0;
    auto require = [&](bool ok, const char* message) {
        if (!ok) throw std::runtime_error(message);
        ++checks;
    };
    auto code = [base](std::uintptr_t rva) { return reinterpret_cast<unsigned char*>(base + rva); };
    require(CoopEngine::MatchesMotionCode(code(CoopEngine::kReleaseCellBodyRva),
        CoopEngine::kReleaseCellBodySize, base, CoopEngine::kReleaseCellBodySize,
        CoopEngine::kReleaseCellBodyHash, CoopEngine::kReleaseCellBodyRelocations), "Body release ABI mismatch");
    require(CoopEngine::MatchesMotionCode(code(CoopEngine::kBuildCellBodyRva),
        CoopEngine::kBuildCellBodySize, base, CoopEngine::kBuildCellBodySize,
        CoopEngine::kBuildCellBodyHash, CoopEngine::kBuildCellBodyRelocations), "Body rebuild ABI mismatch");
    constexpr std::size_t abilityRelocations[] = {1};
    require(CoopEngine::MatchesMotionCode(code(0xa67a60), 0x37, base, 0x37,
        0x2d0d169bu, abilityRelocations), "First native mouth check ABI mismatch");
    require(CoopEngine::MatchesMotionCode(code(0xa67aa0), 0x37, base, 0x37,
        0x0ba4bce7u, abilityRelocations), "Second native mouth check ABI mismatch");

    alignas(4) std::array<unsigned char, 0x51e4> game{};
    alignas(4) std::array<unsigned char, 0x200> gfx{};
    alignas(4) std::array<unsigned char, 0x398> cell{};
    alignas(4) std::array<unsigned char, 0x100> visual{}, campaign{};
    auto write = [](auto& bytes, std::size_t offset, const auto& value) {
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    };
    auto integer = [](const auto& bytes, std::size_t offset) {
        int value; std::memcpy(&value, bytes.data() + offset, 4); return value;
    };
    struct Pool { void* data; int next, identifier, count, allocated, stride, unused; };
    write(game, 0x1c, Pool{cell.data(), -1, 0x12340000, 1, 1, 0x398, 0});
    write(gfx, 0x168, Pool{visual.data(), -1, 0x56780000, 1, 1, 0x100, 0});
    const int avatar = 0x12340000, visualId = 0x56780000;
    write(game, 0x411c, avatar);
    write(game, 0x5190, campaign.data());
    write(cell, 0, avatar); write(cell, 0x248, visualId); write(visual, 0, visualId);
    write(cell, 0x244, 17.5f); write(cell, 0x364, 42); write(cell, 0x35c, 1);
    write(cell, 0x250, 2); write(cell, 0x254, 7); write(cell, 0x258, 29);
    write(campaign, 0x10, Key{111, 1, 2}); write(campaign, 0x30, 12345);
    auto** gameGlobal = reinterpret_cast<void**>(code(0x12b3c04));
    auto** gfxGlobal = reinterpret_cast<void**>(code(0x12b3c08));
    struct Restore {
        void** slot; void* value; ~Restore() { *slot = value; }
    } restoreGame{gameGlobal, *gameGlobal}, restoreGfx{gfxGlobal, *gfxGlobal};
    *gameGlobal = game.data(); *gfxGlobal = gfx.data();
    Patch visualRelease(code(0xa65ad0), reinterpret_cast<void*>(&ReleaseVisual));
    Patch nodeRelease(code(0xa4ec10), reinterpret_cast<void*>(&ReleaseNode));
    Patch statsLookup(code(0xa679e0), reinterpret_cast<void*>(&ModelStats));
    releasedCount = 0; releasedVisual = nullptr; immediateRemoval = false;
    const auto beforeCell = cell;
    const auto beforeCampaign = campaign;
    CoopEngine::ReleaseCellBody(code(CoopEngine::kReleaseCellBodyRva), avatar);
    require(releasedVisual == visual.data() && immediateRemoval, "Release wrapper passed wrong visual/flag");
    require(releasedCount == 2 && releasedNodes[0] == 7 && releasedNodes[1] == 29,
        "Native body release did not remove both old nodes");
    require(integer(cell, 0x248) == 0 && integer(cell, 0x250) == 0 &&
        integer(cell, 0x254) == -1 && integer(cell, 0x258) == -1, "Old body handles remain live");
    require(integer(gfx, 0x174) == 0 && integer(gfx, 0x16c) == 0,
        "Old visual pool slot was not released");
    auto expectedCell = beforeCell;
    write(expectedCell, 0x248, 0); write(expectedCell, 0x250, 0);
    write(expectedCell, 0x254, -1); write(expectedCell, 0x258, -1);
    require(cell == expectedCell && campaign == beforeCampaign && integer(game, 0x411c) == avatar &&
        integer(game, 0x28) == 1, "Body release changed avatar, health, collider or campaign");

    originalStats.fill(0); sharedStats.fill(0);
    write(originalStats, 0x1210, 1); write(sharedStats, 0x120c, 1);
    using Ability = bool(__cdecl*)();
    auto first = reinterpret_cast<Ability>(code(0xa67a60));
    auto second = reinterpret_cast<Ability>(code(0xa67aa0));
    require(first() && !second(), "Native original mouth abilities not reproduced");
    write(cell, 0xfc, Key{222, 1, 2});
    require(first() && !second(), "Changing only rendered body unexpectedly changed campaign abilities");
    write(campaign, 0x10, Key{222, 1, 2});
    require(!first() && second(), "Shared campaign key failed to switch native mouth abilities");
    return checks;
}
