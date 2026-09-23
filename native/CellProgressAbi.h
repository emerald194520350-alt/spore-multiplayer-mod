#pragma once
#include "CellMotionAbi.h"
namespace CoopEngine {
// Installed Steam March 2017 executable. Full routine hashes include ASLR relocation normalization.
// e7ce10: cdecl(int amount, bool plant); native body growth, camera and DNA notifications
constexpr std::uintptr_t kAddFoodRva = 0xa7ce10;
constexpr std::size_t kAddFoodSize = 0x257;
constexpr std::uint32_t kAddFoodHash = 0xe8ae69cau;
constexpr std::size_t kAddFoodRelocations[] = {0x1,0x53,0x69,0x7d,0x8f,0xb2,0xd5,0xea,0x119,0x155,0x1e6,0x1f2,0x1ff};
// e5d7b0: cdecl(int part); native quest counter, unlock popup and palette
constexpr std::uintptr_t kUnlockPartRva = 0xa5d7b0;
constexpr std::size_t kUnlockPartSize = 0x254;
constexpr std::uint32_t kUnlockPartHash = 0x29e741c5u;
constexpr std::size_t kUnlockPartRelocations[] = {0x2,0x22,0x30,0x38,0x77,0x7f,0x8a,0xa7,0xd9,0x12d,0x138,0x146,0x155,0x16c,0x17f,0x18d,0x1a4,0x1bf,0x1fd,0x246};
// e59800: cdecl(bool standalone); false selects campaign editor and native return handler
constexpr std::uintptr_t kEnterCellEditorRva = 0xa59800;
constexpr std::size_t kEnterCellEditorSize = 0xd7;
constexpr std::uint32_t kEnterCellEditorHash = 0xe633e99au;
constexpr std::size_t kEnterCellEditorRelocations[] = {0x2,0x18,0x37,0x52,0x6a,0x77,0x94};
// e79720: cdecl(); native first-part cinematic
constexpr std::uintptr_t kPartCinematicRva = 0xa79720;
constexpr std::size_t kPartCinematicSize = 0x2d6;
constexpr std::uint32_t kPartCinematicHash = 0x2e68f3d3u;
constexpr std::size_t kPartCinematicRelocations[] = {0x1,0x32,0x41,0x6d,0x9b,0xa0,0xc3,0xc8,0x110,0x159,0x16c,0x179,0x192,0x1a3,0x1b3,0x1c6,0x1d2,0x1e0,0x1ee,0x201,0x23a,0x243,0x24c,0x255,0x25e,0x267,0x270,0x279,0x282,0x28b,0x2a2,0x2b0,0x2be};
}
