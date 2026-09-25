#pragma once
#include "CellMotionAbi.h"

namespace CoopEngine
{
    // Steam March 2017, E66010: EAX = cell index, stack = immediate GFX removal.
    // Releases the cell's graphics and articulated nodes, preserving the cell,
    // its broadphase entry, avatar handle, health and campaign progression.
    constexpr std::uintptr_t kReleaseCellBodyRva = 0xa66010;
    constexpr std::size_t kReleaseCellBodySize = 0xdc;
    constexpr std::uint32_t kReleaseCellBodyHash = 0x9f00005cu;
    constexpr std::size_t kReleaseCellBodyRelocations[] = {0x2,0x17,0x31,0x66,0x94};

    // E6D8F0: cdecl(index, flags). The normal cell update calls this when the
    // GFX handle is zero; it recreates the body using the cell's current key.
    constexpr std::uintptr_t kBuildCellBodyRva = 0xa6d8f0;
    constexpr std::size_t kBuildCellBodySize = 0x1e4;
    constexpr std::uint32_t kBuildCellBodyHash = 0x3375c72du;
    constexpr std::size_t kBuildCellBodyRelocations[] = {
        0x2,0x5c,0x6c,0x7c,0x9d,0xd3,0x10f,0x11d,0x139,0x17c,0x1a7,0x1b8};

    inline void ReleaseCellBody(void* function, int index)
    {
        __asm {
            mov eax, index
            push 1
            call function
            add esp, 4
        }
    }
}
