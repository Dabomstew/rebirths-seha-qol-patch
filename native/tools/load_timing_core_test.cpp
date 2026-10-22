#include "load_timing_core.hpp"
#include <cstdio>
#include <stdexcept>
#include <array>
#include <vector>
#include <algorithm>
using rebirths::LoadTimingCore;
void Require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
int main() {
 try {
    LoadTimingCore timing(50);
    timing.Begin(0, 1, 3, true, true, 10); timing.End(0, 1, 3, true, true, 10, 12);
    Require(timing.Completed() == 2, "first native blocked poll");
    timing.Begin(1, 2, 4, true, true, 14); timing.End(1, 2, 4, true, true, 14, 16);
    timing.Begin(0, 1, 3, true, true, 20); timing.End(0, 1, 3, true, true, 20, 22);
    timing.Begin(1, 2, 4, true, true, 24); timing.End(1, 2, 4, false, true, 24, 26);
    timing.Begin(0, 1, 3, true, true, 30); timing.End(0, 1, 3, false, true, 30, 32);
    Require(timing.Completed() == 12, "overlapping owners count once, late owner poll fills earlier holes in union10..22; terminal-ready gaps excluded");
    timing.Begin(0, 1, 3, true, true, 40); timing.End(0, 1, 3, false, true, 40, 42);
    Require(timing.Completed() == 12, "nonresource/fixed delay is not counted");
    timing.Begin(0, 1, 3, true, true, 50); timing.End(0, 1, 3, true, true, 50, 52);
    timing.Cancel(0); timing.Begin(0, 1, 3, true, true, 60);
    Require(timing.Completed() == 14, "new native epoch cancels address reuse");
    timing.End(0, 1, 3, true, true, 60, 62);
    timing.Begin(0, 99, 3, true, true, 70); timing.End(0, 99, 3, true, true, 70, 72);
    Require(timing.Completed() == 18, "changed owner cannot bridge unrelated gap");
    timing.Begin(0, 99, 3, true, false, 80); timing.End(0, 99, 3, true, false, 80, 82);
    timing.Begin(0, 99, 3, true, true, 90); timing.End(0, 99, 3, true, true, 90, 92);
    Require(timing.Completed() == 20, "focus loss does not bridge inactive time");
    timing.Begin(0, 99, 3, true, true, 200); timing.End(0, 99, 3, true, true, 200, 202);
    Require(timing.Completed() == 22, "unobserved long gap discarded with automatic recovery");
    timing.Begin(0, 99, 4, true, true, 210); timing.End(0, 99, 4, true, true, 210, 212);
    Require(timing.Completed() == 24, "changed stage does not bridge gap");
    timing.Begin(0, 99, 4, true, true, 211);
    Require(timing.Faulted() && timing.Completed() == 24, "clock rollback fails open");
    timing.End(0, 99, 4, true, true, 212, 220);
    Require(timing.Completed() == 24, "fault cannot keep accumulating");
    timing.Reset(50); timing.End(0, 1, 3, true, true, 100, 99);
    Require(timing.Faulted() && timing.Completed() == 0, "malformed interval rejected");
    for(unsigned negative=0;negative<3;++negative){
        timing.Reset(50);timing.Begin(0,1,3,true,true,10);timing.End(0,1,3,true,true,10,12);
        timing.Begin(0,1,3,true,true,20);
        Require(timing.Completed()==2&&timing.Settled()==2&&timing.Frontier()==12,"Begin never publishes unconfirmed old-owner gap");
        timing.End(0,1,negative==1?4:3,negative!=0,negative!=2,20,22);
        Require(timing.Completed()==2&&!timing.Faulted(),"terminal-ready, changed phase and lost-focus polls discard pending gap");
    }
    timing.Reset(50);timing.Begin(0,1,3,true,true,10);timing.End(0,1,3,true,true,10,12);
    timing.Begin(0,1,3,true,true,62);timing.Begin(1,2,3,true,true,64);timing.End(1,2,3,true,true,64,66);
    Require(timing.Frontier()==12&&timing.Settled()==2,"pending gap cannot expire past a published frontier while another owner completes");
    timing.End(0,1,3,true,true,62,70);Require(timing.Frontier()==66&&timing.Settled()==56,"late pending-gap confirmation preserves exact prefix delta bound");
    timing.Reset(50);timing.Begin(0,1,3,true,true,0);Require(timing.Faulted(),"zero native clock rejected by reducer");
    timing.Reset(50);
    struct OracleAnchor { uint32_t owner=0,phase=0; uint64_t qpc=1; };
    std::array<OracleAnchor,3> oracle{};
    std::vector<bool> excluded(100000, false);
    uint32_t random = 0x9103276;
    auto draw = [&]() { random ^= random << 13; random ^= random >> 17; random ^= random << 5; return random; };
    auto include = [&](uint64_t begin,uint64_t end) { for (auto tick=begin;tick<end;++tick) excluded.at(static_cast<size_t>(tick))=true; };
    uint64_t qpc=1;
    for (unsigned step=0;step<3000;++step) {
        const unsigned kind=draw()%3;
        if (draw()%7==0) { timing.Cancel(kind); oracle[kind]={}; }
        const uint32_t owner=1+draw()%3,phase=3+draw()%2;
        const bool focused=draw()%5!=0,blocked=draw()%3!=0;
        qpc+=draw()%8; if(draw()%31==0) qpc+=70;
        const auto old=oracle[kind];oracle[kind]={};
        timing.Begin(kind,owner,phase,true,focused,qpc);
        const bool confirmGap=focused&&old.owner==owner&&old.phase==phase&&qpc-old.qpc<=50;
        const auto began=qpc;qpc+=draw()%5;
        timing.End(kind,owner,phase,blocked,focused,began,qpc);
        if(blocked&&focused) {include(confirmGap?old.qpc:began,qpc);oracle[kind]={owner,phase,qpc};}
        const auto expected=static_cast<uint64_t>(std::count(excluded.begin(),excluded.end(),true));
        const auto settled=static_cast<uint64_t>(std::count(excluded.begin(),excluded.begin()+static_cast<size_t>(timing.Frontier()),true));
        Require(timing.Settled()==settled,"published frontier matches independent historical prefix");
        Require(!timing.Faulted() && timing.Completed()==expected,"3000 seeded interleaved reports versus independent tick-set union");
    }
    std::puts("Load timing confirmed union, native epochs, focus, gaps and failure recovery pass"); return 0;
 } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
