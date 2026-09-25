// SPDX-License-Identifier: GPL-3.0-or-later
// Steam March 2017 x86. Full-routine FNV-1a fingerprints normalize PE HIGHLOW
// relocations to image base 0x400000; tests verify the installed executable.
#pragma once
#include <cstdint>
#include <cstddef>
namespace CoopEngine {
inline constexpr std::uintptr_t kModelAttachmentsRva=0xa83e30;
inline constexpr std::size_t kModelAttachmentsSize=0x31a;
inline constexpr std::uint32_t kModelAttachmentsHash=0xbd9561f9u;
inline constexpr std::size_t kModelAttachmentsRelocations[]={0x1a,0x34,0x3d,0x46,0x56,0x63,0x6c,0x75,0x12b,0x1c4,0x2c5};
inline constexpr std::uintptr_t kSavePrepareRva=0x728420;
inline constexpr std::size_t kSavePrepareSize=0x46b;
inline constexpr std::uint32_t kSavePrepareHash=0xd8675c81u;
inline constexpr std::size_t kSavePrepareRelocations[]={0x4a,0x93,0xa9,0x113,0x12e,0x1b3,0x1cc,0x251,0x26a,0x2ec,0x3cb};
inline constexpr std::uintptr_t kSaveWorldRva=0x729000;
inline constexpr std::size_t kSaveWorldSize=0x5e8;
inline constexpr std::uint32_t kSaveWorldHash=0x7145b968u;
inline constexpr std::size_t kSaveWorldRelocations[]={0x47,0x62,0x11a,0x248,0x250,0x295,0x2b1,0x3b7,0x5b0,0x5b8,0x5cc,0x5d4};
inline constexpr std::uintptr_t kSaveCleanupRva=0x728890;
inline constexpr std::size_t kSaveCleanupSize=0xfd;
inline constexpr std::uint32_t kSaveCleanupHash=0xa4ba38e8u;
inline constexpr std::size_t kSaveCleanupRelocations[]={0x44,0x7a};
inline constexpr std::uintptr_t kTimelineShowRva=0xa44200;
inline constexpr std::size_t kTimelineShowSize=0x74d;
inline constexpr std::uint32_t kTimelineShowHash=0xc63c35cau;
inline constexpr std::size_t kTimelineShowRelocations[]={0x391,0x398,0x3ed,0x3f4};
inline constexpr std::uintptr_t kHistoryCreateRva=0x3ec230;
inline constexpr std::size_t kHistoryCreateSize=0xb5;
inline constexpr std::uint32_t kHistoryCreateHash=0x2248a1c5u;
inline constexpr std::size_t kHistoryCreateRelocations[]={0x9};
inline constexpr std::uintptr_t kHistoryGetRva=0x27de50;
inline constexpr std::size_t kHistoryGetSize=0x6;
inline constexpr std::uint32_t kHistoryGetHash=0x2a7e9708u;
inline constexpr std::size_t kHistoryGetRelocations[]={0x1};
inline constexpr std::uintptr_t kHistoryEventRva=0xa394a0;
inline constexpr std::size_t kHistoryEventSize=0x212;
inline constexpr std::uint32_t kHistoryEventHash=0xbc34ee36u;
inline constexpr std::size_t kHistoryEventRelocations[]={0x6c,0x79};
}
