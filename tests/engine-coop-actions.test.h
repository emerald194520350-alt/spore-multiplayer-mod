#pragma once
#include "engine-cell-body.test.h"

namespace CoopActionsFixture
{
    struct Record { unsigned metadata[3]{}; unsigned* data; };
    inline std::array<unsigned,27> timeline{};
    inline std::array<unsigned,24> summary{};
    inline Record timelineRecord{{},timeline.data()}, summaryRecord{{},summary.data()};
    inline int manager=0, mateCalls=0;
    inline unsigned char* activeGame=nullptr;
    inline int __cdecl CellMode() { return 0x1654c00; }
    inline void* __cdecl HistoryManager() { return &manager; }
    inline void* __fastcall CreateRecord(void*,void*,unsigned category,unsigned)
    { return category==12 ? &timelineRecord : &summaryRecord; }
    inline void __fastcall ResizeRecord(void*,void*,unsigned) {}
    inline void __cdecl FillSummary(void*,const void*,const void*) {}
    inline void __cdecl SpawnMate()
    { ++mateCalls; const int mate=0x12340001; std::memcpy(activeGame+0x51d4,&mate,4); }
}

// Run the game's actual history recorder, diet calculation and call-mate gate.
// Only the allocator, secondary summary lookup and final spawn are recorders.
inline int TestEngineCoopActions(std::uintptr_t base)
{
    using namespace CoopActionsFixture;
    int checks=0;
    auto require=[&](bool ok,const char* message) {
        if (!ok) throw std::runtime_error(message); ++checks;
    };
    auto code=[base](std::uintptr_t rva) { return reinterpret_cast<unsigned char*>(base+rva); };
    constexpr std::size_t globalRelocations[]={1};
    constexpr std::size_t dietRelocations[]={0xf,0x26,0x34,0x3a,0x4a};
    constexpr std::size_t mateRelocations[]={1,0xf,0x1e};
    require(CoopEngine::MatchesMotionCode(code(0xa5c530),0x25,base,0x25,0x7a9ab121,globalRelocations),"Native shared diet accessor ABI mismatch");
    require(CoopEngine::MatchesMotionCode(code(0xa511c0),0x6e,base,0x6e,0x1060c94c,dietRelocations),"Native diet formula ABI mismatch");
    require(CoopEngine::MatchesMotionCode(code(0xa5c520),0xf,base,0xf,0x19204fe1,globalRelocations),"Native history timer ABI mismatch");
    require(CoopEngine::MatchesMotionCode(code(0xa76320),0x36,base,0x36,0x05924522,mateRelocations),"Native call-mate gate ABI mismatch");

    alignas(4) std::array<unsigned char,0x51e4> game{};
    alignas(4) std::array<unsigned char,0xec> campaign{};
    alignas(4) std::array<unsigned char,0x944> ui{};
    auto write=[](auto& buffer,size_t offset,const auto& value) { std::memcpy(buffer.data()+offset,&value,sizeof(value)); };
    write(game,0x5190,campaign.data()); write(campaign,0x74,12000);
    auto** gameGlobal=reinterpret_cast<void**>(code(0x12b3c04));
    auto** uiGlobal=reinterpret_cast<void**>(code(0x12b3c0c));
    struct Restore { void** at; void* value; ~Restore() { *at=value; } };
    Restore restoreGame{gameGlobal,*gameGlobal},restoreUI{uiGlobal,*uiGlobal};
    *gameGlobal=game.data(); *uiGlobal=ui.data(); activeGame=game.data();
    using CellBodyFixture::Patch;
    Patch mode(code(0x75b910),reinterpret_cast<void*>(&CellMode));
    Patch managerGet(code(0x27de50),reinterpret_cast<void*>(&HistoryManager));
    Patch create(code(0x3ec230),reinterpret_cast<void*>(&CreateRecord));
    Patch resize(code(0x3eb8f0),reinterpret_cast<void*>(&ResizeRecord));
    Patch summaryFill(code(0xa39400),reinterpret_cast<void*>(&FillSummary));
    Patch mateSpawn(code(0xa760f0),reinterpret_cast<void*>(&SpawnMate));
    using Key=CellBodyFixture::Key;
    using Event=void*(__cdecl*)(unsigned,const Key*,const Key*,const Key*,const Key*);
    const auto event=reinterpret_cast<Event>(code(CoopEngine::kHistoryEventRva));
    const Key creature{111,222,333},empty{};
    auto record=[&](unsigned id,int food,int plant) {
        write(campaign,0x1c,food); write(campaign,0x20,plant);
        timeline.fill(0);summary.fill(0);
        require(event(id,&creature,&empty,&empty,&empty)==&timelineRecord,"Native history returned wrong record");
        float diet=0;std::memcpy(&diet,&timeline[1],4);return diet;
    };
    const float start=record(0x9ef61113u,400,200);
    const float afterPlant=record(0x9ef61113u,500,300);
    require(timeline[0]==0x9ef61113u && timeline[2]==12 && timeline[3]==222 && timeline[4]==111,
        "Native plant history did not keep event, campaign time and shared creature");
    const float afterMeat=record(0xac7161b5u,500,200);
    require(timeline[0]==0xac7161b5u && summary[0]==0xac7161b5u,"Meat event did not reach both native history categories");
    require(std::isfinite(start) && afterPlant<start && afterMeat>start,
        "Guest plant/meat counters must move the actual native history trajectory in opposite directions");

    const auto callMate=reinterpret_cast<void(__cdecl*)()>(code(0xa76320));
    mateCalls=0;campaign[0x69]=1;
    callMate();
    int mate=0;std::memcpy(&mate,game.data()+0x51d4,4);
    require(mateCalls==1 && mate==0x12340001,"Unlocked mate button did not reach native spawn");
    ui[0x936]=1;callMate();
    require(mateCalls==1,"Busy native UI must block duplicate mating");
    ui[0x936]=0;campaign[0x69]=0;callMate();
    require(mateCalls==1,"Locked mating must not be forced");
    campaign[0x69]=1;write(game,0x51cc,1000.0f);callMate();
    require(mateCalls==1,"Native mating cooldown must be preserved");
    return checks;
}
