// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>

namespace CoopHistory
{
    using Event = std::array<std::uint32_t,30>;
    using Key = std::array<std::uint32_t,3>;
    struct Snapshot { Key model{}; std::vector<Event> events; };

    template<class Readable>
    inline bool ReadList(unsigned char* sentinel,std::vector<Event>& result,Readable readable)
    {
        std::vector<Event> events;
        if (!readable(sentinel,8)) return false;
        auto node=*reinterpret_cast<unsigned char**>(sentinel);
        while (node!=sentinel)
        {
            if (events.size()>=320 || !readable(node,0x28)) return false;
            auto record=node+8;
            auto begin=*reinterpret_cast<unsigned char**>(record+0xc);
            auto end=*reinterpret_cast<unsigned char**>(record+0x10);
            if (reinterpret_cast<std::uintptr_t>(end)-reinterpret_cast<std::uintptr_t>(begin)!=108 ||
                !readable(begin,108)) return false;
            Event event;
            std::memcpy(event.data(),record,12); std::memcpy(event.data()+3,begin,108);
            events.push_back(event); node=*reinterpret_cast<unsigned char**>(node);
        }
        result=std::move(events); return true;
    }

    inline bool Decode(const std::vector<unsigned char>& bytes, Snapshot& result)
    {
        if (bytes.size()<20) return false;
        std::uint32_t header[5]; std::memcpy(header,bytes.data(),20);
        if (header[0]!=0x31545348 || header[4]>320 || bytes.size()!=20+120*size_t(header[4])) return false;
        Snapshot decoded; decoded.model={header[1],header[2],header[3]};
        decoded.events.resize(header[4]);
        if (header[4]) std::memcpy(decoded.events.data(),bytes.data()+20,header[4]*120);
        // The first two payload values are the event ID and evolutionary time.
        for (const auto& event:decoded.events)
        {
            float time; std::memcpy(&time,&event[4],4);
            if (!std::isfinite(time)) return false;
        }
        result=std::move(decoded); return true;
    }

    inline std::vector<unsigned char> Encode(const Snapshot& data)
    {
        if (data.events.size()>320) return {};
        std::uint32_t header[]={0x31545348,data.model[0],data.model[1],data.model[2],std::uint32_t(data.events.size())};
        std::vector<unsigned char> bytes(20+120*data.events.size());
        std::memcpy(bytes.data(),header,20);
        if (!data.events.empty()) std::memcpy(bytes.data()+20,data.events.data(),data.events.size()*120);
        return bytes;
    }

    inline void Remap(Event& event,const Key& source,const Key& local)
    {
        // Native timeline resource triples store type before instance.
        for (size_t offset:{3u,6u,13u,16u})
        {
            auto* key=event.data()+3+offset;
            if (source[0] && key[0]==source[1] && key[1]==source[0] && key[2]==source[2])
            { key[0]=local[1]; key[1]=local[0]; key[2]=local[2]; }
        }
    }
}
