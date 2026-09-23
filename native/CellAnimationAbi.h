#pragma once
#include "CellMotionAbi.h"
namespace CoopEngine {
// Installed Steam March 2017 executable. Full routine hashes include ASLR relocation normalization.
// e6d200: cdecl float(cell*, otherCell*, targetIndex, previousIndex); visual animation transition
constexpr std::uintptr_t kPlayCellAnimationRva = 0xa6d200;
constexpr std::size_t kPlayCellAnimationSize = 0x13d;
constexpr std::uint32_t kPlayCellAnimationHash = 0xb79e3f5fu;
constexpr std::size_t kPlayCellAnimationRelocations[] = {0x4f,0x70,0xd1};
}
