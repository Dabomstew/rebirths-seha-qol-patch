#include "tutorial_skip.hpp"
#include "tutorial_profiles.hpp"
#include "tutorial_blocks.hpp"
#include <atomic>
#include <algorithm>
#include <cstring>

namespace rebirths::tutorials {
using Predicate = uint32_t (__cdecl*)();
Predicate original = nullptr;
const Profile* selected = nullptr;
const DiscoveryProfile* discovery = nullptr;
unsigned char* moduleBase = nullptr;
std::atomic<bool> enabled{false};
using RegisterTutorial = uint32_t (__cdecl*)(uint32_t, unsigned char);
using SetFlag = void (__cdecl*)(uint32_t, unsigned char);
RegisterTutorial registerTutorial = nullptr;
SetFlag setFlag = nullptr;
using SeenTutorial=uint32_t (__cdecl*)(uint32_t);
SeenTutorial seenTutorial=nullptr;
using PrepareArguments = void (__cdecl*)(unsigned char*);
PrepareArguments originalArguments=nullptr;
const BlockProfile* block=nullptr;
std::array<unsigned char,20> ArgumentCallBytes(const BlockProfile& profile) {
    std::array<unsigned char,20> bytes{0xe8,0,0,0,0,0x8b,0x55,0x08,0x83,0xc4,0x04,0x0f,0x57,0xd2,0x8b,0x8a,0x0c,0x40,0,0};
    const uint32_t relative=profile.prepareRva-profile.prepareCallRva-5;
    std::memcpy(bytes.data()+1,&relative,4);return bytes;
}
std::array<unsigned char,20> ArgumentBodyBytes(const BlockProfile& profile) {
    if(profile.game==GameId::Rebirth1)return {0x55,0x8b,0xec,0x83,0xec,0x10,0x8b,0x55,0x08,0x53,0x8b,0x82,0xa8,0x04,0,0,0x8b,0x0a,0x69,0xc0};
    return {0x55,0x8b,0xec,0x83,0xec,0x10,0x8b,0x55,0x08,0x53,0x56,0x57,0x69,0x82,0xa8,0x04,0,0,0x94,0};
}
constexpr std::array<unsigned char,3> CacheSaveBytes{0x89,0x5d,0x0c};
constexpr std::array<unsigned char,30> CacheTailBytes{0xff,0x87,0xcc,0x04,0,0,0x3b,0x1f,0x74,0x14,0xc7,0x87,0xcc,0x04,0,0,0,0,0,0,0xc7,0x87,0xd0,0x04,0,0,0,0,0,0};
bool BlockState(const unsigned char* adv) {
    if(!adv || *reinterpret_cast<const uint32_t*>(adv+0x10)!=0)return false;
    const auto state=adv[block->stateOffset];return state==6 || (block->idleAllowed&&state==0);
}

std::array<unsigned char,44> DiscoveryBytes(const DiscoveryProfile& profile, unsigned char* image) {
    std::array<unsigned char,44> bytes{0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x85,0xdb,0x74,0x15,0xf7,0xc3,0,0,0xff,0xff,0x75,0x0d,0x53,0xe8,0,0,0,0,0x83,0xc4,0x04,0x85,0xc0,0x75,0x05,0x32,0xc0,0x5b,0x5d,0xc3,0x56,0x8b,0x35,0,0,0,0};
    const uint32_t relative=profile.lookupRva-profile.registerRva-25;
    const uint32_t slot=reinterpret_cast<uint32_t>(image+profile.saveRva);
    std::memcpy(bytes.data()+21,&relative,4);std::memcpy(bytes.data()+40,&slot,4);
    return bytes;
}
std::array<unsigned char,91> SeenBytes(const SeenProfile& profile,const DiscoveryProfile& effects,unsigned char* image) {
    std::array<unsigned char,91> bytes{0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x85,0xdb,0x74,0x4b,0xf7,0xc3,0,0,0xff,0xff,0x75,0x43,0x53,0xe8,0,0,0,0,0x83,0xc4,0x04,0x85,0xc0,0x74,0x36,0x8b,0x15,0,0,0,0,0x8b,0x82,0,0,0,0,0x8d,0x8a,0,0,0,0,0x8d,0x14,0x42,0x81,0xc2,0,0,0,0,0x3b,0xca,0x73,0x0d,0x90,0x38,0x59,0x01,0x74,0x07,0x83,0xc1,0x02,0x3b,0xca,0x72,0xf4,0x33,0xc0,0x3b,0xca,0x5b,0x0f,0x95,0xc0,0x5d,0xc3,0x32,0xc0,0x5b,0x5d,0xc3};
    const uint32_t relative=effects.lookupRva-profile.queryRva-25,slot=reinterpret_cast<uint32_t>(image+effects.saveRva),entries=effects.countOffset+4;
    std::memcpy(bytes.data()+21,&relative,4);std::memcpy(bytes.data()+34,&slot,4);
    std::memcpy(bytes.data()+40,&effects.countOffset,4);std::memcpy(bytes.data()+46,&entries,4);std::memcpy(bytes.data()+55,&entries,4);
    if(profile.game==GameId::Rebirth1) {const unsigned char tail[]={0x0f,0x95,0xc0,0x5b};std::memcpy(bytes.data()+80,tail,4);}
    return bytes;
}

bool Readable(const void* pointer, size_t size) noexcept {
    MEMORY_BASIC_INFORMATION memory{};
    if (!pointer || !VirtualQuery(pointer, &memory, sizeof(memory)) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const auto start = reinterpret_cast<uintptr_t>(pointer);
    const auto end = reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize;
    return start <= end && size <= end - start;
}
struct BlockMemory {
    bool readable(const unsigned char* at,size_t size) {return Readable(at,size);}
    uint32_t word(const unsigned char* at) {uint32_t value;std::memcpy(&value,at,4);return value;}
};
bool RegisterDiscovery(uint32_t id) {
    const auto save=*reinterpret_cast<unsigned char**>(moduleBase+discovery->saveRva);
    if(!id || id>0xffff || !Readable(save,discovery->countOffset+4+256) ||
        *reinterpret_cast<uint32_t*>(save+discovery->countOffset)>128)return false;
    if(setFlag)setFlag(0x16,1);
    // Native add returns AL=0 both for failure and already-discovered IDs.
    // Its independently verified membership query disambiguates the latter.
    return (registerTutorial(id,1)&0xff)!=0 || (seenTutorial(id)&0xff)!=0;
}
void __cdecl PrepareBlock(unsigned char* record,unsigned char* vm) {
    // Normal argument preparation is retained in every forwarding path. A
    // verified optional CALL may advance to its next native instruction; the
    // interpreter still owns stack transitions, local reset and script effects.
    originalArguments(record);
    if(!enabled.load(std::memory_order_acquire) || !block)return;
    const auto adv=*reinterpret_cast<unsigned char**>(moduleBase+discovery->advRva);
    // This is the engine-owned live global used by native ADV consumers. Keep
    // ordinary/in-engine scripts off the validation path: VM preparation runs
    // for arithmetic and loops too, often thousands of times per frame.
    if(!BlockState(adv))return;
    const auto initial=*reinterpret_cast<unsigned char**>(record);
    if(*reinterpret_cast<uint32_t*>(initial)!=1)return; // original prep owns this valid instruction
    if(!Readable(vm,0x4014) || !Readable(record,0x4d4) ||
        *reinterpret_cast<unsigned char**>(vm+0x4004)!=record)return;
    const auto manager=*reinterpret_cast<unsigned char**>(moduleBase+selected->managerRva);
    if(!Readable(adv,block->stateOffset+2) || !BlockState(adv) ||
        (*reinterpret_cast<uint32_t*>(adv+0x48)&0x800) || !Readable(manager,0x1074) ||
        !*reinterpret_cast<uint32_t*>(manager) || *reinterpret_cast<uint32_t*>(manager+0x1070))return;
    if(*reinterpret_cast<unsigned char**>(vm+0xbfb*4)!=moduleBase+selected->commandRva ||
        *reinterpret_cast<unsigned char**>(vm+0xbeb*4)!=moduleBase+block->musicRva ||
        *reinterpret_cast<unsigned char**>(vm+0xbf4*4)!=moduleBase+block->stopRva ||
        *reinterpret_cast<unsigned char**>(vm+0xbbd*4)!=moduleBase+block->fadeRva)return;
    const auto origin=*reinterpret_cast<unsigned char**>(record+4);
    BlockMemory memory;
    for(unsigned attempt=0;attempt<8;attempt++) {
        const auto ip=*reinterpret_cast<unsigned char**>(record);
        if(ip!=*reinterpret_cast<unsigned char**>(vm+0x400c))return;
        const auto kind=IdentifyBlock(memory,origin,ip);
        if(kind==BlockKind::None || (!block->hasIntro&&kind!=BlockKind::FirstScreen))return;
        const auto size=memory.word(ip+12);
        if(size<16 || size>2048 || reinterpret_cast<uintptr_t>(ip)>UINTPTR_MAX-size || !Readable(ip+size,16))return;
        uint32_t id=0;
        if(kind==BlockKind::FirstScreen) {
            const auto depth=*reinterpret_cast<uint32_t*>(record+0x4a8);
            if(depth>8 || memory.word(ip+8)!=1)return;
            const auto value=*reinterpret_cast<uint32_t**>(record+8+depth*0x94);
            if(!Readable(value,4))return;id=*value;
            if(!RegisterDiscovery(id))return;
        }
        *reinterpret_cast<unsigned char**>(record)=ip+size;
        *reinterpret_cast<unsigned char**>(vm+0x400c)=ip+size;
        originalArguments(record);
        Log("SkipTutorials %s block %s id=%u",GameIdName(selected->game),
            kind==BlockKind::Intro?"intro":kind==BlockKind::IntroWait?"intro-wait":"first-screen",id);
    }
}
unsigned char* __cdecl PrepareBlockBridge(unsigned char* record,unsigned char* vm) {
    const auto before=*reinterpret_cast<unsigned char**>(record);
    PrepareBlock(record,vm);
    const auto after=*reinterpret_cast<unsigned char**>(record);
    return after!=before?after:nullptr;
}
__declspec(naked) void __cdecl BlockArguments() {
    // This bridge is scoped to the verified interpreter CALL, whose EBX and
    // [EBP+0xc] cache the current instruction. Synchronize both after advancing
    // so its native tail retains a following command's yielded local state.
    // Ordinary forwarding preserves both caches and the preparer's C ABI.
    __asm {
        push edx
        push dword ptr [esp+8]
        call PrepareBlockBridge
        add esp,8
        test eax,eax
        jz forwarding
        mov ebx,eax
        mov dword ptr [ebp+0ch],eax
      forwarding:
        ret
    }
}

uint32_t __cdecl Eligible(uint32_t id) {
    // Preserve the native full-width result in forwarding paths. The verified
    // tutorial caller tests AL; false selects its own complete return-EAX=1
    // branch before VM locals, input inhibition, skip clearing or Help loading.
    const uint32_t result = original();
    if (!enabled.load(std::memory_order_acquire) || !(result & 0xff)) return result;
    const auto manager = *reinterpret_cast<unsigned char**>(moduleBase + selected->managerRva);
    if (!Readable(manager, 0x1074) || *reinterpret_cast<uint32_t*>(manager + 0x1070)) return result;
    // A native bit11 suppression keeps its original behavior. Our newly skipped
    // tutorials must register discovery through the same catalog helper used
    // by Help creation, without constructing its list, window or resources.
    const auto adv=*reinterpret_cast<unsigned char**>(moduleBase+discovery->advRva);
    if (adv && (!Readable(adv,0x4c) || (*reinterpret_cast<uint32_t*>(adv+0x48)&0x800))) return result;
    if(!RegisterDiscovery(id))return result;
    Log("SkipTutorials %s bypass id=%u", GameIdName(selected->game),id);
    return 0;
}
// The validated caller already holds the resolved tutorial ID in a preserved
// register. Each bridge passes it to a normal cdecl helper without changing
// the caller's stack or EBX/EDI preservation contract.
__declspec(naked) uint32_t __cdecl Rebirth1Predicate() {
    __asm {
        push ebx
        call Eligible
        add esp,4
        ret
    }
}
__declspec(naked) uint32_t __cdecl OtherPredicate() {
    __asm {
        push edi
        call Eligible
        add esp,4
        ret
    }
}
void* Replacement(GameId game) {return game==GameId::Rebirth1?reinterpret_cast<void*>(Rebirth1Predicate):reinterpret_cast<void*>(OtherPredicate);}
}

namespace rebirths {
bool InstallSkipTutorials(const Context& context, const TutorialPatchOps* ops) noexcept {
    using namespace tutorials;
    const auto* profile = For(context.spec.id);
    const auto* effects = DiscoveryFor(context.spec.id);
    const auto* seen=SeenFor(context.spec.id);
    const auto* blockProfile=BlockFor(context.spec.id);
    if (!profile || !effects || !seen) { Log("SkipTutorials unsupported target"); return false; }
    auto* image = reinterpret_cast<unsigned char*>(context.game);
    if (enabled.load(std::memory_order_acquire)) {
        const bool same = selected == profile && moduleBase == image;
        Log("SkipTutorials %s", same ? "already installed" : "different target refused"); return same;
    }
    // The independently validated shared getter loads its own relocated
    // manager slot. Do not trust merely a matching CALL displacement.
    std::array<unsigned char,21> getter{0x8b,0x0d,0,0,0,0,0x85,0xc9,0x75,0x03,0x32,0xc0,0xc3,0x33,0xc0,0x39,0x01,0x0f,0x95,0xc0,0xc3};
    const uint32_t slot = reinterpret_cast<uint32_t>(image + profile->managerRva);
    std::memcpy(getter.data()+2, &slot, sizeof(slot));
    const unsigned char exit[] = {0x84,0xc0,0x75,0x09,0x5f,0xb8,0x01,0x00,0x00,0x00,0x5b};
    const unsigned char rb1Id[]={0x8b,0x18,0x83,0xc4,0x08},otherId[]={0x83,0xc4,0x08,0x8b,0x38};
    const auto catalog=DiscoveryBytes(*effects,image);
    const auto query=SeenBytes(*seen,*effects,image);
    const unsigned char* idBytes=profile->game==GameId::Rebirth1?rb1Id:otherId;
    if (!Readable(image+profile->callRva,16) || !Readable(image+profile->predicateRva,getter.size()) ||
        !Readable(image+profile->managerRva,sizeof(void*)) ||
        std::memcmp(image+profile->callRva,profile->call.data(),5) ||
        std::memcmp(image+profile->callRva+5,exit,sizeof(exit)) ||
        std::memcmp(image+profile->predicateRva,getter.data(),getter.size()) ||
        !Readable(image+profile->callRva-5,5) || std::memcmp(image+profile->callRva-5,idBytes,5) ||
        !Readable(image+effects->registerRva,catalog.size()) || std::memcmp(image+effects->registerRva,catalog.data(),catalog.size()) ||
        !Readable(image+seen->queryRva,query.size()) || std::memcmp(image+seen->queryRva,query.data(),query.size()) ||
        !Readable(image+effects->saveRva,4) || !Readable(image+effects->advRva,4)) {
        Log("SkipTutorials %s preflight failed", GameIdName(profile->game)); return false;
    }
    if(effects->flagRva) {
        std::array<unsigned char,22> bytes{0x55,0x8b,0xec,0x8b,0x4d,0x08,0x8b,0xd1,0xa1,0,0,0,0,0x83,0xe1,0x1f,0xc1,0xea,0x05,0x8d,0x14,0x90};
        const uint32_t saveSlot=reinterpret_cast<uint32_t>(image+effects->saveRva);std::memcpy(bytes.data()+9,&saveSlot,4);
        if(!Readable(image+effects->flagRva,bytes.size())||std::memcmp(image+effects->flagRva,bytes.data(),bytes.size())) {Log("SkipTutorials flag preflight failed");return false;}
    }
    const auto argumentCall=blockProfile?ArgumentCallBytes(*blockProfile):std::array<unsigned char,20>{};
    const auto argumentBody=blockProfile?ArgumentBodyBytes(*blockProfile):std::array<unsigned char,20>{};
    if(blockProfile && (!Readable(image+blockProfile->prepareCallRva,argumentCall.size()) ||
        !Readable(image+blockProfile->prepareRva,argumentBody.size()) ||
        std::memcmp(image+blockProfile->prepareCallRva,argumentCall.data(),argumentCall.size()) ||
        std::memcmp(image+blockProfile->prepareRva,argumentBody.data(),argumentBody.size()) ||
        !Readable(image+blockProfile->cacheSaveRva,CacheSaveBytes.size()) ||
        std::memcmp(image+blockProfile->cacheSaveRva,CacheSaveBytes.data(),CacheSaveBytes.size()) ||
        !Readable(image+blockProfile->cacheTailRva,CacheTailBytes.size()) ||
        std::memcmp(image+blockProfile->cacheTailRva,CacheTailBytes.data(),CacheTailBytes.size()))) {
        Log("SkipTutorials %s block preflight failed",GameIdName(profile->game));return false;
    }
    const auto manager = *reinterpret_cast<unsigned char**>(image + profile->managerRva);
    if (manager && (!Readable(manager,0x1074) || *reinterpret_cast<uint32_t*>(manager+0x1070))) {
        Log("SkipTutorials %s late install refused: Help already live", GameIdName(profile->game)); return false;
    }
    selected = profile; discovery = effects; moduleBase = image;
    original = reinterpret_cast<Predicate>(image + profile->predicateRva);
    registerTutorial=reinterpret_cast<RegisterTutorial>(image+effects->registerRva);
    seenTutorial=reinterpret_cast<SeenTutorial>(image+seen->queryRva);
    setFlag=effects->flagRva?reinterpret_cast<SetFlag>(image+effects->flagRva):nullptr;
    block=blockProfile;originalArguments=block?reinterpret_cast<PrepareArguments>(image+block->prepareRva):nullptr;
    std::array<unsigned char,5> blockCall{};std::copy_n(argumentCall.begin(),5,blockCall.begin());
    CallSite sites[2]={{profile->callRva,profile->call,Replacement(profile->game)},
        {block?block->prepareCallRva:0,blockCall,reinterpret_cast<void*>(BlockArguments)}};
    const auto retarget = ops ? ops->retargetCalls : RetargetCalls;
    if (!retarget || !retarget(context,sites,block?2:1)) {
        // Even an incomplete transaction must retain native forwarding.
        Log("SkipTutorials %s transaction failed error=%08lx; forwarding", GameIdName(profile->game),GetLastError()); return false;
    }
    enabled.store(true,std::memory_order_release);
    Log("SkipTutorials %s installed: pre-load bypass", GameIdName(profile->game));
    return true;
}
}
