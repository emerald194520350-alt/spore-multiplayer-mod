#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace CoopEngine
{
    // Steam March 2017 native routines, verified from the installed executable.
    // These setters update the articulated body nodes as well as mTransform.
    constexpr std::uintptr_t kPositionRva = 0xa5e590;
    constexpr std::size_t kPositionSize = 0x2b8;
    constexpr std::uint32_t kPositionHash = 0x52f88897u;
    constexpr std::size_t kPositionRelocations[] = { 0x99, 0xb4 };

    constexpr std::uintptr_t kOrientationRva = 0xa5e850;
    constexpr std::size_t kOrientationSize = 0x2c4;
    constexpr std::uint32_t kOrientationHash = 0x1f8e9452u;
    constexpr std::size_t kOrientationRelocations[] = { 0xa7, 0xc6 };

    constexpr std::uintptr_t kGraphicsRva = 0xa73f60;
    constexpr std::size_t kGraphicsSize = 0xac0;
    constexpr std::uint32_t kGraphicsHash = 0xd5d117d2u;
    constexpr std::size_t kGraphicsRelocations[] = { 0x17, 0x31, 0x71, 0x90, 0xae, 0xcf, 0xd5, 0xf3, 0x103, 0x13b, 0x179, 0x1a5, 0x1e5, 0x222, 0x23a, 0x256, 0x266, 0x280, 0x2a1, 0x2a9, 0x2b1, 0x2b9, 0x2c1, 0x2c9, 0x2d1, 0x2d7, 0x30f, 0x345, 0x34d, 0x397, 0x39d, 0x410, 0x439, 0x44d, 0x475, 0x484, 0x49c, 0x4a9, 0x4c4, 0x4de, 0x4eb, 0x504, 0x562, 0x578, 0x585, 0x5a4, 0x5dc, 0x614, 0x63e, 0x64f, 0x6a8, 0x6e7, 0x6f2, 0x71d, 0x739, 0x74a, 0x783, 0x7bc, 0x7f5, 0x8ea, 0x92f, 0x998, 0xa49, 0xa70, 0xa81, 0xa99 };

    template<std::size_t N>
    inline bool MatchesMotionCode(const unsigned char* code, std::size_t length,
        std::uintptr_t imageBase, std::size_t expectedSize, std::uint32_t expectedHash,
        const std::size_t (&relocations)[N])
    {
        if (!code || length < expectedSize) return false;
        std::uint32_t hash = 2166136261u;
        std::size_t relocation = 0;
        for (std::size_t i = 0; i < expectedSize;)
        {
            if (relocation < N && i == relocations[relocation])
            {
                if (i + 4 > expectedSize) return false;
                std::uint32_t value;
                std::memcpy(&value, code + i, 4);
                value -= static_cast<std::uint32_t>(imageBase - 0x400000u);
                for (unsigned j = 0; j < 4; ++j) hash = (hash ^ ((value >> (j*8)) & 255)) * 16777619u;
                i += 4;
                ++relocation;
            }
            else hash = (hash ^ code[i++]) * 16777619u;
        }
        return relocation == N && hash == expectedHash;
    }
}
