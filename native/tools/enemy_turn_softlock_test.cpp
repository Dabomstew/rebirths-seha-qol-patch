#define REBIRTHS_TEST_CONTRACTS
#include "../src/enemy_turn_softlock.cpp"
#include <cstdio>
#include <stdexcept>
#include <vector>
#include <algorithm>

namespace rebirths {
void Log(const char*, ...) noexcept {}
const char* GameIdName(GameId) noexcept { return "fixture"; }
static PatchResult injected{PatchOutcome::Installed,0};
static PatchResult injectedCalls{PatchOutcome::Installed,0};
PatchResult RetargetCallsResult(const Context& context,const CallSite* sites,size_t count) noexcept {
    for(size_t i=0;i<count;++i) {
        auto* at=reinterpret_cast<unsigned char*>(context.game)+sites[i].rva;
        if(std::memcmp(at,sites[i].expected.data(),5)) return {PatchOutcome::Rejected,1};
        if(injectedCalls.MayRedirect()) {
            auto bytes=enemy_turn::ExpectedCall(sites[i].rva,
                static_cast<uint32_t>(reinterpret_cast<uintptr_t>(sites[i].replacement)-reinterpret_cast<uintptr_t>(context.game)));
            std::memcpy(at,bytes.data(),5);
        }
    }
    return injectedCalls;
}
PatchResult RetargetBytesResult(const Context& context,uint32_t rva,const unsigned char* before,
    const unsigned char* after,size_t size) noexcept {
    auto* at=reinterpret_cast<unsigned char*>(context.game)+rva;
    if (std::memcmp(at,before,size)) return {PatchOutcome::Rejected,1};
    if (injected.MayRedirect()) std::memcpy(at,after,size);
    return injected;
}
}
static void Require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
enum class Mode { Immediate, Pending, Fail };
static Mode mode;
static unsigned char* owner;
static bool beforeResume;
static int nativeResult;
static HANDLE callbackGate;
static std::atomic<unsigned> callbacks{0};
static DWORD WINAPI NativeTrampoline(void* opaque) {
    using namespace rebirths::enemy_turn;
    auto* request=static_cast<WorkerArguments*>(opaque);
    Require(request->size==4&&*static_cast<unsigned char**>(request->arguments)==owner,"owned stable argument ABI");
    beforeResume=owner[selected->busyOffset]==1;
    ++callbacks;
    if(mode==Mode::Pending) WaitForSingleObject(callbackGate,INFINITE);
    *reinterpret_cast<uint32_t*>(owner)=0x42;
    owner[selected->busyOffset]=0;
    ExitThread(0); // the real native lifetime, bypassing a normal caller return
}
static HANDLE WINAPI RefuseCreate(LPSECURITY_ATTRIBUTES,SIZE_T,LPTHREAD_START_ROUTINE,LPVOID,DWORD flags,LPDWORD) {
    Require(flags==0,"runnable worker must never require ResumeThread");
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;
}
static void JoinBeforeReturn() {
    using namespace rebirths::enemy_turn;
    Require(WaitForSingleObject(worker.TestHandle(),2000)==WAIT_OBJECT_0,"early native worker exit");
    Require(!worker.Poll(),"early completion retired");
}
static uint32_t capturedEax,capturedFlags,capturedEbx,capturedEdi,capturedEsi,stackDelta;
static int __cdecl NativeStart(int slot,size_t size,void* args) {
    using namespace rebirths::enemy_turn;
    Require(slot==7&&size==4&&*static_cast<unsigned char**>(args)==owner,"native copied-argument ABI");
    beforeResume=owner[selected->busyOffset]==1;
    if(mode==Mode::Immediate) {
        *reinterpret_cast<uint32_t*>(owner)=0x42;
        *reinterpret_cast<uint32_t*>(owner+4)=0;
        owner[selected->busyOffset]=0;
    }
    nativeResult=mode==Mode::Fail?-1:0;
    return nativeResult;
}

static unsigned Execute(const rebirths::enemy_turn::Profile& p,bool patched,bool active,Mode test) {
    using namespace rebirths::enemy_turn;
    unsigned char planner[0x20]{}; owner=planner; mode=test; beforeResume=false;
    callbacks.store(0);
    selected=&p; originalStart=NativeStart; enabled.store(active);
    worker.Configure(NativeTrampoline,reinterpret_cast<void*>(0x1234),p.busyOffset);
    callbackGate=CreateEvent(nullptr,TRUE,FALSE,nullptr);
    workerApi={};if(active&&test==Mode::Fail)workerApi.create=RefuseCreate;
    afterBegin=active&&test==Mode::Immediate?JoinBeforeReturn:nullptr;
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    Require(memory!=nullptr,"fixture code allocation");
    std::vector<unsigned char> code;
    auto bytes=[&](std::initializer_list<unsigned char> b){code.insert(code.end(),b);};
    auto word=[&](uintptr_t v){for(unsigned n=0;n<4;n++)code.push_back(static_cast<unsigned char>(v>>(8*n)));};
    // Real caller frame/local pointer, native nonvolatile registers and 3 cdecl arguments.
    bytes({0x55,0x8b,0xec,0x83,0xec,0x18,0x56,0x53,0x57,0x8b,0x75,0x08,0xbb});word(0x11223344);
    bytes({0xbf});word(0x55667788);bytes({0x8d,0x4d,0xec,0xb8});word(7);
    const size_t setup=code.size();code.insert(code.end(),p.setup.begin(),p.setup.end());
    const size_t call=code.size();code.insert(code.end(),p.original.begin(),p.original.end());
    const uint32_t direct=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&NativeStart)-reinterpret_cast<uintptr_t>(memory)-call-5);
    std::memcpy(code.data()+call+1,&direct,4);
    continuation=reinterpret_cast<uintptr_t>(memory)+code.size();
    bytes({0xa3});word(reinterpret_cast<uintptr_t>(&capturedEax));
    bytes({0x9c,0x58,0xa3});word(reinterpret_cast<uintptr_t>(&capturedFlags));
    bytes({0x89,0x1d});word(reinterpret_cast<uintptr_t>(&capturedEbx));
    bytes({0x89,0x3d});word(reinterpret_cast<uintptr_t>(&capturedEdi));
    bytes({0x89,0x35});word(reinterpret_cast<uintptr_t>(&capturedEsi));
    bytes({0x89,0xe2,0x2b,0xd5,0x89,0x15});word(reinterpret_cast<uintptr_t>(&stackDelta));
    bytes({0x0f,0xb6,0x46,p.busyOffset,0x5f,0x5b,0x5e,0x8b,0xe5,0x5d,0xc3});
    if(patched){const auto replacement=Replacement(reinterpret_cast<uintptr_t>(memory)+setup);std::memcpy(code.data()+setup,replacement.data(),replacement.size());}
    std::memcpy(memory,code.data(),code.size());FlushInstructionCache(GetCurrentProcess(),memory,code.size());
    const unsigned busy=reinterpret_cast<unsigned(__cdecl*)(unsigned char*)>(memory)(planner);
    if(active&&test==Mode::Pending) {
        const auto bound=GetTickCount64()+2000;
        while(!callbacks.load()&&GetTickCount64()<bound)Sleep(1);
        Require(callbacks.load()==1,"pending worker entered native callback");
    }
    SetEvent(callbackGate);worker.Drain();CloseHandle(callbackGate);workerApi={};
    VirtualFree(memory,0,MEM_RELEASE);
    const int expectedResult=active?0:nativeResult;
    Require(capturedEax==static_cast<uint32_t>(expectedResult),"native or accepted-pending result retained");
    Require(bool(capturedFlags&0x40)==(expectedResult==0),"original TEST flags retained");
    Require(capturedEbx==0x11223344&&capturedEdi==0x55667788&&capturedEsi==reinterpret_cast<uintptr_t>(planner),"nonvolatile registers retained");
    Require(stackDelta==static_cast<uint32_t>(-0x24),"native caller stack restored");
    if(test==Mode::Immediate)Require(*reinterpret_cast<uint32_t*>(planner)==0x42,"native results preserved");
    return busy;
}
int main() {
    try {
        using namespace rebirths;using namespace rebirths::enemy_turn;
        for(auto game:AllGameIds) {
            const auto& p=*For(game);
            Require(Execute(p,false,false,Mode::Immediate)==1&&!beforeResume,"original race must go red");
            Require(Execute(p,true,true,Mode::Immediate)==0&&beforeResume,"early worker completion retained");
            Require(Execute(p,true,true,Mode::Pending)==1&&beforeResume,"live worker remains pending");
            Require(Execute(p,true,true,Mode::Fail)==1,"creation refusal stays pending without stale results");
            Require(Execute(p,true,false,Mode::Immediate)==1&&!beforeResume,"inactive retained redirect forwards original behavior");
            for(bool dependencyFault:{false,true})for(auto result:{PatchResult{PatchOutcome::Installed,0},PatchResult{PatchOutcome::Installed,13},
                PatchResult{PatchOutcome::IncompleteRecovery,11},PatchResult{PatchOutcome::Restored,11},PatchResult{PatchOutcome::Rejected,2}}) {
                const auto last=*std::max_element(p.ownedCalls.begin(),p.ownedCalls.end());
                std::vector<unsigned char> image((std::max)(p.callRva,last)+32);
                std::memcpy(image.data()+p.callRva,p.original.data(),16);
                std::memcpy(image.data()+p.callRva-7,p.setup.data(),7);
                const std::array<uint32_t,4> targets{p.creatorRva,p.busyRva,p.busyRva,p.battleAccessorRva};
                for(size_t i=0;i<4;++i){auto bytes=ExpectedCall(p.ownedCalls[i],targets[i]);std::memcpy(image.data()+p.ownedCalls[i],bytes.data(),5);}
                GameSpec spec{};spec.id=game;Context context(reinterpret_cast<HMODULE>(image.data()),spec,L"",L"");
                injected=dependencyFault?PatchResult{PatchOutcome::Installed,0}:result;
                injectedCalls=dependencyFault?result:PatchResult{PatchOutcome::Installed,0};
                Require(InstallEnemyTurnSoftlockFix(context)==result.Succeeded(),"activation follows full transaction success");
                Require(enabled.load()==result.Succeeded(),"uncertain installs inactive");
                Require(originalStart&&originalCreator&&originalBusy&&originalBattle,"dependency forwarding retained on bridge failure");
            }
        }
        Require(!For(static_cast<GameId>(99)),"unknown game rejected");
        std::puts("Four-game native bridge/owner fixtures passed: original red, real ExitThread, early/pending/creation failure, full ABI, inactive forwarding and dependency lifetime");
        return 0;
    } catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
