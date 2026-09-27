// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../native/CellLifecycleAbi.h"
#include "../native/CellStageEndAbi.h"
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
    inline int lootResource=0,lootLevel=0,removals=0,defaultSlot=-1;
    inline unsigned char* removedCell=nullptr;
    inline void __cdecl Loot(void*,void*,void*,void*,float,int level,int resourceID,float,void*,void*) {
        ++lootCalls;lootResource=resourceID;lootLevel=level;
    }
    inline int __cdecl DefaultResource(int slot) { defaultSlot=slot;return 777; }
    inline void __cdecl Remove(int,bool,float,bool) { ++removals;*reinterpret_cast<int*>(removedCell)=-1; }
    inline float capturedScale=0;
    inline float __cdecl Range(void*) { return 1.0f; }
    __declspec(naked) inline void CaptureCreatedScale() {
        __asm {
            movss xmm0,[esp+7Ch]
            movss capturedScale,xmm0
            pop edi
            pop esi
            pop ebp
            pop ebx
            add esp,50h
            mov eax,12340000h
            ret
        }
    }
    inline float __cdecl Animation(void*,int,int,int) { return 0.5f; }
    inline int __fastcall Allocate(void*,void*) { return 0x45670000; }
    inline void __cdecl DeathAction(int,float) { ++deathActions; }
    inline int nameWrites=0;
    inline void __fastcall SetModelName(void*,void*,const char16_t*) { ++nameWrites; }
    inline void* endButtonVtable[11]{};
    inline void** endButton=endButtonVtable;
    inline std::string endEvents;
    inline void* __fastcall FindEndControl(void*,void*,unsigned,bool) { return &endButton; }
    inline void* __fastcall EndControlCast(void* self,void*,unsigned) { return self; }
    inline void __fastcall EndControlFlag(void*,void*,int,bool) {}
    inline void __cdecl EndPause(int,bool) {}
    inline void* __cdecl EndManager() { return nullptr; }
    inline void __fastcall EndHistory(void*,void*,bool visible) { if (!visible) endEvents+='H'; }
    inline void __fastcall EndLayout(void*,void*,bool) {}
    inline void __cdecl BlockLandfall() { endEvents+='T'; }
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
    VERIFY_LIFECYCLE(SpawnAvatar); VERIFY_LIFECYCLE(HatchAvatar); VERIFY_LIFECYCLE(BurstCell);
    VERIFY_LIFECYCLE(FinishCinematic);
    VERIFY_LIFECYCLE(LeaveCellStage); VERIFY_LIFECYCLE(EnterLandEditor);
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

    // The three-piece default meat table is requested by breakup, not by the
    // preceding kill animation. Exercise the real native breakup routine.
    {
        Patch defaults(code(0xa4cc90),reinterpret_cast<void*>(&DefaultResource));
        Patch remove(code(0xa780a0),reinterpret_cast<void*>(&Remove));
        removedCell=cell.data();removals=0;
        auto burst=reinterpret_cast<void(__cdecl*)(int,bool,float)>(code(kBurstCellRva));
        burst(0x12340000,false,0);
        require(defaultSlot==7 && lootResource==777 && lootLevel==2 && lootCalls==2 && removals==1,
            "Guest corpse removal must invoke the default meat table before removing the owner corpse");
        burst(0x12340000,false,0);
        require(lootCalls==2 && removals==1,"Repeated corpse breakup emits no duplicate meat");
    }
    {
        // Run the real creation size math, returning just before allocation.
        // All following allocations/graphics remain outside this private fixture.
        const std::size_t relocations[]={0x2e,0x4c,0x54,0x5e,0x7f};
        require(MatchesMotionCode(code(0xa74a20),0xa8,base,0xa8,0x8f9010f1,relocations),"Cell creation size ABI");
        Patch range(code(0xdf340),reinterpret_cast<void*>(&Range));
        Patch finish(code(0xa74ac8),reinterpret_cast<void*>(&CaptureCreatedScale));
        using Create=int(__cdecl*)(void*,void*,float,void*,int,float,float,bool,void*,int);
        auto create=reinterpret_cast<Create>(code(0xa74a20));
        for (const float worldScale:{0.5f,150.0f,500.0f}) {
            write(game,0x514c,worldScale);
            create(nullptr,nullptr,0,nullptr,-1,worldScale*2,0,true,nullptr,0);
            require(std::abs(capturedScale-2.0f)<0.00001f,"Respawn factor creates full-sized avatar at every growth level");
        }
        create(nullptr,nullptr,0,nullptr,6,1,0.003f,true,nullptr,0);
        require(std::abs(capturedScale-0.003f)<0.000001f,"Food/NPC physics must start at their network scale");
    }

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

    // Run the game's actual History-close callback. Its final-stage branch
    // must reach our interception point only AFTER hiding History.
    {
        const std::size_t relocs[]={0x1,0x56,0x80};
        require(MatchesMotionCode(code(0xa75b80),0x93,base,0x93,0x19e592be,relocs),"Final History callback ABI");
        alignas(4) std::array<unsigned char,0x944> ui{};
        auto** uiGlobal=reinterpret_cast<void**>(code(0x12b3c0c));
        Restore restoreUI{uiGlobal,*uiGlobal}; *uiGlobal=ui.data();
        endButtonVtable[3]=reinterpret_cast<void*>(&EndControlCast);
        endButtonVtable[10]=reinterpret_cast<void*>(&EndControlFlag);
        Patch control(code(0x410650),reinterpret_cast<void*>(&FindEndControl));
        Patch pause(code(0xa53640),reinterpret_cast<void*>(&EndPause));
        Patch historyManager(code(0x73d510),reinterpret_cast<void*>(&EndManager));
        Patch history(code(0xa44200),reinterpret_cast<void*>(&EndHistory));
        Patch layout(code(0x410630),reinterpret_cast<void*>(&EndLayout));
        Patch uiManager(code(0x73d500),reinterpret_cast<void*>(&EndManager));
        Patch landfall(code(kLeaveCellStageRva),reinterpret_cast<void*>(&BlockLandfall));
        auto closeHistory=reinterpret_cast<void(__cdecl*)()>(code(0xa75b80));
        write(game,0x5158,0);endEvents.clear();closeHistory();
        require(endEvents=="H","Closing ordinary History must not trigger a stage ending");
        write(game,0x5158,5);endEvents.clear();closeHistory();
        require(endEvents=="HT","Native final History closes before the landfall interception");
        int stage=0;std::memcpy(&stage,game.data()+0x5158,4);
        require(stage==5,"Intercepted ending must not start native landfall state 3");
    }
    return checks;
}
