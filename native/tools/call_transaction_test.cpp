#include "rebirths_patch.hpp"
#include "transaction_test_contract.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <tlhelp32.h>
#include <cstdlib>
#include <new>

// Arm allocation failure before either noexcept transaction, including page
// discovery and both enumerations. No C++ allocation may be attempted at all.
static bool forbidAllocation = false;
static unsigned allocations = 0;
void* operator new(size_t size) {
    if (forbidAllocation) { ++allocations; throw std::bad_alloc(); }
    if (void* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
void* operator new[](size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, size_t) noexcept { std::free(value); }
void operator delete[](void* value, size_t) noexcept { std::free(value); }

// Exercise real MEM_IMAGE/RX pages, including Sega's page-boundary layout.
#pragma section(".txprobe", read, execute)
__declspec(allocate(".txprobe")) __declspec(align(4096)) unsigned char probe[0x14000]{};
static int protectCalls=0, failProtect=0, flushCalls=0, failFlush=0;
static unsigned failProtectMask=0;
static bool persistentFlushFailure=false, syntheticPeer=false;
static unsigned resumes=0, closes=0, failResume=0;
enum class ThreadFault { None, SnapshotBefore, SnapshotAfter, First, NextBefore, NextAfter,
    Empty, OverflowBefore, OverflowAfter, Appeared, Disappeared, Open, Suspend, Context, InSite,
    ResumePersistent, Terminated };
static ThreadFault threadFault = ThreadFault::None;
static unsigned snapshots=0, snapshotCloses=0, cursor=0, opens=0, suspends=0;
static uintptr_t instructionIp=0;
static HANDLE FakeSnapshot = reinterpret_cast<HANDLE>(0x1234);
static HANDLE FakePeer = reinterpret_cast<HANDLE>(0x5678);
static BOOL WINAPI CheckedProtect(LPVOID p,SIZE_T n,DWORD protection,PDWORD previous) {
    ++protectCalls;
    if(protectCalls==failProtect || (protectCalls<32 && (failProtectMask & (1u<<protectCalls)))){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    return VirtualProtect(p,n,protection,previous);
}
static BOOL WINAPI CheckedFlush(HANDLE process,LPCVOID p,SIZE_T n) {
    if(++flushCalls==failFlush || persistentFlushFailure){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    return FlushInstructionCache(process,p,n);
}
static HANDLE WINAPI CheckedSnapshot(DWORD flags, DWORD pid) {
    if (!syntheticPeer) return CreateToolhelp32Snapshot(flags,pid);
    ++snapshots;
    if ((threadFault==ThreadFault::SnapshotBefore && snapshots==1) ||
        (threadFault==ThreadFault::SnapshotAfter && snapshots==2)) return INVALID_HANDLE_VALUE;
    return FakeSnapshot;
}
static BOOL WINAPI CheckedFirst(HANDLE snapshot, LPTHREADENTRY32 entry) {
    if(snapshot!=FakeSnapshot)return Thread32First(snapshot,entry);
    cursor=0;
    if(threadFault==ThreadFault::First || threadFault==ThreadFault::Empty) {SetLastError(ERROR_NO_MORE_FILES);return FALSE;}
    entry->th32OwnerProcessID=GetCurrentProcessId();entry->th32ThreadID=GetCurrentThreadId();return TRUE;
}
static BOOL WINAPI CheckedNext(HANDLE snapshot, LPTHREADENTRY32 entry) {
    if(snapshot!=FakeSnapshot)return Thread32Next(snapshot,entry);
    if ((threadFault==ThreadFault::NextBefore && snapshots==1) ||
        (threadFault==ThreadFault::NextAfter && snapshots==2)) {SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
    unsigned count=2;
    if ((threadFault==ThreadFault::OverflowBefore && snapshots==1) ||
        (threadFault==ThreadFault::OverflowAfter && snapshots==2)) count=1025;
    if (threadFault==ThreadFault::Appeared && snapshots==2) count=3;
    if (threadFault==ThreadFault::Disappeared && snapshots==2) count=1;
    if(++cursor==count){SetLastError(ERROR_NO_MORE_FILES);return FALSE;}
    entry->th32ThreadID=GetCurrentThreadId()+cursor;return TRUE;
}
static HANDLE WINAPI CheckedOpen(DWORD access,BOOL inherit,DWORD id) {
    if(!syntheticPeer)return OpenThread(access,inherit,id);
    ++opens;return threadFault==ThreadFault::Open ? nullptr : FakePeer;
}
static DWORD WINAPI CheckedSuspend(HANDLE thread) {
    if(thread!=FakePeer)return SuspendThread(thread);
    if(threadFault==ThreadFault::Suspend)return DWORD(-1);
    ++suspends;return 0;
}
static BOOL WINAPI CheckedContext(HANDLE thread,LPCONTEXT context) {
    if(thread!=FakePeer)return GetThreadContext(thread,context);
    if(threadFault==ThreadFault::Context)return FALSE;
    context->Eip=static_cast<DWORD>(instructionIp);return TRUE;
}
static DWORD WINAPI CheckedResume(HANDLE thread) {
    if(thread!=FakePeer)return ResumeThread(thread);
    ++resumes;
    return resumes==failResume || threadFault==ThreadFault::ResumePersistent ||
        threadFault==ThreadFault::Terminated ? DWORD(-1) : 1;
}
static DWORD WINAPI CheckedWait(HANDLE handle,DWORD timeout) {
    if(handle!=FakePeer)return WaitForSingleObject(handle,timeout);
    return threadFault==ThreadFault::Terminated ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
}
static BOOL WINAPI CheckedClose(HANDLE handle) {
    if(handle==FakeSnapshot){++snapshotCloses;return TRUE;}
    if(handle==FakePeer){++closes;return TRUE;}
    return CloseHandle(handle);
}

namespace rebirths { void Log(const char*, ...) noexcept {} }

static void Replacement() {}
static void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main() {
    try {
        const rebirths::testing::TransactionApi api{
            {CheckedSnapshot, CheckedFirst, CheckedNext, CheckedOpen, CheckedSuspend,
             CheckedContext, CheckedResume, CheckedWait, CheckedClose}, CheckedProtect, CheckedFlush};
        const rebirths::testing::TransactionScope injected(api);
        const auto module=GetModuleHandleW(nullptr);
        const uintptr_t base=reinterpret_cast<uintptr_t>(module);
        const uint32_t start=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(probe)-base);
        rebirths::GameSpec spec{};spec.textStartRva=start;spec.textEndRva=start+sizeof(probe);
        rebirths::Context context(module,spec,L"",L"");
        rebirths::CallSite sites[3]{{start+0xfe6,{0xe8,0,0,0,0},reinterpret_cast<void*>(&Replacement)},
            {start+0xff8,{0xe8,0,0,0,0},reinterpret_cast<void*>(&Replacement)},
            {start+0x100f,{0xe8,0,0,0,0},reinterpret_cast<void*>(&Replacement)}};
        auto reset=[&]{DWORD old=0;Require(VirtualProtect(probe,sizeof(probe),PAGE_EXECUTE_READWRITE,&old)!=0,"fixture writable");
            std::memset(probe,0,sizeof(probe));for(auto& site:sites)std::memcpy(reinterpret_cast<void*>(base+site.rva),site.expected.data(),5);
            Require(VirtualProtect(probe,sizeof(probe),PAGE_EXECUTE_READ,&old)!=0,"fixture RX");
            protectCalls=flushCalls=failProtect=failFlush=0;failProtectMask=0;persistentFlushFailure=false;
            syntheticPeer=false;resumes=closes=failResume=0;
            threadFault=ThreadFault::None;snapshots=snapshotCloses=cursor=opens=suspends=0;instructionIp=0;};
        auto originals=[&]{for(auto& site:sites)Require(!std::memcmp(reinterpret_cast<void*>(base+site.rva),site.expected.data(),5),"atomic originals");
            for(size_t offset=0;offset<sizeof(probe);offset+=4096){MEMORY_BASIC_INFORMATION m{};VirtualQuery(probe+offset,&m,sizeof(m));Require(m.Protect==PAGE_EXECUTE_READ,"RX restored");}};
        reset();Require(rebirths::RetargetCalls(context,sites,3),"two-page transaction");
        for(auto& site:sites){const auto* instruction=reinterpret_cast<const unsigned char*>(base+site.rva);int32_t relative=0;
            std::memcpy(&relative,instruction+1,4);Require(instruction[0]==0xe8&&base+site.rva+5+relative==reinterpret_cast<uintptr_t>(&Replacement),"replacement target");}
        reset();auto mismatch=sites[2];mismatch.expected[1]=1;const rebirths::CallSite bad[3]{sites[0],sites[1],mismatch};
        Require(!rebirths::RetargetCalls(context,bad,3),"second-page mismatch rejection");originals();
        reset();failProtect=1;Require(!rebirths::RetargetCalls(context,sites,3),"protect rejection");originals();
        reset();failFlush=1;Require(!rebirths::RetargetCalls(context,sites,3),"flush rollback");originals();
        for(int failure=2;failure<=4;++failure){reset();failProtect=failure;Require(!rebirths::RetargetCalls(context,sites,3),"activation/restore rollback");originals();}
        reset();DWORD old=0;Require(VirtualProtect(probe+4096,4096,PAGE_EXECUTE_READWRITE,&old)!=0,"second-page fixture");
        Require(!rebirths::RetargetCalls(context,sites,3),"mixed protection rejection");
        Require(VirtualProtect(probe+4096,4096,PAGE_EXECUTE_READ,&old)!=0,"second-page restore");originals();
        reset();Require(rebirths::RetargetCalls(context,sites,2),"original one-page transaction");
        auto distant=sites[2];distant.rva=start+0x1200f;
        const rebirths::CallSite apart[2]{sites[0],distant};
        auto resetApart=[&]{reset();DWORD old=0;Require(VirtualProtect(probe+0x12000,4096,PAGE_EXECUTE_READWRITE,&old)!=0,"distant fixture writable");
            std::memcpy(probe+0x1200f,distant.expected.data(),5);Require(VirtualProtect(probe+0x12000,4096,PAGE_EXECUTE_READ,&old)!=0,"distant fixture RX");protectCalls=flushCalls=0;};
        resetApart();Require(rebirths::RetargetCalls(context,apart,2),"discontiguous transaction");
        auto apartOriginals=[&]{originals();Require(!std::memcmp(probe+0x1200f,distant.expected.data(),5),"distant original restored");};
        for(int failure=1;failure<=4;++failure){resetApart();failProtect=failure;Require(!rebirths::RetargetCalls(context,apart,2),"distant protection failure");apartOriginals();}
        for(int failure=1;failure<=2;++failure){resetApart();failFlush=failure;Require(!rebirths::RetargetCalls(context,apart,2),"distant flush failure");apartOriginals();}
        resetApart();Require(VirtualProtect(probe+0x5000,4096,PAGE_READONLY,&old)!=0,"unrelated gap fixture");
        Require(rebirths::RetargetCalls(context,apart,2),"unrelated gap must be untouched");
        MEMORY_BASIC_INFORMATION gap{};VirtualQuery(probe+0x5000,&gap,sizeof(gap));Require(gap.Protect==PAGE_READONLY,"unrelated gap unchanged");
        Require(VirtualProtect(probe+0x5000,4096,PAGE_EXECUTE_READ,&old)!=0,"gap fixture restore");
        // Sega's blank-upload completion uses FF 15 [IAT], replaced by E8+NOP.
        // Verify the actual six-byte transaction, its tail byte and rollback.
        const unsigned char before[6]{0xff,0x15,0x08,0x64,0x74,0x00};
        unsigned char after[6]{0xe8,0,0,0,0,0x90};
        const uint32_t byteRva=start+0x300;
        const uint32_t displacement=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&Replacement)-base-byteRva-5);
        std::memcpy(after+1,&displacement,4);
        auto resetBytes=[&]{reset();DWORD old=0;Require(VirtualProtect(probe,sizeof(probe),PAGE_EXECUTE_READWRITE,&old)!=0,"byte fixture writable");
            std::memcpy(probe+0x300,before,6);Require(VirtualProtect(probe,sizeof(probe),PAGE_EXECUTE_READ,&old)!=0,"byte fixture RX");
            protectCalls=flushCalls=0;};
        auto byteOriginal=[&]{Require(!std::memcmp(probe+0x300,before,6),"six-byte original restored");originals();};
        resetBytes();Require(rebirths::RetargetBytes(context,byteRva,before,after,6),"six-byte transaction");
        Require(!std::memcmp(probe+0x300,after,6)&&probe[0x306]==0,"six-byte target and intact neighbor");
        resetBytes();failProtect=1;Require(!rebirths::RetargetBytes(context,byteRva,before,after,6),"byte protect rejection");byteOriginal();
        resetBytes();failFlush=1;Require(!rebirths::RetargetBytes(context,byteRva,before,after,6),"byte flush rollback");byteOriginal();
        resetBytes();failProtect=2;Require(!rebirths::RetargetBytes(context,byteRva,before,after,6),"byte restore rollback");byteOriginal();
        using rebirths::PatchOutcome;
        auto outcome=[&](rebirths::PatchResult result,PatchOutcome expected) {
            Require(result.outcome==expected,"explicit outcome");
            Require(result.error==GetLastError(),"stable diagnostic");
            Require(result.MayRedirect()==(expected==PatchOutcome::Installed || expected==PatchOutcome::IncompleteRecovery),"ownership distinct from activation");
            return result;
        };
        reset();Require(outcome(rebirths::RetargetCallsResult(context,sites,3),PatchOutcome::Installed).Succeeded(),"installed activation");
        reset();failProtect=1;outcome(rebirths::RetargetCallsResult(context,sites,3),PatchOutcome::Rejected);originals();
        reset();failFlush=1;outcome(rebirths::RetargetCallsResult(context,sites,3),PatchOutcome::Restored);originals();
        reset();syntheticPeer=true;failResume=1;
        auto resume=outcome(rebirths::RetargetCallsResult(context,sites,3),PatchOutcome::Installed);
        Require(!resume.Succeeded() && resume.error==0x2100000d,"installed with resume error disabled");
        Require(resumes==2 && closes==1,"resume retry and owned handle closed");
        // First-page protection restore and second-page reopening fail. That
        // leaves one CALL installed, while the other page rolls back safely.
        reset();failProtectMask=(1u<<3)|(1u<<5);
        outcome(rebirths::RetargetCallsResult(context,sites,3),PatchOutcome::IncompleteRecovery);
        Require(!std::memcmp(probe+0xfe6,sites[0].expected.data(),5),"first page rolled back");
        Require(std::memcmp(probe+0x100f,sites[2].expected.data(),5)!=0,"second page still redirected");
        resetBytes();syntheticPeer=true;failResume=1;
        resume=outcome(rebirths::RetargetBytesResult(context,byteRva,before,after,6),PatchOutcome::Installed);
        Require(!resume.Succeeded() && resume.error==0x2000000d,"byte resume error disabled");
        Require(!std::memcmp(probe+0x300,after,6) && resumes==2 && closes==1,"byte installed with forwarding ownership");
        resetBytes();failFlush=1;outcome(rebirths::RetargetBytesResult(context,byteRva,before,after,6),PatchOutcome::Restored);byteOriginal();
        resetBytes();persistentFlushFailure=true;
        outcome(rebirths::RetargetBytesResult(context,byteRva,before,after,6),PatchOutcome::IncompleteRecovery);
        byteOriginal();
        reset();syntheticPeer=true;forbidAllocation=true;
        auto noAllocation=rebirths::RetargetCallsResult(context,sites,3);
        forbidAllocation=false;
        Require(noAllocation.Succeeded() && !allocations,"CALL transaction with heap unavailable");
        resetBytes();syntheticPeer=true;forbidAllocation=true;
        noAllocation=rebirths::RetargetBytesResult(context,byteRva,before,after,6);
        forbidAllocation=false;
        Require(noAllocation.Succeeded() && !allocations,"byte transaction with heap unavailable");
        // Run thread fault cases through both real transaction entry points,
        // not only the standalone freeze. No executable page may be touched
        // when acquisition fails; all suspended peers must reach cleanup.
        struct FaultCase { ThreadFault fault; DWORD callReason, byteReason; unsigned snapshotsClosed, peerClosed, resumed; };
        const FaultCase faults[]{
            {ThreadFault::SnapshotBefore,4,3,0,0,0}, {ThreadFault::SnapshotAfter,8,8,1,1,1},
            {ThreadFault::First,4,3,1,0,0}, {ThreadFault::NextBefore,4,3,1,0,0},
            {ThreadFault::NextAfter,8,8,2,1,1}, {ThreadFault::Empty,4,3,1,0,0},
            {ThreadFault::OverflowBefore,4,3,1,0,0}, {ThreadFault::OverflowAfter,8,8,2,1,1},
            {ThreadFault::Appeared,8,8,2,1,1}, {ThreadFault::Disappeared,8,8,2,1,1},
            {ThreadFault::Open,5,4,1,0,0}, {ThreadFault::Suspend,6,5,1,1,0},
            {ThreadFault::Context,7,6,1,1,1}, {ThreadFault::InSite,7,7,1,1,1},
        };
        unsigned threadChecks=0;
        for(bool bytes : {false,true}) {
            auto run=[&]() {return bytes ? rebirths::RetargetBytesResult(context,byteRva,before,after,6)
                                        : rebirths::RetargetCallsResult(context,sites,3);};
            auto initialize=[&]() {if(bytes)resetBytes();else reset();syntheticPeer=true;};
            auto unchanged=[&]() {if(bytes)byteOriginal();else originals();};
            const DWORD errorBase=bytes ? 0x20000000 : 0x21000000;
            for(const auto& test : faults) {
                initialize();threadFault=test.fault;
                instructionIp=test.fault==ThreadFault::InSite ? base+(bytes ? byteRva : sites[1].rva) : 0;
                const auto result=outcome(run(),PatchOutcome::Rejected);
                Require(result.error==(errorBase | (bytes ? test.byteReason : test.callReason)),"thread fault diagnostic");
                Require(!protectCalls && !flushCalls,"acquisition fault wrote/protected an image page");unchanged();
                Require(snapshotCloses==test.snapshotsClosed && closes==test.peerClosed && resumes==test.resumed,"thread fault owned cleanup");
                Require(resumes>=suspends,"suspended peer lost on rejected transaction");++threadChecks;
            }
            const size_t width=bytes ? 6 : 5;
            const uintptr_t site=base+(bytes ? byteRva : sites[1].rva);
            for(size_t offset=0;offset<width;++offset) {
                initialize();instructionIp=site+offset;
                Require(outcome(run(),PatchOutcome::Rejected).error==(errorBase | 7),"instruction pointer inside owned range");
                unchanged();Require(!protectCalls && resumes==1 && closes==1,"IP rejection before write with cleanup");++threadChecks;
            }
            for(uintptr_t ip : {site-1,site+width}) {
                initialize();instructionIp=ip;
                Require(outcome(run(),PatchOutcome::Installed).Succeeded(),"IP immediately outside owned range allowed");
                Require(resumes==1 && closes==1 && snapshotCloses==2,"installed peer cleanup");++threadChecks;
            }
            initialize();threadFault=ThreadFault::ResumePersistent;
            const auto persistent=outcome(run(),PatchOutcome::Installed);
            Require(!persistent.Succeeded() && persistent.error==(errorBase | 13),"persistent resume failure retains installed ownership");
            Require(resumes==2 && closes==1,"persistent resume retry bounded and handle closed");++threadChecks;
            initialize();persistentFlushFailure=true;threadFault=ThreadFault::ResumePersistent;
            const auto uncertain=outcome(run(),PatchOutcome::IncompleteRecovery);
            Require(!uncertain.Succeeded() && uncertain.MayRedirect() && uncertain.error==(errorBase | 11),"incomplete recovery retains ownership despite failed resume");
            unchanged();Require(resumes==2 && closes==1,"incomplete recovery still accounts for suspended peer");++threadChecks;
            initialize();threadFault=ThreadFault::Terminated;
            Require(outcome(run(),PatchOutcome::Installed).Succeeded(),"terminated peer accounted for");
            Require(resumes==1 && closes==1,"terminated peer no extra retry");++threadChecks;
        }
        // Nested overrides must not leak callback state into their caller.
        reset();
        {
            const rebirths::testing::TransactionApi win32{};
            const rebirths::testing::TransactionScope native(win32);
            Require(rebirths::RetargetCallsResult(context,sites,3).Succeeded(),"nested Win32 transaction");
            Require(!protectCalls && !flushCalls,"nested override bypasses fixture callbacks");
        }
        reset();Require(rebirths::RetargetCallsResult(context,sites,3).Succeeded(),"outer override restored");
        Require(protectCalls && flushCalls,"nested scope lost outer callbacks");
        std::printf("Transaction thread boundary OK: %u enumeration/membership/IP/suspend/context/resume and cleanup contracts\n",threadChecks);
        std::puts("CALL/byte transactions OK: one/two/distant pages, untouched gap, six-byte IAT call, targets, mismatch, activation/flush/restore rollback");return 0;
    } catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
