// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "CellMotionAbi.h"
namespace CoopEngine {
// E75B80 closes the final History and tail-calls E72C80 only in state 5.
// Block before it changes to state 3, records landfall and starts the cinematic.
constexpr std::uintptr_t kLeaveCellStageRva=0xa72c80;
constexpr std::size_t kLeaveCellStageSize=0x1e6;
constexpr std::uint32_t kLeaveCellStageHash=0x91893935;
constexpr std::size_t kLeaveCellStageRelocations[]={0x1,0x3d,0x4a,0x67,0x7c,0xbf,0xe7,0x113,0x124,0x168,0x171,0x17a,0x183,0x18c,0x195,0x19e,0x1a7,0x1b0,0x1b9,0x1d1};
// Secondary guard: the final campaign creature editor after the cinematic.
constexpr std::uintptr_t kEnterLandEditorRva=0xa61140;
constexpr std::size_t kEnterLandEditorSize=0x62;
constexpr std::uint32_t kEnterLandEditorHash=0x7c183198;
constexpr std::size_t kEnterLandEditorRelocations[]={0x1,0x16,0x2f,0x55};
}
