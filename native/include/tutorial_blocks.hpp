#pragma once
#include <cstdint>
#include <cstddef>

namespace rebirths::tutorials {
// Script routines are identified by their instruction/operand semantics, never
// by an event ID or a relocated script address. Each supported engine's VM
// layout and optional tutorial routines are independently validated.
enum class BlockKind { None, Intro, IntroWait, FirstScreen };
template<class Memory> bool Header(Memory& memory, const unsigned char* at,
    uint32_t kind, uint32_t opcode, uint32_t count, uint32_t size, bool anyOpcode=false) {
    return memory.readable(at,16) && memory.word(at)==kind &&
        (anyOpcode || memory.word(at+4)==opcode) && memory.word(at+8)==count && memory.word(at+12)==size;
}
template<class Memory> const unsigned char* ScriptTarget(Memory& memory,
    const unsigned char* origin,const unsigned char* call) {
    if(!memory.readable(call,16)) return nullptr;
    const auto offset=memory.word(call+4);
    if(offset>=0x1000000 || reinterpret_cast<uintptr_t>(origin)>UINTPTR_MAX-offset) return nullptr;
    const auto target=origin+offset;
    return memory.readable(target,16)?target:nullptr;
}
template<class Memory> bool IntroRoutine(Memory& memory,const unsigned char* at,const unsigned char* origin) {
    if(!at || !memory.readable(at,336)) return false;
    constexpr uint32_t shapes[][3]={{0,0,16},{16,1,48},{64,0,16},{80,0,16},{96,2,80},
        {176,0,16},{192,1,48},{240,0,16},{256,1,48},{304,0,16}};
    for(const auto& shape:shapes) if(!Header(memory,at+shape[0],1,0,shape[1],shape[2],true))return false;
    if(!Header(memory,at+320,0,0,0,16) || memory.word(at+60)!=0x3ecccccd ||
        memory.word(at+152)!=238 || memory.word(at+172)!=0x3f000000 ||
        memory.word(at+236)!=0x3dcccccd || memory.word(at+300)!=0x3ecccccd)return false;
    const auto music=ScriptTarget(memory,origin,at+96),stop=ScriptTarget(memory,origin,at);
    return music && stop && Header(memory,music,0,0xbeb,3,72) && Header(memory,stop,0,0xbf4,7,240);
}
template<class Memory> bool Parameter(Memory& memory,const unsigned char* at,unsigned index,uint32_t descriptor) {
    const auto operand=at+16+index*12;
    return memory.readable(operand,12) && memory.word(operand)==descriptor &&
        memory.word(operand+4)==0x40000000 && memory.word(operand+8)==0x40000000;
}
template<class Memory> bool Constant(Memory& memory,const unsigned char* origin,
    const unsigned char* at,unsigned index,uint32_t kind,uint32_t value) {
    const auto operand=at+16+index*12;
    if(!memory.readable(operand,12))return false;
    const auto descriptor=memory.word(operand);
    if(descriptor>=0x1000000 || reinterpret_cast<uintptr_t>(origin)>UINTPTR_MAX-descriptor ||
        !Parameter(memory,at,index,descriptor))return false;
    const auto literal=origin+descriptor;
    return memory.readable(literal,20) && memory.word(literal)==kind &&
        memory.word(literal+4)==1 && memory.word(literal+8)==1 &&
        memory.word(literal+12)==4 && memory.word(literal+16)==value;
}
template<class Memory> bool FadeRoutine(Memory& memory,const unsigned char* at,const unsigned char* origin) {
    if(!at || !Header(memory,at,0,0xbbd,9,284) || !memory.readable(at,300) ||
        !Header(memory,at+284,0,0,0,16) || !Constant(memory,origin,at,0,0,1) ||
        !Parameter(memory,at,5,0xffffff00))return false;
    for(unsigned i=1;i<5;i++)if(!Constant(memory,origin,at,i,0,0))return false;
    for(unsigned i=6;i<9;i++)if(!Constant(memory,origin,at,i,1,0))return false;
    return true;
}
template<class Memory> bool FadePollRoutine(Memory& memory,const unsigned char* at,const unsigned char* origin) {
    if(!at || !Header(memory,at,0,0xbbd,9,244) || !memory.readable(at,260) ||
        !Header(memory,at+244,0,0,0,16) || !Constant(memory,origin,at,0,0,2) ||
        !Constant(memory,origin,at,4,0,0))return false;
    for(unsigned i=1;i<4;i++)if(!Parameter(memory,at,i,0xffffff00+i-1))return false;
    for(unsigned i=5;i<9;i++)if(!Constant(memory,origin,at,i,1,0))return false;
    return true;
}
template<class Memory> bool FadeWaitRoutine(Memory& memory,const unsigned char* at,const unsigned char* origin) {
    if(!at || !Header(memory,at,1,0,3,92,true) || !memory.readable(at,108) ||
        !Header(memory,at+92,0,0,0,16) || !Constant(memory,origin,at,0,0,0xffffffff) ||
        !Constant(memory,origin,at,1,0,1) ||
        (!Parameter(memory,at,2,0x60)&&!Parameter(memory,at,2,0x70)))return false;
    return FadePollRoutine(memory,ScriptTarget(memory,origin,at),origin);
}
template<class Memory> bool FirstScreenRoutine(Memory& memory,const unsigned char* at,const unsigned char* origin) {
    return at && memory.readable(at,108) && Header(memory,at,1,0,1,48,true) &&
        Constant(memory,origin,at,0,1,0x3f000000) && Header(memory,at+48,0,0xbfb,1,28) &&
        memory.word(at+64)==0xffffff00 && memory.word(at+68)==0x40000000 &&
        memory.word(at+72)==0x40000000 && Header(memory,at+76,1,0,0,16,true) && Header(memory,at+92,0,0,0,16) &&
        FadeRoutine(memory,ScriptTarget(memory,origin,at),origin) &&
        FadeWaitRoutine(memory,ScriptTarget(memory,origin,at+76),origin);
}
template<class Memory> bool IntroWaitRoutine(Memory& memory,const unsigned char* at,
    const unsigned char* origin,const unsigned char* caller) {
    if(!at || !memory.readable(at,484))return false;
    constexpr uint32_t shapes[][4]={{0,0xffffff14,2,72},{72,0xc9,3,92},{164,3,4,104},
        {268,7,1,48},{316,6,1,28},{344,0,0,16},{360,0x11e,4,124}};
    for(const auto& shape:shapes)if(!Header(memory,at+shape[0],0,shape[1],shape[2],shape[3]))return false;
    // The generic thread wait is elided only following the recognized intro.
    for(uintptr_t offset=16;offset<=256;offset+=4) {
        if(reinterpret_cast<uintptr_t>(caller)<reinterpret_cast<uintptr_t>(origin) ||
            reinterpret_cast<uintptr_t>(caller)-reinterpret_cast<uintptr_t>(origin)<offset)break;
        const auto before=caller-offset;
        if(Header(memory,before,1,0,0,16,true) && IntroRoutine(memory,ScriptTarget(memory,origin,before),origin))return true;
    }
    return false;
}
template<class Memory> bool HasFollowingScreen(Memory& memory,const unsigned char* origin,const unsigned char* caller) {
    for(uintptr_t offset=16;offset<=256;offset+=4) {
        if(reinterpret_cast<uintptr_t>(caller)>UINTPTR_MAX-offset)break;
        const auto after=caller+offset;
        if(Header(memory,after,1,0,1,48,true) && FirstScreenRoutine(memory,ScriptTarget(memory,origin,after),origin))return true;
    }
    return false;
}
template<class Memory> BlockKind IdentifyBlock(Memory& memory,const unsigned char* origin,const unsigned char* caller) {
    if(!memory.readable(caller,16) || memory.word(caller)!=1)return BlockKind::None;
    const auto target=ScriptTarget(memory,origin,caller);
    if(Header(memory,caller,1,0,0,16,true)) {
        if(IntroRoutine(memory,target,origin) && HasFollowingScreen(memory,origin,caller))return BlockKind::Intro;
        if(IntroWaitRoutine(memory,target,origin,caller) && HasFollowingScreen(memory,origin,caller))return BlockKind::IntroWait;
    }
    if(Header(memory,caller,1,0,1,48,true) && FirstScreenRoutine(memory,target,origin))return BlockKind::FirstScreen;
    return BlockKind::None;
}
}
