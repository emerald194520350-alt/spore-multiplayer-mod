// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../native/CellLifecycleAbi.h"
#include "engine-cell-body.test.h"

namespace LifecycleFixture {
    inline std::array<unsigned char,0x100> resource{};
    inline int lootCalls=0, deathActions=0;
    inline void __fastcall ResourceInit(void* self,void*) { *static_cast<unsigned*>(self)=0; }
    inline void __fastcall ResourceRelease(void*,void*) {}
    inline void* __cdecl ResourceGet(void*,void*) { return resource.data(); }
    inline int __cdecl Difference(void*) { return 2; }
    inline unsigned __cdecl Hash(const char*) { return 1; }
    inline void* __cdecl Effect(void*,unsigned) { return nullptr; }
    inline void __cdecl Attacker(void*,int) {}
    inline int __cdecl Stage() { return 3; }
    inline int __cdecl LootScale(int,int) { return 3; }
    inline void __cdecl Loot(void*,void*,void*,void*,float,int,int,float,bool,bool) { ++lootCalls; }
    inline float __cdecl Animation(void*,int,int,int) { return 0.5f; }
    inline int __fastcall Allocate(void*,void*) { return 0x45670000; }
    inline void __cdecl DeathAction(int,float) { ++deathActions; }
    inline int nameWrites=0;
    inline void __fastcall SetModelName(void*,void*,const char16_t*) { ++nameWrites; }
}

inline int TestEngineLifecycle(std::uintptr_t base)
{
    using namespace LifecycleFixture;
    using CellBodyFixture::Patch;
    int checks=0;
    auto require=[&](bool ok,const char* what) { if (!ok) throw std::runtime_error(what); ++checks; };
    auto code=[base](std::uintptr_t rva) { return reinterpret_cast<unsigned char*>(base+rva); };
    using namespace CoopEngine;
#define VERIFY_LIFECYCLE(n) require(MatchesMotionCode(code(k##n##Rva),k##n##Size,base,k##n##Size,k##n##Hash,k##n##Relocations),"Lifecycle ABI: " #n)
    VERIFY_LIFECYCLE(KillCell); VERIFY_LIFECYCLE(ResetCellWorld);
    VERIFY_LIFECYCLE(SpawnAvatar); VERIFY_LIFECYCLE(HatchAvatar);
#undef VERIFY_LIFECYCLE
    require(MatchesStaticCode(code(0x9f95a0),0x79,0x79,0xe51d1273),"Galaxy saved-world list ABI");
    require(MatchesStaticCode(code(0x175e20),0x2d,0x2d,0x0b40f7fa),"Campaign name setter ABI");
    alignas(4) std::array<unsigned char,0x51e4> game{};
    alignas(4) std::array<unsigned char,0x398> cell{};
    alignas(4) std::array<unsigned char,0x80> action{};
    alignas(4) std::array<unsigned char,0xec> campaign{};
    auto write=[](auto& a,size_t offset,const auto& value) { std::memcpy(a.data()+offset,&value,sizeof(value)); };
    struct Pool { void* data; int next,id,count,allocated,stride,unused; };
    write(game,0x1c,Pool{cell.data(),-1,0x12340000,1,1,0x398,0});
    write(game,0x54,Pool{action.data(),-1,0x45670000,1,1,0x80,0});
    write(game,0x411c,-1); write(game,0x5190,campaign.data());
    write(cell,0,0x12340000); write(action,0,0x45670000);
    write(cell,0x358,3); write(cell,0x58,1.0f);
    auto** global=reinterpret_cast<void**>(code(0x12b3c04));
    struct Restore { void** at; void* old; ~Restore(){*at=old;} } restore{global,*global};
    *global=game.data();
    Patch init(code(0x343bd0),reinterpret_cast<void*>(&ResourceInit));
    Patch release(code(0xa82130),reinterpret_cast<void*>(&ResourceRelease));
    Patch get(code(0xa4cbf0),reinterpret_cast<void*>(&ResourceGet));
    Patch diff(code(0xa57340),reinterpret_cast<void*>(&Difference));
    Patch hash(code(0x171cf0),reinterpret_cast<void*>(&Hash));
    Patch fx(code(0xa66840),reinterpret_cast<void*>(&Effect));
    Patch attacker(code(0xa71f00),reinterpret_cast<void*>(&Attacker));
    Patch stage(code(0xa4ee40),reinterpret_cast<void*>(&Stage));
    Patch scale(code(0xa52a20),reinterpret_cast<void*>(&LootScale));
    Patch loot(code(0xa771d0),reinterpret_cast<void*>(&Loot));
    Patch anim(code(0xa6d200),reinterpret_cast<void*>(&Animation));
    Patch alloc(code(0x772270),reinterpret_cast<void*>(&Allocate));
    Patch death(code(0xa59170),reinterpret_cast<void*>(&DeathAction));
    using Kill=bool(__cdecl*)(void*,int,void*,bool,bool,float);
    const auto kill=reinterpret_cast<Kill>(code(kKillCellRva));
    lootCalls=deathActions=0;
    require(kill(cell.data(),-1,nullptr,false,false,0),"Owner native death accepts lethal guest hit");
    require(lootCalls==1 && deathActions==1 && cell[0x113]==1,"Native death emits loot and starts corpse animation");
    require(!kill(cell.data(),-1,nullptr,false,false,0) && lootCalls==1,"Repeated lethal hit must not duplicate loot");
    int kills=-1; std::memcpy(&kills,campaign.data()+0x6c,4);
    require(kills==0,"Owner replay must not double-credit the guest kill");

    // Reproduce the native editor setter's silent no-op for a campaign override.
    alignas(4) std::array<unsigned char,0x200> editor{};
    void* nameVtable[]={reinterpret_cast<void*>(&SetModelName)};
    void** model=nameVtable;
    write(editor,0x8c,&model); write(editor,0x1d0,100u); write(editor,0x1d4,104u);
    auto setName=reinterpret_cast<void(__thiscall*)(void*,const char16_t*)>(code(0x175e20));
    nameWrites=0;setName(editor.data(),u"1");
    require(nameWrites==0,"Native campaign name override reproduces lost-name bug");
    write(editor,0x1d4,100u);setName(editor.data(),u"1");
    require(nameWrites==1,"Without override native setter reaches model");
    return checks;
}
