// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "engine-cell-body.test.h"
#include "../native/CellLootAbi.h"

namespace PartLootFixture {
    inline std::array<unsigned char,0x24> table{};
    inline std::array<unsigned char,0x100> pickup{};
    inline int spawned=0;
    inline void* spawnedResource=nullptr;
    inline float spawnedX=0;
    inline std::array<unsigned char,0x398> spawnedCell{};
    inline void __fastcall Init(void* self,void*) { *static_cast<unsigned*>(self)=0; }
    inline void __fastcall Release(void*,void*) {}
    inline void* __cdecl Get(void* ref,void*) { return ref==table.data() ? table.data() : pickup.data(); }
    inline double __fastcall RandomUnit(void*,void*) { return 0.5; }
    inline int __fastcall RandomCount(void*,void*,int) { return 0; }
    __declspec(naked) inline void Direction() {
        __asm {
            mov dword ptr [esi],0
            mov dword ptr [esi+4],0
            mov dword ptr [esi+8],0
            mov eax,esi
            ret
        }
    }
    inline int __cdecl Spawn(void* ref,int,int,const float* position,const float*,bool,float,float,float,float,int) {
        ++spawned; spawnedResource=ref; spawnedX=position[0];
        std::memcpy(spawnedCell.data()+0x4c,position,12);
        return 0x67890000;
    }
}

inline int TestEnginePartLoot(std::uintptr_t base)
{
    using namespace PartLootFixture;
    using CellBodyFixture::Patch;
    int checks=0;
    auto require=[&](bool ok,const char* why) { if (!ok) throw std::runtime_error(why); ++checks; };
    auto code=[base](std::uintptr_t rva) { return reinterpret_cast<unsigned char*>(base+rva); };
    using namespace CoopEngine;
    require(MatchesMotionCode(code(kSpawnLootRva),kSpawnLootSize,base,kSpawnLootSize,kSpawnLootHash,
        kSpawnLootRelocations),"Native part loot ABI");
    alignas(4) std::array<unsigned char,0x51e4> game{};
    alignas(4) std::array<unsigned char,0xec> campaign{};
    alignas(4) std::array<unsigned char,0x1c> entry{};
    auto write=[](auto& a,size_t at,const auto& value) { std::memcpy(a.data()+at,&value,sizeof(value)); };
    auto** global=reinterpret_cast<void**>(code(0x12b3c04));
    auto* bounds=reinterpret_cast<float*>(code(0x12b3c88));
    struct Restore {
        void** global; void* game; float* bounds; std::array<float,6> saved;
        ~Restore() { *global=game; std::memcpy(bounds,saved.data(),sizeof(saved)); }
    } restore{global,*global,bounds,{}};
    std::memcpy(restore.saved.data(),bounds,sizeof(restore.saved));
    *global=game.data();
    for (int i=0;i<3;++i) { bounds[i]=-10; bounds[i+3]=10; }
    write(game,0x5190,campaign.data()); write(game,0x516c,-1);
    struct Pool { void* data; int next,id,count,allocated,stride,unused; };
    spawnedCell.fill(0); write(spawnedCell,0,0x67890000); write(spawnedCell,0x58,1.0f);
    write(game,0x1c,Pool{spawnedCell.data(),-1,0x67890000,1,1,0x398,0});
    table.fill(0); pickup.fill(0);
    write(table,0,entry.data()); write(table,4,1); write(table,0x1c,true);
    write(entry,0,1); write(entry,4,pickup.data()); write(entry,0xc,1.0f); write(entry,0x10,1);
    write(pickup,0xb4,6); write(pickup,0xb8,7); // Native Spike unlock pickup.
    CellBodyFixture::sharedStats.fill(0);
    write(CellBodyFixture::sharedStats,0x1220,1); // Victim has a spike.
    CellBodyFixture::Key victim{222,1,1};
    float position[]={0,0,0};
    Patch init(code(0x343bd0),reinterpret_cast<void*>(&Init));
    Patch release(code(0xa82130),reinterpret_cast<void*>(&Release));
    Patch get(code(0xa4cbf0),reinterpret_cast<void*>(&Get));
    Patch stats(code(0xa679e0),reinterpret_cast<void*>(&CellBodyFixture::ModelStats));
    Patch random(code(0x536040),reinterpret_cast<void*>(&RandomUnit));
    Patch count(code(0x668f70),reinterpret_cast<void*>(&RandomCount));
    Patch direction(code(0xa5cae0),reinterpret_cast<void*>(&Direction));
    Patch spawn(code(0xa76480),reinterpret_cast<void*>(&Spawn));
    using Loot=void(__cdecl*)(void*,void*,int,const float*,float,int,void*,float,void*,void*);
    auto loot=reinterpret_cast<Loot>(code(0xa771d0));
    auto drop=[&] {
        spawned=0; write(game,0x516c,-1); spawnedCell[0x354]=0;
        loot(nullptr,&victim,0,position,1.0f,3,table.data(),0,nullptr,nullptr);
    };
    drop();
    require(spawned==1 && spawnedResource==pickup.data(),"Visible native kill creates the eligible spike fragment");
    position[0]=100; drop();
    require(spawned==0,"Native owner camera reproduces missing guest fragment outside its view");
    // Replay exactly the five relocated bytes Detours moves into its trampoline.
    auto gate=code(kSpawnLootRva+kPartLootOutsideViewOffset);
    struct Trampoline {
        unsigned char* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,16,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
        ~Trampoline() { if (code) VirtualFree(code,0,MEM_RELEASE); }
    } trampoline;
    require(trampoline.code!=nullptr,"Allocate isolated part visibility trampoline");
    std::memcpy(trampoline.code,gate,5); trampoline.code[5]=0xe9;
    const auto jump=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(gate+5)-
        reinterpret_cast<std::uintptr_t>(trampoline.code+10));
    std::memcpy(trampoline.code+6,&jump,4);
    FlushInstructionCache(GetCurrentProcess(),trampoline.code,10);
    struct ResetHook { ~ResetHook(){gPartLootVisibilityOriginal=gPartLootEligible=nullptr;} } resetHook;
    gPartLootVisibilityOriginal=trampoline.code;
    gPartLootEligible=code(kSpawnLootRva+kPartLootEligibleOffset);
    Patch visibility(gate,reinterpret_cast<void*>(&PartLootVisibilityHook));
    drop(); require(spawned==0,"Patched owner-only path retains native camera filtering");
    {
        GuestLootScope guest(true);
        for (int part:{2,3,4,5,6,7,8,9,10}) {
            write(pickup,0xb8,part); CellBodyFixture::sharedStats.fill(0);
            write(CellBodyFixture::sharedStats,0x1204+part*4,1);
            drop();
            require(spawned==1 && spawnedResource==pickup.data() && spawnedX==100 && spawnedCell[0x354]==1,
                "Guest kill emits the eligible fragment at the victim, outside owner view");
            write(campaign,0x34+part*4,1); drop();
            require(spawned==0,"Guest kills do not emit an already unlocked fragment");
            write(campaign,0x34+part*4,0); CellBodyFixture::sharedStats.fill(0); drop();
            require(spawned==0,"Guest kills cannot drop a part absent from the victim");
        }
        write(CellBodyFixture::sharedStats,0x122c,1);
        write(game,0x516c,0x67890000); write(spawnedCell,0x4c,0.0f);
        spawned=0; loot(nullptr,&victim,0,position,1.0f,3,table.data(),0,nullptr,nullptr);
        require(spawned==0,"Native existing-part guard still prevents a second active discovery");
        require(bounds[0]==-10 && bounds[3]==10,"Part drop does not move or expand the owner's camera");
        { GuestLootScope nested(true); require(gGuestLootDepth==2,"Nested corpse removal keeps guest loot context"); }
        require(gGuestLootDepth==1,"Nested loot context restores its caller");
    }
    require(gGuestLootDepth==0,"Guest loot context cannot leak to unrelated native kills");
    write(CellBodyFixture::sharedStats,0x122c,1); drop();
    require(spawned==0,"Subsequent owner kill outside view follows the original native path");
    return checks;
}
