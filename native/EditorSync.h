#pragma once
#include <vector>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace CoopEditor
{
    // Native CommitEditHistory increments a one-based cursor after appending;
    // Undo reads mEditHistory[cursor-1], including when redo entries remain.
    inline int HistorySlot(int cursor, size_t count)
    {
        return cursor > 0 && static_cast<size_t>(cursor) <= count ? cursor-1 : -1;
    }

    using Bytes = std::vector<unsigned char>;
    constexpr size_t Header = 136, Block = 472;
    inline std::uint32_t Word(const Bytes& b,size_t offset) { std::uint32_t v; std::memcpy(&v,b.data()+offset,4); return v; }
    inline void SetWord(Bytes& b,size_t offset,std::uint32_t v) { std::memcpy(b.data()+offset,&v,4); }
    inline bool Valid(const Bytes& b)
    {
        return b.size()>=Header && Word(b,0)==0x31504353 && Word(b,4)<=512 && b.size()==Header+Word(b,4)*Block;
    }
    inline bool Identity(const Bytes& a,size_t ai,const Bytes& b,size_t bi)
    {
        const size_t ap=Header+ai*Block,bp=Header+bi*Block;
        return std::memcmp(a.data()+ap,b.data()+bp,8)==0 &&
            std::memcmp(a.data()+ap+20,b.data()+bp+20,12)==0;
    }
    inline std::vector<int> Align(const Bytes& base,const Bytes& side)
    {
        const size_t n=Word(base,4),m=Word(side,4);
        std::vector<unsigned short> lengths((n+1)*(m+1));
        auto at=[&](size_t i,size_t j)->unsigned short& {return lengths[i*(m+1)+j];};
        for(size_t i=n;i-->0;) for(size_t j=m;j-->0;)
            at(i,j)=Identity(base,i,side,j) ? 1+at(i+1,j+1) : std::max(at(i+1,j),at(i,j+1));
        std::vector<int> map(m,-1);
        size_t i=0,j=0,lastI=0,lastJ=0;
        auto gap=[&](size_t endI,size_t endJ) {
            if(endI-lastI==endJ-lastJ)
                for(size_t k=0;k<endI-lastI;++k)
                    if(Word(base,Header+(lastI+k)*Block)==Word(side,Header+(lastJ+k)*Block) &&
                        Word(base,Header+(lastI+k)*Block+4)==Word(side,Header+(lastJ+k)*Block+4))
                        map[lastJ+k]=int(lastI+k);
        };
        while(i<n && j<m)
        {
            if(Identity(base,i,side,j)) {gap(i,j);map[j]=int(i);lastI=++i;lastJ=++j;}
            else if(at(i+1,j)>=at(i,j+1)) ++i; else ++j;
        }
        gap(n,m);
        return map;
    }
    // Rebase changes made locally while the peer's accepted model was in flight.
    // Identical-length edits merge by 32-bit fields. Concurrent appended parts
    // retain both branches and remap parent/symmetry indices. Ambiguous topology
    // edits are reported as conflicts instead of producing broken part graphs.
    inline bool Merge(const Bytes& base,const Bytes& local,const Bytes& remote,Bytes& result)
    {
        if (!Valid(base)||!Valid(local)||!Valid(remote)) return false;
        if (local==base || local==remote) {result=remote;return true;}
        if (remote==base) {result=local;return true;}
        const size_t bn=Word(base,4),ln=Word(local,4),rn=Word(remote,4);
        const auto lm=Align(base,local),rm=Align(base,remote);
        std::vector<int> bl(bn,-1),br(bn,-1),bo(bn,-1),lo(ln,-1),ro(rn,-1);
        for(size_t i=0;i<ln;++i) if(lm[i]>=0) bl[lm[i]]=int(i);
        for(size_t i=0;i<rn;++i) if(rm[i]>=0) br[rm[i]]=int(i);
        struct Record {int base,local,remote;};
        std::vector<Record> records;
        for(size_t i=0;i<rn;++i)
        {
            const int b=rm[i]; if(b>=0 && bl[b]<0) continue;
            ro[i]=int(records.size());
            if(b>=0) {bo[b]=ro[i];lo[bl[b]]=ro[i];}
            records.push_back({b,b>=0?bl[b]:-1,int(i)});
        }
        for(size_t i=0;i<ln;++i) if(lm[i]<0)
        {lo[i]=int(records.size());records.push_back({-1,int(i),-1});}
        if(records.size()>512) return false;
        result.assign(remote.begin(),remote.begin()+Header);
        // Last local edit wins only for the same scalar field; independent
        // fields and parts from both players remain in the merged model.
        for(size_t p=8;p<Header;p+=4)
            if(Word(local,p)!=Word(base,p)) SetWord(result,p,Word(local,p));
        auto reference=[](const Bytes& bytes,size_t p,const std::vector<int>& map)->std::uint32_t {
            const auto index=Word(bytes,p);
            if(index==UINT32_MAX) return index;
            return index<map.size() && map[index]>=0 ? std::uint32_t(map[index]) : UINT32_MAX-1;
        };
        for(const auto& record:records)
        {
            const size_t start=result.size(); result.resize(start+Block);
            const auto& source=record.remote>=0?remote:local;
            const size_t sp=Header+size_t(record.remote>=0?record.remote:record.local)*Block;
            std::memcpy(result.data()+start,source.data()+sp,Block);
            for(size_t field=0;field<Block;field+=4)
            {
                const bool ref=field==8 || field==12;
                auto value=ref?reference(source,sp+field,record.remote>=0?ro:lo):Word(source,sp+field);
                if(record.base>=0)
                {
                    const size_t bp=Header+size_t(record.base)*Block+field,lp=Header+size_t(record.local)*Block+field;
                    const auto bv=ref?reference(base,bp,bo):Word(base,bp);
                    const auto lv=ref?reference(local,lp,lo):Word(local,lp);
                    if(lv!=bv) value=lv;
                }
                if(ref && value==UINT32_MAX-1) return false;
                SetWord(result,start+field,value);
            }
        }
        SetWord(result,4,std::uint32_t(records.size()));
        return true;
    }
}
