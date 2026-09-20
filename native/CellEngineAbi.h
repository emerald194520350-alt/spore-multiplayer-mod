#pragma once
#include <cstdint>
#include <cstring>

namespace CoopEngine
{
    // Verified in this installation's Steam March 2017 executable:
    // VA E780A0, cdecl (cell index, death effects/loot, size, immediate GFX).
    // Unlike cObjectPool::DeleteObject this unlinks spatial queries and destroys
    // all structure graphics. All call sites for quiet despawn pass (id,0,0,0).
    constexpr std::uintptr_t kRemoveCellRva = 0xA780A0;
    constexpr size_t kRemoveCellCodeSize = 0x187;

    inline bool MatchesRemovalAbi(const unsigned char* code, size_t length,
        std::uintptr_t address, std::uintptr_t cellGameGlobal,
        std::uintptr_t poolGet, std::uintptr_t poolDelete)
    {
        if (!code || length < kRemoveCellCodeSize) return false;
        const unsigned char setup[] = { 0x83,0xec,0x10,0x55,0x8b,0x6c,0x24,0x18,0x83,0xc1,0x1c,0x55,0xe8 };
        const unsigned char teardown[] = { 0x5e,0x5d,0x83,0xc4,0x10,0xc3 };
        if (code[0] != 0x8b || code[1] != 0x0d ||
            std::memcmp(code+6, setup, sizeof(setup)) != 0 ||
            code[0x172] != 0x8b || code[0x173] != 0x0d || code[0x17c] != 0xe8 ||
            std::memcmp(code+0x181, teardown, sizeof(teardown)) != 0) return false;
        std::uint32_t firstGlobal = 0, lastGlobal = 0;
        std::int32_t getOffset = 0, deleteOffset = 0;
        std::memcpy(&firstGlobal, code+2, 4);
        std::memcpy(&lastGlobal, code+0x174, 4);
        std::memcpy(&getOffset, code+0x13, 4);
        std::memcpy(&deleteOffset, code+0x17d, 4);
        return firstGlobal == cellGameGlobal && lastGlobal == cellGameGlobal &&
            address + 0x17 + getOffset == poolGet &&
            address + 0x181 + deleteOffset == poolDelete;
    }
}
