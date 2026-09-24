#pragma once
#include <array>
#include "../native/CellMotionAbi.h"
#include "../native/CellReplicaAbi.h"

namespace EditorDispatchFixture
{
    inline int saveCalls=0;
    inline void* savedUI=nullptr;
    inline std::uint32_t command=0x102;
    inline std::uint32_t __fastcall GetCommand(void*, void*) { return command; }
    inline void __fastcall RecordSave(void* ui, void*) { ++saveCalls; savedUI=ui; }
}

// Execute the real EditorUI handler AND command switch from a private mapped
// game image. Only the final save routine is replaced by a recorder: the test
// must not create files, start the game, or require its global UI/renderer.
inline int TestEngineEditorDispatch(std::uintptr_t base)
{
    using namespace EditorDispatchFixture;
    int checks=0;
    auto require=[&](bool ok,const char* message) {
        if (!ok) throw std::runtime_error(message);
        ++checks;
    };
    auto code=[base](std::uintptr_t rva) { return reinterpret_cast<unsigned char*>(base+rva); };
    constexpr std::size_t handlerRelocations[]={0x1f3};
    constexpr std::size_t dispatchRelocations[]={0x24,0x29,0x5b,0x12d,0x15f,0x1c5};
    require(CoopEngine::MatchesMotionCode(code(0x1e0040),0x363,base,0x363,0x23ac5af4u,handlerRelocations),
        "EditorUI handler ABI mismatch");
    require(CoopEngine::MatchesMotionCode(code(0x1dfd40),0x2bf,base,0x2bf,0x08e4d00eu,dispatchRelocations),
        "EditorUI command dispatcher ABI mismatch");
    require(CoopEngine::MatchesStaticCode(code(0x410110),0x11,0x11,0xd8bcd89du),
        "Editor layout visibility ABI mismatch");
    require(CoopEngine::MatchesStaticCode(code(0x1dfb80),0x6d,0x6d,0x197278e4u),
        "Editor validated save entry ABI mismatch");
    require(*reinterpret_cast<std::uintptr_t*>(code(0x1e0000)+2*4)==base+0x1dfdf6,
        "Accept command must select the native validated save branch");

    auto save=code(0x1dfb80);
    DWORD protection=0;
    require(VirtualProtect(save,5,PAGE_EXECUTE_READWRITE,&protection)!=0,"Cannot prepare private save recorder");
    struct RestoreSave {
        unsigned char* address; DWORD protection; std::array<unsigned char,5> original;
        ~RestoreSave() {
            std::memcpy(address,original.data(),original.size());
            DWORD ignored; VirtualProtect(address,5,protection,&ignored);
            FlushInstructionCache(GetCurrentProcess(),address,5);
        }
    } restore{save,protection,{}};
    std::memcpy(restore.original.data(),save,5);
    const auto displacement=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&RecordSave)
        -reinterpret_cast<std::uintptr_t>(save+5));
    save[0]=0xe9; std::memcpy(save+1,&displacement,4);
    FlushInstructionCache(GetCurrentProcess(),save,5);

    alignas(4) std::array<unsigned char,0x12c> ui{};
    alignas(4) std::array<unsigned char,0xa0> layout{};
    auto* layoutPointer=layout.data();
    std::memcpy(ui.data()+0x28,&layoutPointer,4); // mMainUI.mpLayoutObjects
    layout[0x8c]=1; ui[0xa1]=1;
    std::array<std::uintptr_t,9> sourceVtable{};
    sourceVtable[8]=reinterpret_cast<std::uintptr_t>(&GetCommand);
    auto* source=sourceVtable.data();
    struct Message { void* source; int unused; std::uint32_t event; std::uint32_t args[8]; };
    Message message{&source,0,0x17,{0x102}};
    using Handle=bool(__thiscall*)(void*,void*,const Message&);
    auto handle=reinterpret_cast<Handle>(code(0x1e0040));
    auto invoke=[&]() { return handle(ui.data()+4,&source,message); }; // IWinProc subobject
    saveCalls=0; savedUI=nullptr; command=0x102;
    require(!invoke() && saveCalls==0,"Beta 6 low-level click must reproduce the ignored finish");
    message.event=0x287259f6; // SDK kMsgComponentActivated
    require(invoke() && saveCalls==1 && savedUI==ui.data(),
        "Component activation must reach the actual native accept/save entry exactly once");
    require(*reinterpret_cast<std::uint32_t*>(ui.data()+0xcc)==0x102,
        "Native dispatcher must record accept rather than cancel");
    layout[0x8c]=0;
    require(!invoke() && saveCalls==1,"Hidden editor must reject completion");
    layout[0x8c]=1; ui[0xa1]=0;
    require(!invoke() && saveCalls==1,"Inactive editor must reject completion");
    ui[0xa1]=1; ui[0xa2]=1;
    require(!invoke() && saveCalls==1,"Busy editor must reject completion");
    ui[0xa2]=0;
    require(invoke() && saveCalls==2,"Completion retry must work after editor becomes ready");
    command=0xffff;
    require(!invoke() && saveCalls==2,"Unknown command must not save");
    return checks;
}
