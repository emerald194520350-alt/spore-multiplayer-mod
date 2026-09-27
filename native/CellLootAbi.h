// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "CellMotionAbi.h"
namespace CoopEngine {
constexpr std::uintptr_t kSpawnLootRva=0xa771d0;
constexpr std::size_t kSpawnLootSize=0x5ca;
constexpr std::uint32_t kSpawnLootHash=0x93dd34cb;
constexpr std::size_t kSpawnLootRelocations[]={0x17,0x5a,0xfe,0x140,0x148,0x150,0x158,0x160,0x168,
    0x170,0x1b2,0x23b,0x24c,0x2ce,0x37b,0x3a5,0x3cc,0x3f3,0x406,0x415,0x461,0x52b,0x59b};
constexpr std::size_t kPartLootOutsideViewOffset=0x23a, kPartLootEligibleOffset=0x248;
inline void* gPartLootVisibilityOriginal=nullptr;
inline void* gPartLootEligible=nullptr;
inline unsigned gGuestLootDepth=0;

struct GuestLootScope {
    bool active;
    explicit GuestLootScope(bool enabled) : active(enabled) { if (active) ++gGuestLootDepth; }
    ~GuestLootScope() { if (active) --gGuestLootDepth; }
    GuestLootScope(const GuestLootScope&)=delete;
    GuestLootScope& operator=(const GuestLootScope&)=delete;
};

// Entered at E7740A only after native checks for an unowned part, its presence
// on the victim, and an existing part actor. Guest kills may be outside the
// owner's camera: skip only that rejection, keeping the native weighted table
// selection, spawn position and pickup behavior. No camera globals are changed.
__declspec(naked) inline void PartLootVisibilityHook() {
    __asm {
        pushfd
        cmp gGuestLootDepth,0
        je nativeVisibility
        popfd
        jmp dword ptr [gPartLootEligible]
    nativeVisibility:
        popfd
        jmp dword ptr [gPartLootVisibilityOriginal]
    }
}
}
