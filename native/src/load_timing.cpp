#include "load_timing.hpp"
#include <atomic>
#include <cstring>
#pragma comment(lib,"user32.lib")

namespace rebirths {
namespace load_timing {
struct ScopeProfile { uint32_t epochCall, epoch, pollCall, poll, statusCall, stateOffset; };
struct Profile { GameId game; uint32_t status, initializerCall, initializer; std::array<ScopeProfile,2> scopes; };
constexpr Profile profiles[] = {
    {GameId::Rebirth1,0x2300,0x1684d3,0x1263a0,{{{0x164558,0x163a20,0x164568,0x163ad0,0x163afb,4},{0x1689b7,0x168450,0x1689c7,0x1684d0,0x1684f2,0}}}},
    {GameId::Rebirth2,0x6daa0,0x1d4903,0x196420,{{{0x1d0a4d,0x1cfe60,0x1d0a5d,0x1cff10,0x1cff3b,4},{0x1d4de7,0x1d4880,0x1d4df7,0x1d4900,0x1d4922,0}}}},
    {GameId::Rebirth3,0x70db0,0x20cca3,0x1c9da0,{{{0x20889d,0x207c70,0x2088ad,0x207d30,0x207d5b,4},{0x20d187,0x20cc20,0x20d197,0x20cca0,0x20ccc2,0}}}},
    {GameId::SegaHardGirls,0x79df0,0x1d4fb3,0x18a4f0,{{{0x1d07cd,0x1cfb70,0x1d07dd,0x1cfc20,0x1cfc4b,4},{0x1d54b7,0x1d4f30,0x1d54c7,0x1d4fb0,0x1d4fd2,0}}}},
};
LoadTimingState bridge;
volatile LoadTimingDescriptor descriptor{LoadMagic0,LoadMagic1,1,sizeof(LoadTimingDescriptor),&bridge,sizeof(bridge)};
SRWLOCK lock=SRWLOCK_INIT;
LoadTimingCore timing;
std::atomic<bool> enabled{false};
std::atomic<bool> worldEnabled{false};
uint32_t coverage=3;
const Profile* profile=nullptr;
uint64_t frequency=0;
uint32_t status=0;
using Phase=uint32_t (__cdecl*)(unsigned char*);
// All four original task-status bodies read [EBP+8] and return RET4;
// their callers PUSH task with no caller stack adjustment. ECX is not an input.
using Task=uint32_t (__stdcall*)(uint32_t);
using Initializer=uint32_t (__cdecl*)();
std::array<Phase,2> originalEpoch{},originalPoll{};
Phase originalWorld=nullptr,originalWorldCreate=nullptr;
Task originalStatus=nullptr;
Initializer originalInitializer=nullptr;
std::atomic<bool> clockFault{false};
BOOL (WINAPI* queryClock)(LARGE_INTEGER*)=QueryPerformanceCounter;
uint64_t NativeNow() noexcept {
    LARGE_INTEGER q{};
    if(!queryClock(&q)||q.QuadPart<=0){clockFault.store(true,std::memory_order_release);return 0;}
    return static_cast<uint64_t>(q.QuadPart);
}
bool NativeFocus() noexcept { DWORD process=0; GetWindowThreadProcessId(GetForegroundWindow(),&process); return process==GetCurrentProcessId(); }
uint64_t (*now)() noexcept=NativeNow;
bool (*focused)() noexcept=NativeFocus;
struct Scope { unsigned kind; bool queried=false, blocked=false; explicit Scope(unsigned value) noexcept:kind(value){} };
thread_local Scope* scope=nullptr;
struct ScopeGuard {
    Scope* previous;
    explicit ScopeGuard(Scope& current) noexcept:previous(scope){scope=&current;}
    ~ScopeGuard(){scope=previous;}
};
void Publish(bool focus) noexcept {
    if(clockFault.load(std::memory_order_acquire))timing.Fail();
    LoadTimingPayload payload{};
    payload.status=timing.Faulted()?2:status; payload.coverage=coverage;
    payload.transitions=timing.Transitions(); payload.focused=focus?1:0;
    payload.fault=timing.Faulted()?1:status==2?2:0;
    payload.frequency=frequency; payload.completedTicks=timing.Settled(); payload.lastQpc=timing.Frontier();
    InterlockedIncrement(&bridge.sequence); MemoryBarrier();
    std::memcpy(&bridge.data,&payload,sizeof(payload));
    MemoryBarrier(); InterlockedIncrement(&bridge.sequence);
}
bool PhaseValue(unsigned kind,unsigned char* owner,uint32_t& value) noexcept {
    __try { if(!owner)return false; value=*reinterpret_cast<uint32_t*>(owner+profile->scopes[kind].stateOffset);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool Eligible(unsigned kind,uint32_t phase) noexcept { return phase==3 || (kind==1&&phase==4); }
uint32_t __stdcall TaskStatus(uint32_t task) {
    const uint32_t result=originalStatus(task);
    if(enabled.load(std::memory_order_acquire)&&scope) {scope->queried=true;scope->blocked|=result!=2;}
    return result;
}
uint32_t __cdecl SceneInitializer() {
    // Each native caller compares AL to exactly1. Preserve the full native EAX;
    // upper bits are unspecified and must not turn AL0/2 into a blocked result.
    const uint32_t result=originalInitializer();
    if(enabled.load(std::memory_order_acquire)&&scope&&scope->kind==1) {scope->queried=true;scope->blocked|=(result&0xff)==1;}
    return result;
}
uint32_t Poll(unsigned kind,unsigned char* owner) {
    if(!enabled.load(std::memory_order_acquire))return originalPoll[kind](owner);
    uint32_t before=0,after=0;
    const bool validBefore=PhaseValue(kind,owner,before),focusBefore=focused();
    const uint64_t began=now();
    AcquireSRWLockExclusive(&lock);
    if(!validBefore||!began||clockFault.load(std::memory_order_acquire))timing.Fail();
    timing.Begin(kind,reinterpret_cast<uint32_t>(owner),before,validBefore&&Eligible(kind,before),focusBefore,began);
    Publish(focusBefore); ReleaseSRWLockExclusive(&lock);
    Scope current(kind); uint32_t result=0;
    { ScopeGuard guard(current);result=originalPoll[kind](owner); }
    const bool validAfter=PhaseValue(kind,owner,after),focusAfter=focused();
    const uint64_t ended=now();
    const bool waiting=validBefore&&validAfter&&before==after&&Eligible(kind,before)&&result==0&&current.queried&&current.blocked;
    AcquireSRWLockExclusive(&lock);
    if(!validAfter||!ended||clockFault.load(std::memory_order_acquire))timing.Fail();
    timing.End(kind,reinterpret_cast<uint32_t>(owner),after,waiting,focusBefore&&focusAfter,began,ended);
    Publish(focusAfter); ReleaseSRWLockExclusive(&lock);
    return result;
}
uint32_t Epoch(unsigned kind,unsigned char* owner) {
    if(enabled.load(std::memory_order_acquire)) {
        AcquireSRWLockExclusive(&lock);timing.Cancel(kind);Publish(focused());ReleaseSRWLockExclusive(&lock);
    }
    return originalEpoch[kind](owner);
}
uint32_t __cdecl Battle(unsigned char* owner) {return Poll(0,owner);}
uint32_t __cdecl Dungeon(unsigned char* owner) {return Poll(1,owner);}
uint32_t __cdecl BattleEpoch(unsigned char* owner) {return Epoch(0,owner);}
uint32_t __cdecl DungeonEpoch(unsigned char* owner) {return Epoch(1,owner);}
bool WorldState(unsigned char* owner,uint32_t& callback,uint32_t& flags) noexcept {
    __try {
        if(!owner)return false;
        callback=*reinterpret_cast<uint32_t*>(owner+0x58244);
        flags=*reinterpret_cast<uint32_t*>(owner+0x58250);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
uint32_t __cdecl World(unsigned char* owner) {
    if(!enabled.load(std::memory_order_acquire)||!worldEnabled.load(std::memory_order_acquire))return originalWorld(owner);
    uint32_t before=0,after=0,flagsBefore=0,flagsAfter=0;
    const bool validBefore=WorldState(owner,before,flagsBefore),focusBefore=focused();
    const uint64_t began=now();
    const auto callback=reinterpret_cast<uint32_t>(&World);
    AcquireSRWLockExclusive(&lock);
    if(!validBefore||!began||clockFault.load(std::memory_order_acquire))timing.Fail();
    timing.Begin(2,reinterpret_cast<uint32_t>(owner),1,validBefore&&before==callback&&!(flagsBefore&1),focusBefore,began);
    Publish(focusBefore);ReleaseSRWLockExclusive(&lock);
    // This original callback contains only resource-readiness short circuits.
    // Its all-ready path initializes presentation and replaces itself. Retain
    // every original call, return value and side effect; count only pending.
    const uint32_t result=originalWorld(owner);
    const bool validAfter=WorldState(owner,after,flagsAfter),focusAfter=focused();
    const uint64_t ended=now();
    const bool waiting=validBefore&&validAfter&&before==callback&&after==callback&&!(flagsBefore&1)&&!(flagsAfter&1)&&result==0;
    AcquireSRWLockExclusive(&lock);
    if(!validAfter||!ended||clockFault.load(std::memory_order_acquire))timing.Fail();
    timing.End(2,reinterpret_cast<uint32_t>(owner),1,waiting,focusBefore&&focusAfter,began,ended);
    Publish(focusAfter);ReleaseSRWLockExclusive(&lock);return result;
}
uint32_t __cdecl WorldEpoch(unsigned char* owner) {
    if(enabled.load(std::memory_order_acquire)&&worldEnabled.load(std::memory_order_acquire)) {
        AcquireSRWLockExclusive(&lock);timing.Cancel(2);Publish(focused());ReleaseSRWLockExclusive(&lock);
    }
    return originalWorldCreate(owner);
}
std::array<unsigned char,5> Expected(uint32_t site,uint32_t target) {
    std::array<unsigned char,5> value{0xe8,0,0,0,0};uint32_t displacement=target-site-5;std::memcpy(value.data()+1,&displacement,4);return value;
}
}
bool InstallLoadTiming(const Context& context) noexcept {
    using namespace load_timing;
    const Profile* selected=nullptr;
    for(const auto& candidate:profiles)if(candidate.game==context.spec.id)selected=&candidate;
    if(!selected)return false;
    profile=selected;enabled.store(false,std::memory_order_release);worldEnabled.store(false,std::memory_order_release);
    coverage=3;
    LARGE_INTEGER f{};
    if(!QueryPerformanceFrequency(&f)||f.QuadPart<=0)return false;
    frequency=static_cast<uint64_t>(f.QuadPart);clockFault.store(false,std::memory_order_release);
    AcquireSRWLockExclusive(&lock);timing.Reset(frequency/2);status=0;
    const auto startupQpc=now();
    if(!startupQpc||clockFault.load(std::memory_order_acquire)){
        timing.Fail();status=2;Publish(false);ReleaseSRWLockExclusive(&lock);
        Log("LoadTiming startup clock rejected; optional timing unavailable");return false;
    }
    ReleaseSRWLockExclusive(&lock);
    descriptor.state=&bridge; // Keep the scan descriptor resident in the pinned DLL.
    const auto base=reinterpret_cast<unsigned char*>(context.game);
    originalStatus=reinterpret_cast<Task>(base+profile->status);
    originalInitializer=reinterpret_cast<Initializer>(base+profile->initializer);
    CallSite sites[8]{};size_t count=0;
    const std::array<void*,2> polls{reinterpret_cast<void*>(Battle),reinterpret_cast<void*>(Dungeon)};
    const std::array<void*,2> epochs{reinterpret_cast<void*>(BattleEpoch),reinterpret_cast<void*>(DungeonEpoch)};
    for(unsigned kind=0;kind<2;++kind) {
        const auto& p=profile->scopes[kind];
        originalEpoch[kind]=reinterpret_cast<Phase>(base+p.epoch);originalPoll[kind]=reinterpret_cast<Phase>(base+p.poll);
        sites[count++]={p.epochCall,Expected(p.epochCall,p.epoch),epochs[kind]};
        sites[count++]={p.pollCall,Expected(p.pollCall,p.poll),polls[kind]};
        sites[count++]={p.statusCall,Expected(p.statusCall,profile->status),reinterpret_cast<void*>(TaskStatus)};
    }
    sites[count++]={profile->initializerCall,Expected(profile->initializerCall,profile->initializer),reinterpret_cast<void*>(SceneInitializer)};
    if(context.spec.id==GameId::Rebirth1) {
        originalWorld=reinterpret_cast<Phase>(base+0x18ec70);
        originalWorldCreate=reinterpret_cast<Phase>(base+0x18aaa0);
        sites[count++]={0x18ec28,Expected(0x18ec28,0x18aaa0),reinterpret_cast<void*>(WorldEpoch)};
    }
    const auto result=RetargetCallsResult(context,sites,count);
    AcquireSRWLockExclusive(&lock);status=result.Succeeded()?1:2;Publish(NativeFocus());ReleaseSRWLockExclusive(&lock);
    if(!result.Succeeded()) {Log("LoadTiming installation failed; optional timing unavailable");return false;}
    enabled.store(true,std::memory_order_release);
    if(context.spec.id==GameId::Rebirth1) {
        std::array<unsigned char,10> expected{0xc7,0x86,0x44,0x82,0x05,0x00,0,0,0,0};
        const auto native=reinterpret_cast<uint32_t>(originalWorld),replacement=reinterpret_cast<uint32_t>(&World);
        std::memcpy(expected.data()+6,&native,4);auto bytes=expected;std::memcpy(bytes.data()+6,&replacement,4);
        const auto world=RetargetBytesResult(context,0x18ec60,expected.data(),bytes.data(),bytes.size());
        AcquireSRWLockExclusive(&lock);
        if(world.Succeeded()){coverage|=4;worldEnabled.store(true,std::memory_order_release);}
        Publish(NativeFocus());ReleaseSRWLockExclusive(&lock);
        if(!world.Succeeded())Log("LoadTiming world scope unavailable; retained battle/dungeon timing");
    }
    Log("LoadTiming installed game=%s; confirmed battle/dungeon resource waits",GameIdName(context.spec.id));return true;
}
}
