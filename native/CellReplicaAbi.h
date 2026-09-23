#pragma once
#include <cstdint>
#include <cstddef>

namespace CoopEngine
{
    // Steam March 2017. E780A0 uses BBBF50 -> BBB380 to unregister
    // field_364 from the cell collision broadphase at game+0x4104.
    // This self-contained cdecl routine contains no ASLR relocations/calls.
    constexpr std::uintptr_t kUnregisterCollisionRva = 0x7bb380;
    constexpr std::size_t kUnregisterCollisionSize = 0xca;
    constexpr std::uint32_t kUnregisterCollisionHash = 0xf1497561u;

    inline bool MatchesStaticCode(const unsigned char* code, std::size_t length,
        std::size_t size, std::uint32_t expectedHash)
    {
        if (!code || length < size) return false;
        std::uint32_t hash = 2166136261u;
        for (std::size_t i = 0; i < size; ++i) hash = (hash ^ code[i]) * 16777619u;
        return hash == expectedHash;
    }
}
