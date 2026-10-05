#include "rebirths_patch.hpp"
#include "thread_freeze.hpp"
#ifdef REBIRTHS_TEST_CONTRACTS
#include "transaction_test_contract.hpp"
#endif
#include <algorithm>
#include <cstring>

namespace rebirths {
#ifdef REBIRTHS_TEST_CONTRACTS
namespace { const testing::TransactionApi* transactionApi = nullptr; }
testing::TransactionScope::TransactionScope(const TransactionApi& api) noexcept
    : previous_(transactionApi) { transactionApi = &api; }
testing::TransactionScope::~TransactionScope() noexcept { transactionApi = previous_; }
#endif
namespace {
#ifdef REBIRTHS_TEST_CONTRACTS
// Test-build overloads route through typed callbacks. Shipping translation units
// retain the original unqualified Win32 calls and contain no injection machinery.
BOOL WINAPI VirtualProtect(LPVOID address, SIZE_T size, DWORD protection, PDWORD previous) noexcept {
    if (transactionApi) return transactionApi->protect(address, size, protection, previous);
    return ::VirtualProtect(address, size, protection, previous);
}
BOOL WINAPI FlushInstructionCache(HANDLE process, LPCVOID address, SIZE_T size) noexcept {
    if (transactionApi) return transactionApi->flush(process, address, size);
    return ::FlushInstructionCache(process, address, size);
}
#endif

static_assert(sizeof(void*) == 4, "Re;Birth1 call patches are verified for x86 only");

// Preserve the two transaction interfaces' existing diagnostic reason codes.
DWORD FreezeReason(FreezeFailure reason, bool calls) noexcept {
    switch (reason) {
    case FreezeFailure::Enumeration: return calls ? 4 : 3;
    case FreezeFailure::Open: return calls ? 5 : 4;
    case FreezeFailure::Suspend: return calls ? 6 : 5;
    case FreezeFailure::Context: return calls ? 7 : 6;
    case FreezeFailure::InSite: return 7;
    default: return 8;
    }
}

}

PatchResult RetargetCallsResult(const Context& context, const CallSite* sites, size_t count) noexcept {
    auto fail = [](DWORD reason, PatchOutcome outcome = PatchOutcome::Rejected) {
        const DWORD error = 0x21000000 | reason;
        SetLastError(error); return PatchResult{outcome, error};
    };
    if (!sites || !count || count > 16) return fail(1);
    const uint32_t textStartRva = context.spec.textStartRva;
    const uint32_t textEndRva = context.spec.textEndRva;
    if (textEndRva < textStartRva || textEndRva - textStartRva < 5) return fail(2);
    const uintptr_t base = reinterpret_cast<uintptr_t>(context.game);
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    if (!system.dwPageSize || (system.dwPageSize & (system.dwPageSize - 1))) return fail(2);
    const uintptr_t pageMask = ~(static_cast<uintptr_t>(system.dwPageSize) - 1);
    struct Page {uintptr_t address;DWORD original=0;bool writable=false;};
    std::array<Page, 16> pageStorage{};
    for(size_t i=0;i<count;++i)pageStorage[i].address=(base+sites[i].rva)&pageMask;
    std::sort(pageStorage.begin(),pageStorage.begin()+count,[](const Page& a,const Page& b){return a.address<b.address;});
    const auto pageEnd=std::unique(pageStorage.begin(),pageStorage.begin()+count,[](const Page& a,const Page& b){return a.address==b.address;});
    // A bounded view keeps the protection/rollback loops allocation free too.
    struct Pages {
        Page* first; Page* last;
        Page* begin() const noexcept { return first; }
        Page* end() const noexcept { return last; }
    } pages{pageStorage.data(),pageStorage.data()+(pageEnd-pageStorage.begin())};
    // Protect only transaction-owned pages. Tutorial VM and screen CALLs can be
    // far apart; intervening code pages must never become writable.
    for (const auto& page:pages) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(reinterpret_cast<void*>(page.address), &memory, sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT || memory.Type != MEM_IMAGE || memory.AllocationBase != context.game ||
            memory.Protect != PAGE_EXECUTE_READ) return fail(3);
    }
    for (size_t i = 0; i < count; ++i) {
        const uintptr_t address = base + sites[i].rva;
        MEMORY_BASIC_INFORMATION memory{};
        if (!sites[i].replacement || sites[i].rva < textStartRva || sites[i].rva > textEndRva - 5 ||
            (address & pageMask) != ((address + 4) & pageMask) || sites[i].expected[0] != 0xe8 ||
            VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT || memory.Type != MEM_IMAGE || memory.Protect != PAGE_EXECUTE_READ ||
            address - reinterpret_cast<uintptr_t>(memory.BaseAddress) > memory.RegionSize - 5 ||
            std::memcmp(reinterpret_cast<void*>(address), sites[i].expected.data(), 5))
            return fail(3);
        for (size_t prior = 0; prior < i; ++prior)
            if (sites[i].rva < sites[prior].rva + 5 && sites[prior].rva < sites[i].rva + 5) return fail(3);
    }

    std::array<InstructionRange, 16> ranges{};
    for (size_t i = 0; i < count; ++i) ranges[i] = {base + sites[i].rva, sites[i].expected.size()};
#ifdef REBIRTHS_TEST_CONTRACTS
    ThreadFreeze freeze(transactionApi ? transactionApi->threads : ThreadApi{});
#else
    ThreadFreeze freeze;
#endif
    const auto freezeFailure = freeze.Acquire(ranges.data(), count);
    if (freezeFailure != FreezeFailure::None) return fail(FreezeReason(freezeFailure, true));

    auto restore=[&]() {
        bool complete=true;
        for(auto& page:pages)if(page.writable) {
            DWORD ignored=0;
            if(VirtualProtect(reinterpret_cast<void*>(page.address),system.dwPageSize,page.original,&ignored))page.writable=false;
            else complete=false;
        }
        return complete;
    };
    auto flush=[&]() {
        bool complete=true;
        for(const auto& page:pages)if(!FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(page.address),system.dwPageSize))complete=false;
        return complete;
    };
    for(auto& page:pages) {
        if(!VirtualProtect(reinterpret_cast<void*>(page.address),system.dwPageSize,PAGE_EXECUTE_READWRITE,&page.original)) {
            bool restored = restore(); if (!restored) restored = restore();
            freeze.Release(); return fail(9, restored ? PatchOutcome::Rejected : PatchOutcome::IncompleteRecovery);
        }
        page.writable=true;
        if(page.original!=PAGE_EXECUTE_READ) {
            bool restored = restore(); if (!restored) restored = restore();
            freeze.Release(); return fail(10, restored ? PatchOutcome::Rejected : PatchOutcome::IncompleteRecovery);
        }
    }
    bool wrote = false,success=true;
    {
        for (size_t i = 0; i < count; ++i) {
            const uintptr_t address = base + sites[i].rva;
            if (std::memcmp(reinterpret_cast<void*>(address), sites[i].expected.data(), 5)) { success = false; break; }
            const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(sites[i].replacement) - address - 5);
            unsigned char replacement[5] = {0xe8};
            std::memcpy(replacement + 1, &relative, sizeof(relative));
            std::memcpy(reinterpret_cast<void*>(address), replacement, sizeof(replacement));
            wrote = true;
        }
        success = success && flush();
    }
    bool restored=restore();
    PatchOutcome outcome = wrote ? PatchOutcome::Installed : PatchOutcome::Rejected;
    if((!success||!restored)&&wrote) {
        // A restoration may have succeeded on only some pages. Reopen those
        // pages before rollback; never write to a page whose protection failed.
        for(auto& page:pages)if(!page.writable) {
            DWORD ignored=0;
            page.writable=VirtualProtect(reinterpret_cast<void*>(page.address),system.dwPageSize,PAGE_EXECUTE_READWRITE,&ignored)!=FALSE;
        }
        bool rolledBack = true;
        for(size_t i=0;i<count;++i) {
            const auto page=std::find_if(pages.begin(),pages.end(),[&](const Page& p){return p.address==((base+sites[i].rva)&pageMask);});
            if(page->writable)std::memcpy(reinterpret_cast<void*>(base+sites[i].rva),sites[i].expected.data(),5);
            else rolledBack = false;
        }
        const bool rollbackFlushed = flush();
        restored=restore();if(!restored)restored=restore();success=false;
        outcome = rolledBack && rollbackFlushed && restored ? PatchOutcome::Restored : PatchOutcome::IncompleteRecovery;
    }
    const unsigned resumeFailures = freeze.Release();
    if (!restored) outcome = PatchOutcome::IncompleteRecovery;
    if (!success) return fail(11, outcome);
    if (!restored) return fail(12, outcome);
    if (resumeFailures) return fail(13, outcome);
    SetLastError(0);
    return {outcome, 0};
}

PatchResult RetargetBytesResult(const Context& context, uint32_t rva, const unsigned char* expected, const unsigned char* replacement, size_t count) noexcept {
    // Application-defined diagnostics make rejected runtime transactions
    // distinguishable without relaxing a guard or writing a partial patch.
    auto fail = [](DWORD reason, PatchOutcome outcome = PatchOutcome::Rejected) {
        const DWORD error = 0x20000000 | reason;
        SetLastError(error); return PatchResult{outcome, error};
    };
    const uint32_t textStartRva = context.spec.textStartRva;
    const uint32_t textEndRva = context.spec.textEndRva;
    if (!expected || !replacement || !count || count > 16 || textEndRva < textStartRva ||
        rva < textStartRva || rva > textEndRva || count > textEndRva - rva) return fail(1);
    const uintptr_t address = reinterpret_cast<uintptr_t>(context.game) + rva;
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory)) != sizeof(memory) || memory.State != MEM_COMMIT ||
        memory.Type != MEM_IMAGE || memory.Protect != PAGE_EXECUTE_READ || address - reinterpret_cast<uintptr_t>(memory.BaseAddress) > memory.RegionSize - count ||
        std::memcmp(reinterpret_cast<void*>(address), expected, count)) return fail(2);
    const InstructionRange range{address, count};
#ifdef REBIRTHS_TEST_CONTRACTS
    ThreadFreeze freeze(transactionApi ? transactionApi->threads : ThreadApi{});
#else
    ThreadFreeze freeze;
#endif
    const auto freezeFailure = freeze.Acquire(&range, 1);
    if (freezeFailure != FreezeFailure::None) return fail(FreezeReason(freezeFailure, false));
    SYSTEM_INFO system{}; GetSystemInfo(&system); const uintptr_t page = address & ~(static_cast<uintptr_t>(system.dwPageSize) - 1);
    if (address + count - 1 >= page + system.dwPageSize) { freeze.Release(); return fail(9); }
    DWORD original = 0;
    if (!VirtualProtect(reinterpret_cast<void*>(page), system.dwPageSize, PAGE_EXECUTE_READWRITE, &original)) { freeze.Release(); return fail(10); }
    if (original != PAGE_EXECUTE_READ) {
        DWORD ignored = 0;
        const bool restored = VirtualProtect(reinterpret_cast<void*>(page), system.dwPageSize, original, &ignored) != FALSE;
        freeze.Release();
        return fail(10, restored ? PatchOutcome::Rejected : PatchOutcome::IncompleteRecovery);
    }
    bool success = std::memcmp(reinterpret_cast<void*>(address), expected, count) == 0;
    const bool wrote = success;
    PatchOutcome outcome = wrote ? PatchOutcome::Installed : PatchOutcome::Rejected;
    if (success) { std::memcpy(reinterpret_cast<void*>(address), replacement, count); success = FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(page), system.dwPageSize) != FALSE; }
    if (!success && wrote) {
        std::memcpy(reinterpret_cast<void*>(address), expected, count);
        const bool flushed = FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(page), system.dwPageSize) != FALSE;
        outcome = flushed ? PatchOutcome::Restored : PatchOutcome::IncompleteRecovery;
    }
    DWORD ignored = 0; bool restored = VirtualProtect(reinterpret_cast<void*>(page), system.dwPageSize, original, &ignored) != FALSE;
    if (!restored) {
        if (wrote) {
            std::memcpy(reinterpret_cast<void*>(address), expected, count);
            const bool flushed = FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(page), system.dwPageSize) != FALSE;
            outcome = flushed ? PatchOutcome::Restored : PatchOutcome::IncompleteRecovery;
        }
        restored = VirtualProtect(reinterpret_cast<void*>(page), system.dwPageSize, original, &ignored) != FALSE;
        success = false;
    }
    const unsigned resumeFailures = freeze.Release();
    if (!restored) outcome = PatchOutcome::IncompleteRecovery;
    if (!success) return fail(11, outcome);
    if (!restored) return fail(12, outcome);
    if (resumeFailures) return fail(13, outcome);
    SetLastError(0);
    return {outcome, 0};
}

bool RetargetCalls(const Context& context, const CallSite* sites, size_t count) noexcept {
    const auto result = RetargetCallsResult(context, sites, count);
    if (!result.Succeeded()) Log("CALL transaction outcome=%s error=%#lx", PatchOutcomeName(result.outcome), result.error);
    SetLastError(result.error);
    return result.Succeeded();
}

bool RetargetBytes(const Context& context, uint32_t rva, const unsigned char* expected,
                   const unsigned char* replacement, size_t count) noexcept {
    const auto result = RetargetBytesResult(context, rva, expected, replacement, count);
    if (!result.Succeeded()) Log("byte transaction outcome=%s error=%#lx", PatchOutcomeName(result.outcome), result.error);
    SetLastError(result.error);
    return result.Succeeded();
}

}
