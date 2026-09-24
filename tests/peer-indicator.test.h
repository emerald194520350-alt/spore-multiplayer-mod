#pragma once
#include "../native/PeerIndicator.h"

inline void TestPeerIndicator()
{
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    for (const auto size : {std::pair<float,float>{800,600},{1920,1080},{600,900}})
    {
        const float w=size.first,h=size.second;
        require(!CoopUi::PeerArrow(w/2,h/2,w,h).visible,"Visible peer has no arrow");
        require(!CoopUi::PeerArrow(w,h,w,h).visible,"Viewport boundary is still visible");
        const auto right=CoopUi::PeerArrow(w+100,h/2,w,h);
        require(right.visible && std::abs(right.x-(w-32))<.01f && right.dx>.99f,"Right arrow stays inside viewport");
        const auto left=CoopUi::PeerArrow(-100,h/2,w,h);
        require(left.visible && std::abs(left.x-32)<.01f && left.dx<-.99f,"Left arrow direction");
        const auto top=CoopUi::PeerArrow(w/2,-100,w,h);
        require(top.visible && std::abs(top.y-32)<.01f && top.dy<-.99f,"Top arrow direction");
        const auto bottom=CoopUi::PeerArrow(w/2,h+100,w,h);
        require(bottom.visible && std::abs(bottom.y-(h-90))<.01f && bottom.dy>.99f,"Bottom arrow avoids Cell HUD");
        const auto diagonal=CoopUi::PeerArrow(w*2,-h,w,h);
        require(diagonal.visible && diagonal.x<=w-32 && diagonal.y>=31.99f && diagonal.dx>0 && diagonal.dy<0,"Diagonal arrow stays on inset edge");
    }
    require(!CoopUi::PeerArrow(NAN,0,800,600).visible,"Invalid position hides arrow");
    float x=0,y=0,invalid[16]{};
    require(!CoopUi::ProjectPeer(invalid,1,2,3,x,y),"Singular camera hides arrow");
}

inline int TestPeerProjectionAgainstEngine(std::uintptr_t base)
{
    auto require=[](bool ok,const char* message){if(!ok)throw std::runtime_error(message);};
    const unsigned char prologue[]={0x83,0xec,0x0c,0x8b,0x81,0x70,0x01,0x00,0x00};
    auto code=reinterpret_cast<unsigned char*>(base+0x3c4800);
    require(std::memcmp(code,prologue,sizeof(prologue))==0,"Native camera ray ABI mismatch");
    struct Vec {float x,y,z;};
    using CameraRay=bool(__thiscall*)(void*,float,float,Vec*,Vec*);
    const auto ray=reinterpret_cast<CameraRay>(code);
    alignas(4) unsigned char viewer[0x174]{},camera[0x94]{};
    const unsigned width=800,height=600;
    memcpy(camera+0x80,&width,4); memcpy(camera+0x84,&height,4);
    auto cameraPtr=camera; memcpy(viewer+0x170,&cameraPtr,4);
    const Vec origin{10,20,30}; memcpy(viewer+0x30,&origin,sizeof(origin));
    // Perspective with rotated screen axes and translation: world rays generated
    // by the actual game must project back to the same screen coordinates.
    const float inverse[16]={0,2,0,0, -3,0,0,0, 10,20,30,1, 0,0,-1,0};
    memcpy(viewer+0x100,inverse,sizeof(inverse));
    int checks=1;
    for (const auto pixel : {std::pair<float,float>{400,300},{-200,300},{1100,-100},{200,900}})
    {
        Vec cameraPosition{},direction{};
        require(ray(viewer,pixel.first,pixel.second,&cameraPosition,&direction),"Native camera ray failed");
        float x=0,y=0;
        require(CoopUi::ProjectPeer(inverse,cameraPosition.x+direction.x*100,
            cameraPosition.y+direction.y*100,cameraPosition.z+direction.z*100,x,y),"Peer projection failed");
        require(std::abs((x+1)*400-pixel.first)<.02f && std::abs((1-y)*300-pixel.second)<.02f,
            "Peer indicator does not match native viewport projection");
        ++checks;
    }
    float x=0,y=0;
    require(!CoopUi::ProjectPeer(inverse,10,20,40,x,y),"Behind-camera peer must not flip the arrow");
    return checks+1;
}
