#include "sega_dungeon_movement_fix.hpp"

#include <array>
#include <cstring>

namespace rebirths {
namespace {

// Sega VA 0x005e9855: MOV dword ptr [EAX+0x24], 1, reached when the
// clock's integer step count is below one. Zero leaves the fractional
// clock carry intact without adding an unearned player simulation step.
constexpr uint32_t BranchRva = 0x1e9850;
constexpr uint32_t ImmediateRva = 0x1e9858;
constexpr std::array<unsigned char, 14> Original = {
    0x83, 0xf9, 0x01,       // CMP ECX, 1
    0x7d, 0x09,             // JGE upper-bound check
    0xc7, 0x40, 0x24, 0x01, 0x00, 0x00, 0x00, // MOV [EAX+0x24], 1
    0xeb, 0x0c              // JMP after upper-bound check
};
constexpr size_t ImmediateOffset = ImmediateRva - BranchRva;
static_assert(ImmediateOffset == 8);

bool OriginalSite(const Context& context) noexcept {
    const auto address = reinterpret_cast<uintptr_t>(context.game) + BranchRva;
    MEMORY_BASIC_INFORMATION memory{};
    return VirtualQuery(reinterpret_cast<const void*>(address), &memory, sizeof(memory)) == sizeof(memory) &&
        memory.State == MEM_COMMIT && memory.Type == MEM_IMAGE && memory.Protect == PAGE_EXECUTE_READ &&
        memory.RegionSize >= Original.size() &&
        address - reinterpret_cast<uintptr_t>(memory.BaseAddress) <= memory.RegionSize - Original.size() &&
        std::memcmp(reinterpret_cast<const void*>(address), Original.data(), Original.size()) == 0;
}

bool PatchedSite(const Context& context) noexcept {
    const auto address = reinterpret_cast<uintptr_t>(context.game) + BranchRva;
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(reinterpret_cast<const void*>(address), &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || memory.Type != MEM_IMAGE || memory.Protect != PAGE_EXECUTE_READ ||
        memory.RegionSize < Original.size() ||
        address - reinterpret_cast<uintptr_t>(memory.BaseAddress) > memory.RegionSize - Original.size()) return false;
    auto expected = Original;
    expected[ImmediateOffset] = 0;
    return std::memcmp(reinterpret_cast<const void*>(address), expected.data(), expected.size()) == 0;
}

}

bool InstallSegaDungeonMovementFix(const Context& context,
                                   const DungeonMovementFixPatchOps* injectedOps) noexcept {
    if (context.spec.id != GameId::SegaHardGirls) {
        Log("DungeonMovementFix wrong target");
        return false;
    }
    const DungeonMovementFixPatchOps productionOps{RetargetBytes};
    const auto& ops = injectedOps ? *injectedOps : productionOps;
    if (!ops.retargetBytes || !OriginalSite(context)) {
        Log("DungeonMovementFix preflight failed; no bytes changed");
        return false;
    }
    constexpr unsigned char before = 1, after = 0;
    const bool transaction = ops.retargetBytes(context, ImmediateRva, &before, &after, 1);
    if (transaction && PatchedSite(context)) {
        Log("DungeonMovementFix sega installed rva=%#x 1->0", ImmediateRva);
        return true;
    }
    const bool wrote = *reinterpret_cast<const unsigned char*>(
        reinterpret_cast<uintptr_t>(context.game) + ImmediateRva) == after;
    bool rollback = !wrote && OriginalSite(context);
    if (wrote) {
        rollback = ops.retargetBytes(context, ImmediateRva, &after, &before, 1) && OriginalSite(context);
    }
    Log("DungeonMovementFix install failed rva=%#x rollback=%s", ImmediateRva,
        rollback ? "complete" : "incomplete");
    return false;
}

}
