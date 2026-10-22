#include "../src/load_timing.cpp"
#include <cstdio>
#include <stdexcept>
#include <vector>
namespace rebirths {
void Log(const char*,...) noexcept {}
const char* GameIdName(GameId) noexcept {return "fixture";}
PatchResult injected{PatchOutcome::Installed,0};
PatchResult injectedWorld{PatchOutcome::Installed,0};
unsigned siteChecks=0;
unsigned worldSiteChecks=0;
PatchResult RetargetCallsResult(const Context& context,const CallSite* sites,size_t count) noexcept {
    for(size_t i=0;i<count;++i) {
        if(std::memcmp(reinterpret_cast<unsigned char*>(context.game)+sites[i].rva,sites[i].expected.data(),5))return {PatchOutcome::Rejected,1};
        ++siteChecks;
    }
    return injected;
}
PatchResult RetargetBytesResult(const Context& context,uint32_t rva,const unsigned char* expected,const unsigned char* replacement,size_t count) noexcept {
    if(context.spec.id!=GameId::Rebirth1||rva!=0x18ec60||count!=10||std::memcmp(reinterpret_cast<unsigned char*>(context.game)+rva,expected,count)||std::memcmp(expected,replacement,6))return {PatchOutcome::Rejected,1};
    ++worldSiteChecks;return injectedWorld;
}
}
using namespace rebirths::load_timing;
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
uint64_t clockValue=0;
bool focusValue=true,clockFail=false,throwPoll=false;
uint32_t taskValue=1,pollValue=0;
uint32_t initializerValue=0;
unsigned initializerCalls=0;
unsigned worldCalls=0;bool worldReady=false;
unsigned rawCalls=0,failAt=0;int rawMode=0;
BOOL WINAPI RawClock(LARGE_INTEGER* value){++rawCalls;if(rawCalls==failAt){value->QuadPart=rawMode==2?-1:0;return rawMode==0?FALSE:TRUE;}value->QuadPart=static_cast<LONGLONG>(clockValue+=10);return TRUE;}
uint64_t Clock() noexcept{return clockFail?0:clockValue+=10;}
bool Focus() noexcept{return focusValue;}
uint32_t __stdcall Status(uint32_t task){Require(task==0x1234,"task ABI");return taskValue;}
uint32_t __cdecl LegacyWrongCleanup(uint32_t task){return Status(task);}
struct RawCall {uint32_t before,after,value,ebxValue,esiValue,ediValue,ebpBefore,ebpAfter,canary;};
RawCall NativeCaller(void* target){
    RawCall result{};uint32_t before,after,value,ebxValue,esiValue,ediValue,ebpBefore,ebpAfter,canary;
    __asm{
        push ebx
        push esi
        push edi
        push 0a5a55a5ah
        mov before,esp
        mov ebpBefore,ebp
        mov ebx,11223344h
        mov esi,55667788h
        mov edi,12345678h
        mov ecx,87654321h
        mov edx,0fedcba98h
        push 1234h
        call target
        mov value,eax
        mov after,esp
        mov ebpAfter,ebp
        mov ebxValue,ebx
        mov esiValue,esi
        mov ediValue,edi
        // Recover the test even when the intentionally wrong adapter leaves an
        // argument behind; the native caller itself performs no ADD ESP4.
        mov esp,before
        mov eax,dword ptr [esp]
        mov canary,eax
        add esp,4
        pop edi
        pop esi
        pop ebx
    }
    result={before,after,value,ebxValue,esiValue,ediValue,ebpBefore,ebpAfter,canary};return result;
}
uint32_t __cdecl Initialize(){++initializerCalls;return initializerValue;}
uint32_t __cdecl BattlePoll(unsigned char* owner){if(throwPoll)throw std::runtime_error("native fixture exception");if(TaskStatus(0x1234)==2)*reinterpret_cast<uint32_t*>(owner+4)=4;return pollValue;}
uint32_t __cdecl DungeonPoll(unsigned char* owner){if((SceneInitializer()&0xff)==1)return pollValue;if(TaskStatus(0x1234)==2)*reinterpret_cast<uint32_t*>(owner)=5;return pollValue;}
uint32_t __cdecl Start(unsigned char*){return 0xfedcba98;}
uint32_t __cdecl WorldPoll(unsigned char* owner){++worldCalls;if(worldReady){*reinterpret_cast<uint32_t*>(owner+0x58244)=0x12345678;*reinterpret_cast<uint32_t*>(owner+0x58250)|=1;}return pollValue;}
int main(){try{
    alignas(4) unsigned char battle[32]{},dungeon[32]{};
    *reinterpret_cast<uint32_t*>(battle+4)=3;*reinterpret_cast<uint32_t*>(dungeon)=3;
    profile=&profiles[0];originalPoll={BattlePoll,DungeonPoll};originalEpoch={Start,Start};originalStatus=Status;originalInitializer=Initialize;
    now=Clock;focused=Focus;frequency=1000;status=1;enabled=true;timing.Reset(500);
    for(bool active:{false,true})for(auto value:{0u,1u,2u,3u,4u,0xabcdef01u}){
        enabled=active;taskValue=value;const auto raw=NativeCaller(reinterpret_cast<void*>(TaskStatus));
        Require(raw.before==raw.after&&raw.value==value&&raw.canary==0xa5a55a5a,"actual native PUSH/CALL/noADD task-status ABI and stack canary");
        Require(raw.ebxValue==0x11223344&&raw.esiValue==0x55667788&&raw.ediValue==0x12345678&&raw.ebpBefore==raw.ebpAfter,"actual adapter preserves native EBX/ESI/EDI/EBP");
    }
    const auto legacy=NativeCaller(reinterpret_cast<void*>(LegacyWrongCleanup));Require(legacy.before!=legacy.after,"independent native caller oracle catches former cdecl adapter despite equivalent return value");
    enabled=true;taskValue=1;
    uint64_t previousTicks=0,previousFrontier=0;
    for(unsigned i=0;i<4;++i){Require((i%2?Dungeon(dungeon):Battle(battle))==0,"native resource poll return ABI");
        const auto& p=bridge.data;
        Require(p.completedTicks>=previousTicks&&p.lastQpc>=previousFrontier&&p.completedTicks-previousTicks<=p.lastQpc-previousFrontier,"published counter delta never outruns its confirmed QPC frontier");
        previousTicks=p.completedTicks;previousFrontier=p.lastQpc;
    }
    taskValue=2;Battle(battle);Dungeon(dungeon);
    Require(timing.Completed()==70,"actual adapters merge native two-owner waits");
    Require(bridge.sequence%2==0&&bridge.data.completedTicks==70&&bridge.data.status==1&&bridge.data.coverage==3,"resident closed-counter ABI");
    Require(bridge.data.reason==0&&bridge.data.openSinceQpc==0&&bridge.data.activeOwners==0,"consumer cannot extrapolate missing polls");
    // Independent primary world initializer path exits before TaskStatus.
    taskValue=2;initializerValue=0xabcdef01;clockValue=0;timing.Reset(500);*reinterpret_cast<uint32_t*>(dungeon)=4;
    Require(SceneInitializer()==0xabcdef01&&scope==nullptr,"unscoped initializer full EAX forwarding");
    Dungeon(dungeon);Dungeon(dungeon);Require(timing.Completed()==30,"AL1 initializer resource waits count without TaskStatus");
    const auto initializerTicks=timing.Completed();initializerValue=0xabcdef00;Dungeon(dungeon);
    Require(timing.Completed()==initializerTicks,"AL0 ready initializer drops candidate gap despite nonzero upper EAX");
    for(auto value:{0xabcdef02u,0xabcdef00u}){initializerValue=value;Require(SceneInitializer()==value,"initializer upper EAX forwarded exactly");}
    initializerValue=1;*reinterpret_cast<uint32_t*>(dungeon)=6;Dungeon(dungeon);
    Require(timing.Completed()==initializerTicks,"interactive phase excludes even AL1 initializer");
    *reinterpret_cast<uint32_t*>(dungeon)=4;focusValue=false;Dungeon(dungeon);
    Require(timing.Completed()==initializerTicks,"unfocused AL1 initializer excluded");
    focusValue=true;initializerValue=0;
    // Initializer proof belongs only to the exact scene poll's TLS scope.
    const auto callsBefore=initializerCalls;
    Scope outer(0);
    {ScopeGuard battleGuard(outer);
        initializerValue=0xabcdef01;Require(SceneInitializer()==initializerValue&&!outer.queried&&!outer.blocked,"initializer cannot establish battle resource evidence");
        Scope inner(1);
        {ScopeGuard sceneGuard(inner);
            for(auto value:{0xabcdef00u,0xabcdef02u}){initializerValue=value;Require(SceneInitializer()==value&&inner.queried&&!inner.blocked,"AL0/AL2 initializer query forwards full EAX without blocking");}
            initializerValue=0xabcdef01;Require(SceneInitializer()==initializerValue&&inner.blocked,"AL1 with unspecified upper EAX establishes scene wait");
            taskValue=2;TaskStatus(0x1234);Require(inner.blocked,"ready TaskStatus cannot erase simultaneous initializer evidence");
        }
        Require(scope==&outer&&!outer.queried,"nested scene scope restores untouched battle scope");
        taskValue=1;TaskStatus(0x1234);Require(outer.queried&&outer.blocked,"battle TaskStatus remains independently scoped");
    }
    Require(scope==nullptr&&initializerCalls==callsBefore+4,"original initializer side effects execute exactly once per adapter call");
    initializerValue=0;
    std::vector<unsigned char> world(0x58300);
    auto armWorld=[&](){*reinterpret_cast<uint32_t*>(world.data()+0x58244)=reinterpret_cast<uint32_t>(&World);*reinterpret_cast<uint32_t*>(world.data()+0x58250)=0;};
    originalWorld=WorldPoll;originalWorldCreate=Start;worldEnabled=true;enabled=true;coverage=7;timing.Reset(500);clockValue=0;pollValue=0;armWorld();
    Require(World(world.data())==0&&World(world.data())==0&&timing.Completed()==30&&worldCalls==2,"world pending callback forwards exactly once and confirms bounded resource gap");
    const auto worldPrefix=timing.Completed();worldReady=true;World(world.data());worldReady=false;
    Require(timing.Completed()==worldPrefix&&*reinterpret_cast<uint32_t*>(world.data()+0x58244)==0x12345678&&(*reinterpret_cast<uint32_t*>(world.data()+0x58250)&1),"world ready effects retained and terminal frontier excluded");
    armWorld();World(world.data());const auto epochPrefix=timing.Completed();Require(WorldEpoch(world.data())==0xfedcba98,"world epoch native result preserved");World(world.data());
    Require(timing.Completed()==epochPrefix+10,"new world epoch cannot bridge reused owner address");
    const auto gated=timing.Completed();focusValue=false;World(world.data());focusValue=true;
    Require(timing.Completed()==gated,"unfocused world time counted");
    *reinterpret_cast<uint32_t*>(world.data()+0x58250)=1;World(world.data());Require(timing.Completed()==gated,"initialized/input-capable world excluded");
    armWorld();*reinterpret_cast<uint32_t*>(world.data()+0x58244)=0x12345678;World(world.data());Require(timing.Completed()==gated,"wrong callback excluded");
    armWorld();pollValue=0xabcdef00;Require(World(world.data())==pollValue&&timing.Completed()==gated,"full world EAX forwarded without crediting unknown return");pollValue=0;
    worldEnabled=false;const auto forwarded=worldCalls;World(world.data());Require(worldCalls==forwarded+1&&timing.Completed()==gated,"unavailable world adapter remains native forwarding");coverage=3;
    Require(BattleEpoch(battle)==0xfedcba98&&DungeonEpoch(dungeon)==0xfedcba98,"epoch native return preserved");
    enabled=false;pollValue=0x12345678;Require(Battle(battle)==pollValue,"disabled native forwarding");
    enabled=true;pollValue=0;taskValue=1;focusValue=false;clockValue=0;timing.Reset(500);
    *reinterpret_cast<uint32_t*>(battle+4)=3;Battle(battle);Battle(battle);
    Require(timing.Completed()==0&&bridge.data.focused==0,"unfocused native waits excluded from timing");
    focusValue=true;Battle(battle);Battle(battle);
    Require(timing.Completed()==30,"focus recovery only counts newly confirmed native waits");
    const auto retained=timing.Completed();*reinterpret_cast<uint32_t*>(battle+4)=6;Battle(battle);
    Require(timing.Completed()==retained,"interactive battle stage never excludes input");
    *reinterpret_cast<uint32_t*>(battle+4)=3;throwPoll=true;
    bool propagated=false;try {Battle(battle);}catch(const std::runtime_error&){propagated=true;}
    Require(propagated,"native exception propagated");
    throwPoll=false;Require(scope==nullptr,"native exception restores thread-local observer scope");
    timing.Reset(500);clockFail=true;Battle(battle);clockFail=false;
    Require(bridge.data.status==2&&timing.Completed()==0,"QPC failure exposes unavailable optional timing");
    // Exercise the real NativeNow guard before AND after a native poll.
    now=NativeNow;queryClock=RawClock;
    for(int mode=0;mode<3;++mode)for(unsigned endpoint=1;endpoint<=2;++endpoint){
        profile=&profiles[0];originalPoll={BattlePoll,DungeonPoll};originalEpoch={Start,Start};originalStatus=Status;
        enabled=true;status=1;frequency=1000;timing.Reset(500);clockFault=false;rawCalls=0;failAt=0;clockValue=0;taskValue=1;pollValue=0;focusValue=true;
        *reinterpret_cast<uint32_t*>(battle+4)=3;Battle(battle);const auto prefix=timing.Completed();Require(prefix==10,"positive prefix before injected native clock failure");
        rawMode=mode;failAt=rawCalls+endpoint;Battle(battle);
        Require(clockFault.load()&&timing.Faulted()&&bridge.data.status==2&&timing.Completed()==prefix,"failed/zero/negative before/after native QPC fails closed and preserves prefix");
        failAt=0;Require(Battle(battle)==0&&timing.Completed()==prefix,"latched clock failure forwards without accumulating after clock recovers");
    }
    now=Clock;clockFault=false;
    std::vector<unsigned char> image(0x300000,0);
    for(const auto& candidate:profiles){
        for(const auto& p:candidate.scopes)for(const auto pair:std::array<std::array<uint32_t,2>,3>{{{p.epochCall,p.epoch},{p.pollCall,p.poll},{p.statusCall,candidate.status}}}) {
            const auto bytes=Expected(pair[0],pair[1]);std::memcpy(image.data()+pair[0],bytes.data(),5);
        }
        const auto initializerBytes=Expected(candidate.initializerCall,candidate.initializer);std::memcpy(image.data()+candidate.initializerCall,initializerBytes.data(),5);
        if(candidate.game==rebirths::GameId::Rebirth1){
            const auto epochBytes=Expected(0x18ec28,0x18aaa0);std::memcpy(image.data()+0x18ec28,epochBytes.data(),5);
            const unsigned char instruction[]{0xc7,0x86,0x44,0x82,0x05,0x00};std::memcpy(image.data()+0x18ec60,instruction,6);
            const auto callback=reinterpret_cast<uint32_t>(image.data()+0x18ec70);std::memcpy(image.data()+0x18ec66,&callback,4);
        }
        const rebirths::GameSpec spec{candidate.game,L"fixture",{},{},{},0x400000,0x1000,0x300000};
        const rebirths::Context context{reinterpret_cast<HMODULE>(image.data()),spec,L"fixture",L"fixture.ini"};
        for(int mode=0;mode<3;++mode){
            now=NativeNow;rawCalls=0;failAt=1;rawMode=mode;
            const auto checked=rebirths::siteChecks;
            Require(!rebirths::InstallLoadTiming(context)&&!enabled.load()&&bridge.data.status==2,"failed/zero/negative startup QPC refuses activation");
            Require(rebirths::siteChecks==checked,"startup clock rejection precedes hook transaction");
        }
        now=Clock;
        for(auto outcome:{rebirths::PatchOutcome::Rejected,rebirths::PatchOutcome::Installed,rebirths::PatchOutcome::IncompleteRecovery}) {
            rebirths::injected={outcome,1};Require(!rebirths::InstallLoadTiming(context)&&!enabled.load()&&bridge.data.status==2,"failed/partial timing install is unavailable");
            Require(originalStatus==reinterpret_cast<Task>(image.data()+candidate.status),"retained adapter forwarding target");
            Require(originalInitializer==reinterpret_cast<Initializer>(image.data()+candidate.initializer),"retained initializer forwarding target");
        }
        rebirths::injected={rebirths::PatchOutcome::Installed,0};Require(rebirths::InstallLoadTiming(context)&&enabled.load()&&bridge.data.status==1,"successful timing activation");
        Require(bridge.data.coverage==(candidate.game==rebirths::GameId::Rebirth1?7u:3u),"world coverage only on independently verified RB1 profile");
        if(candidate.game==rebirths::GameId::Rebirth1){
            for(auto outcome:{rebirths::PatchOutcome::Rejected,rebirths::PatchOutcome::Installed,rebirths::PatchOutcome::IncompleteRecovery}){
                rebirths::injectedWorld={outcome,1};Require(rebirths::InstallLoadTiming(context)&&enabled.load()&&!worldEnabled.load()&&bridge.data.coverage==3,"world installation failure preserves qualified battle/dungeon bridge");
                Require(originalWorld==reinterpret_cast<Phase>(image.data()+0x18ec70)&&originalWorldCreate==reinterpret_cast<Phase>(image.data()+0x18aaa0),"world failure retains exact native forwarding targets");
            }
            rebirths::injectedWorld={rebirths::PatchOutcome::Installed,0};
        }
        Require(descriptor.state==&bridge&&descriptor.stateBytes==112&&descriptor.version==1,"scan descriptor residency/layout");
    }
    Require(rebirths::siteChecks==140&&rebirths::worldSiteChecks==4,"independent existing profiles, RB1 epoch and world transaction failure modes");
    std::puts("Load timing native poll/epoch/forwarding, ABI publication and partial-installation checks pass");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}}
