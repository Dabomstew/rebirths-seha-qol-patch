#define REBIRTHS_TEST_CONTRACTS
#include "enemy_turn_worker.hpp"
#include <atomic>
#include <cstdio>
#include <stdexcept>
#include <thread>

using namespace rebirths::enemy_turn;
static void Require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
static std::atomic<unsigned> calls{0},creates{0},closed{0};
static unsigned refuse=0,failWait=0,failClose=0;
static HANDLE releaseGate,entered;
static bool delayEntry=false,blockCallback=false,clearBeforeBlock=false;
static LPTHREAD_START_ROUTINE savedEntry;
static void* savedArgument;
static unsigned busyOffset;
static DWORD WINAPI DelayedEntry(void*) { WaitForSingleObject(releaseGate,INFINITE);return savedEntry(savedArgument); }
static HANDLE WINAPI Create(LPSECURITY_ATTRIBUTES,SIZE_T,LPTHREAD_START_ROUTINE entry,LPVOID arg,DWORD flags,LPDWORD) {
    Require(flags==0,"no suspended worker or resume dependency");++creates;
    if(refuse){--refuse;SetLastError(ERROR_NOT_ENOUGH_MEMORY);return nullptr;}
    savedEntry=entry;savedArgument=arg;
    return ::CreateThread(nullptr,0,delayEntry?DelayedEntry:entry,delayEntry?nullptr:arg,0,nullptr);
}
static DWORD WINAPI Wait(HANDLE h,DWORD ms) {
    if(failWait){--failWait;SetLastError(ERROR_INVALID_HANDLE);return WAIT_FAILED;}
    return ::WaitForSingleObject(h,ms);
}
static BOOL WINAPI Close(HANDLE h) {
    if(failClose){--failClose;SetLastError(ERROR_INVALID_HANDLE);return FALSE;}
    const auto result=::CloseHandle(h);if(result)++closed;return result;
}
static DWORD WINAPI Callback(void* opaque) {
    auto* slot=static_cast<WorkerArguments*>(opaque);
    auto* planner=*static_cast<unsigned char**>(slot->arguments);
    if(slot->size!=4||planner[busyOffset]!=1)std::abort();
    ++calls;
    *reinterpret_cast<uint32_t*>(planner)=0x42;
    if(clearBeforeBlock)planner[busyOffset]=0;
    SetEvent(entered);
    if(blockCallback)WaitForSingleObject(releaseGate,INFINITE);
    planner[busyOffset]=0;
    ::ExitThread(0);
}
static void Reset() {
    calls=creates=closed=0;refuse=failWait=failClose=0;
    delayEntry=blockCallback=clearBeforeBlock=false;
    ResetEvent(releaseGate);ResetEvent(entered);workerApi={Create,Wait,Close};
}
static void UntilIdle(WorkerOwner& worker) {
    const auto bound=GetTickCount64()+3000;
    while(worker.Poll()&&GetTickCount64()<bound)Sleep(1);
    Require(worker.TestPhase()==WorkerOwner::Phase::Idle,"bounded retirement");
}
int main() {
    try {
        releaseGate=CreateEvent(nullptr,TRUE,FALSE,nullptr);entered=CreateEvent(nullptr,TRUE,FALSE,nullptr);
        Require(releaseGate&&entered,"fixture event ownership");
        for(auto offset:{12u,12u,12u,8u}) {
            busyOffset=offset;WorkerOwner worker;worker.Configure(Callback,reinterpret_cast<void*>(0x1234),offset);
            unsigned char planner[32]{};
            Reset();refuse=2;*reinterpret_cast<uint32_t*>(planner)=7;worker.Begin(planner);
            Require(worker.TestPlanner()==planner&&planner[offset]==1&&!worker.TestHandle(),"NULL creation remains pending");
            UntilIdle(worker);Require(creates==3&&calls==1&&closed==1&&*reinterpret_cast<uint32_t*>(planner)==0x42,"transient refusal retries once-owned request");

            Reset();refuse=1000;worker.Begin(planner);worker.Poll();worker.Drain();
            Require(calls==0&&closed==0&&planner[offset]==0&&!worker.TestPlanner(),"persistent refusal cancels on teardown without stale consumption");

            Reset();delayEntry=true;worker.Begin(planner);
            const auto original=worker.TestHandle();std::thread cancel([&]{worker.Drain();});
            const auto bound=GetTickCount64()+2000;
            while(worker.TestPhase()!=WorkerOwner::Phase::Cancelled&&GetTickCount64()<bound)Sleep(1);
            Require(worker.TestPhase()==WorkerOwner::Phase::Cancelled&&planner[offset]==1&&::WaitForSingleObject(original,0)==WAIT_TIMEOUT,"cooperative cancel retains live argument until exit");
            SetEvent(releaseGate);cancel.join();
            Require(calls==0&&closed==1&&!worker.TestPlanner()&&planner[offset]==0,"cancelled runnable gate exits and joins exactly once");

            for(bool late:{false,true}) {
                Reset();blockCallback=true;clearBeforeBlock=late;worker.Begin(planner);
                Require(::WaitForSingleObject(entered,2000)==WAIT_OBJECT_0,"native callback entered");
                Require(worker.Poll()&&worker.TestPlanner()==planner,"results are not consumed before worker termination");
                std::atomic<bool> drained{false};std::thread drain([&]{worker.Drain();drained=true;});
                Require(!drained.load()&&closed==0,"running callback is joined, never cancelled mid-calculation");
                SetEvent(releaseGate);drain.join();Require(calls==1&&closed==1&&!worker.TestHandle(),"running or late completion joined once");
            }

            Reset();worker.Begin(planner);Require(::WaitForSingleObject(worker.TestHandle(),2000)==WAIT_OBJECT_0,"worker ExitThread signal");
            const auto retained=worker.TestHandle();failWait=1;
            Require(worker.Poll()&&worker.TestHandle()==retained&&worker.TestPlanner()==planner&&closed==0,"failed wait retains handle and arguments");
            failClose=1;Require(worker.Poll()&&worker.TestHandle()==retained&&closed==0,"failed close retains ownership");
            UntilIdle(worker);Require(closed==1&&calls==1,"wait/close failures do not duplicate worker or release");

            Reset();worker.Begin(planner);failWait=1;failClose=1;worker.Drain();
            Require(closed==1&&!worker.TestPlanner(),"teardown retries failed wait/close before retiring");
            for(unsigned n=0;n<80;++n) {
                Reset();worker.Begin(planner);worker.Drain();
                Require(calls<=1&&creates==1&&closed==1&&planner[offset]==0&&!worker.TestPlanner(),"entry/cancel race preserves one owner and joins before reuse");
            }
        }
        CloseHandle(releaseGate);CloseHandle(entered);workerApi={};
        std::puts("Four layouts: real worker retry/cancel/join/ExitThread/late-clear/wait-close ownership and 320 entry-cancel races passed");
        return 0;
    } catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
