#include <windows.h>
static unsigned memoryQueries=0;
static SIZE_T CountedQuery(LPCVOID pointer,PMEMORY_BASIC_INFORMATION memory,SIZE_T size){++memoryQueries;return VirtualQuery(pointer,memory,size);}
#define VirtualQuery CountedQuery
#include "../src/tutorial_skip.cpp"
#undef VirtualQuery
#include <cassert>
#include <cstdio>
#include <vector>
#include <fstream>
#include <iterator>

namespace rebirths {
void Log(const char*, ...) noexcept {}
bool RetargetCalls(const Context&,const CallSite*,size_t) noexcept { return false; }
}
namespace {
bool success=true;
unsigned transactions=0;
unsigned registrations=0,flags=0;
unsigned prepared=0;
uint32_t argumentId=4;
bool registrationSucceeds=true;
void __cdecl Prepare(unsigned char* record){++prepared;*reinterpret_cast<uint32_t**>(record+8)=&argumentId;}
uint32_t __cdecl Register(uint32_t id,unsigned char add) {assert(id==4&&add==1);++registrations;return registrationSucceeds?1:0;}
void __cdecl Flag(uint32_t id,unsigned char add) {assert(id==0x16&&add==1);++flags;}
uint32_t Invoke(rebirths::GameId game,uint32_t id) {
    void* target=rebirths::tutorials::Replacement(game);
    const uint32_t ebxId=game==rebirths::GameId::Rebirth1?id:0x1234;
    const uint32_t ediId=game==rebirths::GameId::Rebirth1?0x5678:id;
    uint32_t result,ebxAfter,ediAfter;
    __asm {
        push ebx
        push edi
        mov ebx,ebxId
        mov edi,ediId
        call target
        mov result,eax
        mov ebxAfter,ebx
        mov ediAfter,edi
        pop edi
        pop ebx
    }
    assert(ebxAfter==ebxId&&ediAfter==ediId);
    return result;
}
bool Calls(const rebirths::Context& context,const rebirths::CallSite* sites,size_t count) noexcept {
    using namespace rebirths::tutorials;
    assert(count==(BlockFor(selected->game)?2u:1u) && sites[0].rva==selected->callRva && sites[0].expected==selected->call);
    if(count==2)assert(sites[1].rva==BlockFor(selected->game)->prepareCallRva && sites[1].replacement==reinterpret_cast<void*>(BlockArguments));
    assert(sites[0].replacement==Replacement(selected->game));
    assert(context.spec.id==selected->game);++transactions;return success;
}
// Model the independently exported VM frame: EBX and [EBP+0xc] cache the IP.
// The outer helper retains its own ordinary C ABI while the scoped bridge may
// synchronize those interpreter caches after omitting an optional CALL.
__declspec(naked) uint32_t __cdecl InvokeArguments(unsigned char*,unsigned char*,uint32_t*) {
    __asm {
        push ebp
        mov ebp,esp
        push ebx
        push edi
        mov edx,dword ptr [ebp+0ch]
        mov eax,dword ptr [ebp+8]
        mov ebx,dword ptr [eax]
        mov dword ptr [ebp+0ch],ebx
        push eax
        call rebirths::tutorials::BlockArguments
        add esp,4
        mov ecx,dword ptr [ebp+10h]
        mov eax,dword ptr [ebp+0ch]
        mov dword ptr [ecx],eax
        mov eax,ebx
        pop edi
        pop ebx
        pop ebp
        ret
    }
}
void TestBlocks(unsigned char* image,const rebirths::tutorials::Profile& profile) {
    using namespace rebirths::tutorials;
    const auto* bp=BlockFor(profile.game);if(!bp)return;
    std::vector<unsigned char> code(8192),vm(0x4014),record(0x4d4),adv(0x9000);
    auto word=[&](size_t offset,uint32_t value){std::memcpy(code.data()+offset,&value,4);};
    auto header=[&](size_t offset,uint32_t kind,uint32_t op,uint32_t count,uint32_t size){word(offset,kind);word(offset+4,op);word(offset+8,count);word(offset+12,size);};
    constexpr uint32_t intro=0x100,wait=0x400,first=0x800,music=0x1000,stop=0x1100,main=0x1800;
    constexpr uint32_t shapes[][3]={{0,0,16},{16,1,48},{64,0,16},{80,0,16},{96,2,80},{176,0,16},{192,1,48},{240,0,16},{256,1,48},{304,0,16}};
    for(const auto& s:shapes)header(intro+s[0],1,music,s[1],s[2]);
    header(intro+320,0,0,0,16);word(intro+4,stop);word(intro+60,0x3ecccccd);word(intro+152,238);word(intro+172,0x3f000000);word(intro+236,0x3dcccccd);word(intro+300,0x3ecccccd);
    header(music,0,0xbeb,3,72);header(stop,0,0xbf4,7,240);
    constexpr uint32_t fade=0x1200,poll=0x1400,fadeWait=0x1600;
    auto parameter=[&](uint32_t at,unsigned index,uint32_t descriptor){word(at+16+index*12,descriptor);word(at+20+index*12,0x40000000);word(at+24+index*12,0x40000000);};
    auto constant=[&](uint32_t at,unsigned index,uint32_t& cursor,uint32_t kind,uint32_t value){parameter(at,index,cursor);word(cursor,kind);word(cursor+4,1);word(cursor+8,1);word(cursor+12,4);word(cursor+16,value);cursor+=20;};
    header(fade,0,0xbbd,9,284);header(fade+284,0,0,0,16);uint32_t cursor=fade+124;
    constant(fade,0,cursor,0,1);for(unsigned i=1;i<5;i++)constant(fade,i,cursor,0,0);parameter(fade,5,0xffffff00);for(unsigned i=6;i<9;i++)constant(fade,i,cursor,1,0);
    header(poll,0,0xbbd,9,244);header(poll+244,0,0,0,16);cursor=poll+124;
    constant(poll,0,cursor,0,2);for(unsigned i=1;i<4;i++)parameter(poll,i,0xffffff00+i-1);constant(poll,4,cursor,0,0);for(unsigned i=5;i<9;i++)constant(poll,i,cursor,1,0);
    header(fadeWait,1,poll,3,92);header(fadeWait+92,0,0,0,16);cursor=fadeWait+52;
    constant(fadeWait,0,cursor,0,0xffffffff);constant(fadeWait,1,cursor,0,1);parameter(fadeWait,2,0x70);
    constexpr uint32_t waits[][4]={{0,0xffffff14,2,72},{72,0xc9,3,92},{164,3,4,104},{268,7,1,48},{316,6,1,28},{344,0,0,16},{360,0x11e,4,124}};
    for(const auto& s:waits)header(wait+s[0],0,s[1],s[2],s[3]);
    header(first,1,fade,1,48);cursor=first+28;constant(first,0,cursor,1,0x3f000000);header(first+48,0,0xbfb,1,28);word(first+64,0xffffff00);word(first+68,0x40000000);word(first+72,0x40000000);header(first+76,1,fadeWait,0,16);header(first+92,0,0,0,16);
    header(main,1,intro,0,16);header(main+16,1,wait,0,16);header(main+32,1,first,1,48);header(main+80,0,7,1,48);
    BlockMemory memory;assert(IdentifyBlock(memory,code.data(),code.data()+main)==BlockKind::Intro);
    assert(IdentifyBlock(memory,code.data(),code.data()+main+16)==BlockKind::IntroWait);
    assert(IdentifyBlock(memory,code.data(),code.data()+main+32)==BlockKind::FirstScreen);
    word(intro+152,237);assert(IdentifyBlock(memory,code.data(),code.data()+main)==BlockKind::None);assert(IdentifyBlock(memory,code.data(),code.data()+main+16)==BlockKind::None);word(intro+152,238);
    word(first+48+4,0xbfa);assert(IdentifyBlock(memory,code.data(),code.data()+main+32)==BlockKind::None);word(first+48+4,0xbfb);
    word(main+32+8,0);assert(IdentifyBlock(memory,code.data(),code.data()+main)==BlockKind::None);assert(IdentifyBlock(memory,code.data(),code.data()+main+16)==BlockKind::None);word(main+32+8,1);
    auto pointer=[](unsigned char* at,unsigned char* value){std::memcpy(at,&value,4);};
    const auto oldAdv=*reinterpret_cast<unsigned char**>(image+discovery->advRva);
    DWORD slotProtection,discard;assert(VirtualProtect(image+discovery->advRva,4,PAGE_READWRITE,&slotProtection));
    pointer(image+discovery->advRva,adv.data());assert(VirtualProtect(image+discovery->advRva,4,slotProtection,&discard));adv[bp->stateOffset]=6;
    pointer(vm.data()+0x4004,record.data());pointer(vm.data()+0xbfb*4,image+profile.commandRva);pointer(vm.data()+0xbeb*4,image+bp->musicRva);pointer(vm.data()+0xbf4*4,image+bp->stopRva);pointer(vm.data()+0xbbd*4,image+bp->fadeRva);pointer(record.data()+4,code.data());
    originalArguments=Prepare;
    const auto entry=main+(bp->hasIntro?0:32);
    auto reset=[&](){pointer(record.data(),code.data()+entry);pointer(vm.data()+0x400c,code.data()+entry);*reinterpret_cast<uint32_t*>(record.data()+0x4cc)=0;*reinterpret_cast<uint32_t*>(record.data()+0x4d0)=0;prepared=0;};
    reset();enabled=false;PrepareBlock(record.data(),vm.data());assert(prepared==1&&*reinterpret_cast<unsigned char**>(record.data())==code.data()+entry);
    enabled=true;reset();adv[bp->stateOffset]=5;PrepareBlock(record.data(),vm.data());assert(prepared==1);adv[bp->stateOffset]=6;
    reset();*reinterpret_cast<uint32_t*>(adv.data()+0x48)=0x800;PrepareBlock(record.data(),vm.data());assert(prepared==1);*reinterpret_cast<uint32_t*>(adv.data()+0x48)=0;
    reset();pointer(vm.data()+0x4004,nullptr);PrepareBlock(record.data(),vm.data());assert(prepared==1);pointer(vm.data()+0x4004,record.data());
    reset();pointer(vm.data()+0xbfb*4,image+profile.commandRva+1);PrepareBlock(record.data(),vm.data());assert(prepared==1);pointer(vm.data()+0xbfb*4,image+profile.commandRva);
    reset();*reinterpret_cast<uint32_t*>(adv.data()+0x10)=1;memoryQueries=0;
    for(unsigned i=0;i<10000;i++)PrepareBlock(record.data(),vm.data());
    assert(prepared==10000&&memoryQueries==0);*reinterpret_cast<uint32_t*>(adv.data()+0x10)=0;
    reset();pointer(record.data(),code.data()+main+80);pointer(vm.data()+0x400c,code.data()+main+80);memoryQueries=0;
    for(unsigned i=0;i<10000;i++)PrepareBlock(record.data(),vm.data());
    assert(prepared==10000&&memoryQueries==0);
    reset();pointer(vm.data()+0x400c,code.data()+main+80);PrepareBlock(record.data(),vm.data());assert(prepared==1);
    auto firstOnly=[&](){reset();pointer(record.data(),code.data()+main+32);pointer(vm.data()+0x400c,code.data()+main+32);};
    firstOnly();adv[bp->stateOffset]=0;PrepareBlock(record.data(),vm.data());assert(prepared==(bp->idleAllowed?2u:1u));adv[bp->stateOffset]=6;
    firstOnly();uint32_t unchangedCache=0;enabled=false;
    const auto unchanged=InvokeArguments(record.data(),vm.data(),&unchangedCache);
    assert(unchanged==reinterpret_cast<uint32_t>(code.data()+main+32)&&unchangedCache==unchanged&&prepared==1);enabled=true;
    firstOnly();argumentId=0;const auto invalidBefore=registrations;PrepareBlock(record.data(),vm.data());assert(prepared==1&&registrations==invalidBefore);argumentId=4;
    firstOnly();registrationSucceeds=false;PrepareBlock(record.data(),vm.data());assert(prepared==1&&*reinterpret_cast<unsigned char**>(record.data())==code.data()+main+32);registrationSucceeds=true;
    const auto save=*reinterpret_cast<unsigned char**>(image+discovery->saveRva);
    *reinterpret_cast<uint32_t*>(save+discovery->countOffset)=1;save[discovery->countOffset+4]=1;save[discovery->countOffset+5]=4;
    firstOnly();registrationSucceeds=false;PrepareBlock(record.data(),vm.data());
    assert(prepared==2&&*reinterpret_cast<unsigned char**>(record.data())==code.data()+main+80);registrationSucceeds=true;
    *reinterpret_cast<uint32_t*>(save+discovery->countOffset)=0;
    reset();const auto before=registrations;uint32_t savedCache=0;
    const auto cached=InvokeArguments(record.data(),vm.data(),&savedCache);
    assert(prepared==(bp->hasIntro?4u:2u)&&registrations==before+1);
    assert(*reinterpret_cast<unsigned char**>(record.data())==code.data()+main+80);
    assert(*reinterpret_cast<unsigned char**>(vm.data()+0x400c)==code.data()+main+80);
    assert(cached==reinterpret_cast<uint32_t>(code.data()+main+80)&&savedCache==cached);
    // A required instruction immediately following the bypass yields in place.
    // Execute its independently verified native cache/local-reset tail.
    *reinterpret_cast<uint32_t*>(record.data()+0x4d0)=0x78;
    unsigned char* r=record.data();
    __asm {
        push ebx
        push edi
        mov ebx,cached
        mov edi,r
        inc dword ptr [edi+4cch]
        cmp ebx,dword ptr [edi]
        je waiting
        mov dword ptr [edi+4cch],0
        mov dword ptr [edi+4d0h],0
      waiting:
        pop edi
        pop ebx
    }
    assert(*reinterpret_cast<uint32_t*>(record.data()+0x4d0)==0x78);
    // A malformed repeated sequence cannot cause an unbounded preparation loop.
    for(unsigned i=0;i<9;i++)header(main+i*16,1,intro,0,16);
    header(main+144,1,first,1,48);header(main+192,0,7,1,48);
    reset();PrepareBlock(record.data(),vm.data());
    if(bp->hasIntro)assert(prepared==9&&*reinterpret_cast<unsigned char**>(record.data())==code.data()+main+128);
    else assert(prepared==1&&*reinterpret_cast<unsigned char**>(record.data())==code.data()+entry);
    // Uncertain thread wait outside the tutorial sequence must forward.
    header(main,1,music,0,16);header(main+16,1,wait,0,16);header(main+32,1,first,1,48);
    for(unsigned i=3;i<9;i++)header(main+i*16,0,7,0,16);
    assert(IdentifyBlock(memory,code.data(),code.data()+main+16)==BlockKind::None);
    assert(VirtualProtect(image+discovery->advRva,4,PAGE_READWRITE,&discard));pointer(image+discovery->advRva,oldAdv);assert(VirtualProtect(image+discovery->advRva,4,slotProtection,&discard));
}
void TestOriginalScript(const char* path,const std::vector<uint32_t>& sites) {
    using namespace rebirths::tutorials;
    std::ifstream file(path,std::ios::binary);assert(file);
    std::vector<unsigned char> code(std::istreambuf_iterator<char>(file),{});
    assert(code.size()>108&&code.size()<=0x1000000);BlockMemory memory;
    for(size_t i=0;i<sites.size();i++) {
        assert(sites[i]+48<code.size());
        const auto expected=sites.size()==1?BlockKind::FirstScreen:i==0?BlockKind::Intro:i==1?BlockKind::IntroWait:BlockKind::FirstScreen;
        assert(IdentifyBlock(memory,code.data(),code.data()+sites[i])==expected);
    }
    const auto first=ScriptTarget(memory,code.data(),code.data()+sites.back());
    const auto fade=ScriptTarget(memory,code.data(),first);
    auto* mode=code.data()+memory.word(fade+16)+16;
    const auto modeValue=memory.word(mode);uint32_t changed=9;std::memcpy(mode,&changed,4);
    assert(IdentifyBlock(memory,code.data(),code.data()+sites.back())==BlockKind::None);
    std::memcpy(mode,&modeValue,4);
    const auto wait=ScriptTarget(memory,code.data(),first+76),poll=ScriptTarget(memory,code.data(),wait);
    auto* opcode=const_cast<unsigned char*>(poll+4);const auto originalOpcode=memory.word(opcode);changed=0xcd0;std::memcpy(opcode,&changed,4);
    for(auto site:sites)assert(IdentifyBlock(memory,code.data(),code.data()+site)==BlockKind::None);
    std::memcpy(opcode,&originalOpcode,4);
    assert(IdentifyBlock(memory,code.data(),code.data()+sites.back())==BlockKind::FirstScreen);
}
}
int main(int argc,char** argv) {
    using namespace rebirths;
    using namespace rebirths::tutorials;
    constexpr size_t size=0x500000;
    auto* manager=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x2000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(manager);
    auto* save=static_cast<unsigned char*>(VirtualAlloc(nullptr,0xd0000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(save);
    for(const auto& profile:Profiles) for(int relocation=0;relocation<2;relocation++) {
        auto* first=static_cast<unsigned char*>(VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(first);
        auto* second=static_cast<unsigned char*>(VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));assert(second&&second!=first);
        auto* image=relocation?second:first;auto* unused=relocation?first:second;
        enabled=false;original=nullptr;transactions=0;registrations=flags=0;
        const auto& effect=*DiscoveryFor(profile.game);
        std::memset(save,0,0xd0000);
        // Execute the actual native getter machine code against relocated slots.
        unsigned char getter[]={0x8b,0x0d,0,0,0,0,0x85,0xc9,0x75,0x03,0x32,0xc0,0xc3,0x33,0xc0,0x39,0x01,0x0f,0x95,0xc0,0xc3};
        const uint32_t slot=reinterpret_cast<uint32_t>(image+profile.managerRva);std::memcpy(getter+2,&slot,4);
        std::memcpy(image+profile.predicateRva,getter,sizeof(getter));
        std::memcpy(image+profile.callRva,profile.call.data(),5);
        const unsigned char rb1Id[]={0x8b,0x18,0x83,0xc4,0x08},otherId[]={0x83,0xc4,0x08,0x8b,0x38};
        std::memcpy(image+profile.callRva-5,profile.game==GameId::Rebirth1?rb1Id:otherId,5);
        const auto catalog=DiscoveryBytes(effect,image);std::memcpy(image+effect.registerRva,catalog.data(),catalog.size());
        const auto query=SeenBytes(*SeenFor(profile.game),effect,image);std::memcpy(image+SeenFor(profile.game)->queryRva,query.data(),query.size());
        // Execute the real relocated query, with a valid fixture catalog lookup.
        const unsigned char lookup[]={0xb8,1,0,0,0,0xc3};std::memcpy(image+effect.lookupRva,lookup,sizeof(lookup));
        if(const auto* bp=BlockFor(profile.game)) {
            const auto site=ArgumentCallBytes(*bp),body=ArgumentBodyBytes(*bp);
            std::memcpy(image+bp->prepareCallRva,site.data(),site.size());std::memcpy(image+bp->prepareRva,body.data(),body.size());
            std::memcpy(image+bp->cacheSaveRva,CacheSaveBytes.data(),CacheSaveBytes.size());
            std::memcpy(image+bp->cacheTailRva,CacheTailBytes.data(),CacheTailBytes.size());
        }
        *reinterpret_cast<unsigned char**>(image+effect.saveRva)=save;
        *reinterpret_cast<unsigned char**>(image+effect.advRva)=manager+0x200;
        *reinterpret_cast<uint32_t*>(manager+0x248)=0;
        if(effect.flagRva) {
            unsigned char bytes[]={0x55,0x8b,0xec,0x8b,0x4d,0x08,0x8b,0xd1,0xa1,0,0,0,0,0x83,0xe1,0x1f,0xc1,0xea,0x05,0x8d,0x14,0x90};
            const uint32_t saveSlot=reinterpret_cast<uint32_t>(image+effect.saveRva);std::memcpy(bytes+9,&saveSlot,4);
            std::memcpy(image+effect.flagRva,bytes,sizeof(bytes));
        }
        const unsigned char exit[]={0x84,0xc0,0x75,0x09,0x5f,0xb8,1,0,0,0,0x5b};std::memcpy(image+profile.callRva+5,exit,sizeof(exit));
        *reinterpret_cast<unsigned char**>(image+profile.managerRva)=manager;
        *reinterpret_cast<uint32_t*>(manager)=1;*reinterpret_cast<uint32_t*>(manager+0x1070)=0;
        DWORD protection;assert(VirtualProtect(image,size,PAGE_EXECUTE_READ,&protection));
        Context context{reinterpret_cast<HMODULE>(image),GameSpecFor(profile.game),L"",L""};TutorialPatchOps ops{Calls};
        success=false;assert(!InstallSkipTutorials(context,&ops));assert(transactions==1&&!enabled);
        assert(Invoke(profile.game,4)==1); // failed transaction forwards
        success=true;assert(InstallSkipTutorials(context,&ops));
        registerTutorial=Register;setFlag=effect.flagRva?Flag:nullptr;
        assert(Invoke(profile.game,4)==0);assert(registrations==1);assert(flags==(effect.flagRva?1u:0u));
        assert(Invoke(profile.game,0)==1);assert(registrations==1);
        assert(Invoke(profile.game,0x10000)==1);assert(registrations==1);
        *reinterpret_cast<uint32_t*>(manager+0x248)=0x800;
        assert(Invoke(profile.game,4)==1);assert(registrations==1); // preserve native suppression
        *reinterpret_cast<uint32_t*>(manager+0x248)=0;
        *reinterpret_cast<uint32_t*>(save+effect.countOffset)=129;assert(Invoke(profile.game,4)==1);assert(registrations==1);
        *reinterpret_cast<uint32_t*>(save+effect.countOffset)=0;
        // Native registration returns zero for an already-discovered ID.
        // A repeated automatic tutorial must still take the no-display path.
        registrationSucceeds=false;*reinterpret_cast<uint32_t*>(save+effect.countOffset)=1;
        save[effect.countOffset+4]=1;save[effect.countOffset+5]=4;
        assert(Invoke(profile.game,4)==0);
        registrationSucceeds=true;*reinterpret_cast<uint32_t*>(save+effect.countOffset)=0;
        TestBlocks(image,profile);
        registrations=1;flags=effect.flagRva?1u:0u;
        assert(original()==1); // unpatched shared getter/manual Help unchanged
        *reinterpret_cast<uint32_t*>(manager+0x1070)=7;assert(Invoke(profile.game,4)==1);assert(registrations==1);
        *reinterpret_cast<uint32_t*>(manager)=0;assert((Invoke(profile.game,4)&255)==0);assert(registrations==1);
        enabled=false;*reinterpret_cast<uint32_t*>(manager)=1;
        assert(!InstallSkipTutorials(context,&ops)); // existing Help refuses late install
        *reinterpret_cast<uint32_t*>(manager+0x1070)=0;
        assert(VirtualProtect(image,size,PAGE_READWRITE,&protection));image[profile.callRva+5]^=1;
        const auto before=transactions;assert(!InstallSkipTutorials(context,&ops));assert(transactions==before);
        if(const auto* bp=BlockFor(profile.game)) {
            image[profile.callRva+5]^=1;image[bp->prepareCallRva+11]^=1;
            assert(!InstallSkipTutorials(context,&ops));assert(transactions==before);
            image[bp->prepareCallRva+11]^=1;
            image[bp->cacheSaveRva]^=1;assert(!InstallSkipTutorials(context,&ops));assert(transactions==before);image[bp->cacheSaveRva]^=1;
            image[bp->cacheTailRva]^=1;assert(!InstallSkipTutorials(context,&ops));assert(transactions==before);image[bp->cacheTailRva]^=1;
        }
        else image[profile.callRva+5]^=1;
        image[SeenFor(profile.game)->queryRva+80]^=1;
        assert(!InstallSkipTutorials(context,&ops));assert(transactions==before);
        enabled=false;original=nullptr;selected=nullptr;moduleBase=nullptr;
        assert(VirtualFree(image,0,MEM_RELEASE));assert(VirtualFree(unused,0,MEM_RELEASE));
    }
    assert(VirtualFree(manager,0,MEM_RELEASE));
    assert(VirtualFree(save,0,MEM_RELEASE));
    assert(argc==1||argc==5);
    if(argc==5) {
        TestOriginalScript(argv[1],{0x1b75c});TestOriginalScript(argv[2],{0x20e48});
        TestOriginalScript(argv[3],{0x15e678,0x15e754,0x15e764});TestOriginalScript(argv[4],{0x24228,0x24238,0x24248});
        std::puts("four independently identified original scripts: utility matcher and unsafe-helper refusal pass");
    }
    std::puts("tutorial skip: four profiles, x86 bridges, discovery effects, bounded block advancement, ownership/forwarding, relocation and byte refusal pass");
}
