#include "runtime/dispatch.h"

#include <algorithm>
#include <array>
#include <cassert>

#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "absl/container/flat_hash_map.h"

namespace fe7 {
namespace {

// Thumb instructions are 2-byte aligned, so halfword indexing halves the
// direct-mapped table size while still giving every valid ARM/Thumb entry point
// a unique slot (8,388,608 slots = 64 MiB VA).
constexpr size_t kRomHalfwordSlots = kRomRegionSize / 2u;

// Zero-initialized in BSS so untouched virtual pages consume no physical RAM
// until registered.
std::array<NativeFn, kRomHalfwordSlots> gNativeTable{};

FallbackExecutor absl_nullable gFallbackExecutor = nullptr;

bool gTrackFallbacks = true;

// Use absl::NoDestructor to avoid non-trivial static destruction order issues
// while satisfying the Google C++ Style Guide rule against non-trivial globals.
absl::NoDestructor<absl::flat_hash_map<uint32_t, uint64_t>> gFallbackCounts;

// Clear bit 0 (the ARM/Thumb interworking bit) so callers passing `addr | 1`
// and `addr` map to the same table slot and fallback counter entry.
inline uint32_t NormalizeRomAddr(uint32_t gba_addr) { return gba_addr & ~1u; }

inline bool IsInRomRange(uint32_t aligned_addr) {
  return aligned_addr >= kRomBaseAddr &&
         aligned_addr < (kRomBaseAddr + kRomRegionSize);
}

}  // namespace

void RegisterNative(uint32_t gba_addr, NativeFn absl_nullable fn) {
  const uint32_t aligned = NormalizeRomAddr(gba_addr);
  assert(IsInRomRange(aligned) &&
         "RegisterNative address must be in 0x08000000..0x08FFFFFF");
  // Shift right by 1 to convert the 2-byte-aligned ROM offset into a halfword
  // slot index.
  gNativeTable[(aligned - kRomBaseAddr) >> 1u] = fn;
}

NativeFn absl_nullable LookupNative(uint32_t gba_addr) {
  const uint32_t aligned = NormalizeRomAddr(gba_addr);
  // Out-of-ROM addresses (e.g., IWRAM thunks or BIOS calls) are never in the
  // ROM dispatch table.
  if (!IsInRomRange(aligned)) {
    return nullptr;
  }
  return gNativeTable[(aligned - kRomBaseAddr) >> 1u];
}

void SetFallbackExecutor(FallbackExecutor absl_nullable executor) {
  gFallbackExecutor = executor;
}

void SetTrackFallbacks(bool enabled) { gTrackFallbacks = enabled; }

uint32_t GbaCall(uint32_t gba_addr, uint32_t r0, uint32_t r1, uint32_t r2,
                 uint32_t r3, const uint32_t* absl_nullable stack_args,
                 size_t stack_arg_count) {
  // Fast path: stay entirely in native C++20 when this ROM address has been
  // decompiled.
  if (NativeFn fn = LookupNative(gba_addr); fn != nullptr) {
    return fn(r0, r1, r2, r3, stack_args);
  }
  // Normalize before counting and invoking fallback so Thumb `addr | 1` and
  // `addr` aggregate under a single canonical ROM address.
  const uint32_t aligned = NormalizeRomAddr(gba_addr);
  if (gTrackFallbacks) {
    ++(*gFallbackCounts)[aligned];
  }
  assert(gFallbackExecutor != nullptr &&
         "FallbackExecutor not set for un-decompiled call");
  return gFallbackExecutor(aligned, r0, r1, r2, r3, stack_args,
                           stack_arg_count);
}

bool TryDispatchFromCpu(uint32_t gba_addr, uint32_t r0, uint32_t r1,
                        uint32_t r2, uint32_t r3,
                        const uint32_t* absl_nullable stack_args,
                        uint32_t* absl_nullable out_r0) {
  NativeFn fn = LookupNative(gba_addr);
  if (fn == nullptr) {
    // Signal the fallback CPU to continue executing the original machine code
    // at gba_addr.
    return false;
  }
  const uint32_t res = fn(r0, r1, r2, r3, stack_args);
  if (out_r0 != nullptr) {
    *out_r0 = res;
  }
  return true;
}

std::vector<std::pair<uint32_t, uint64_t>> GetFallbackReport() {
  std::vector<std::pair<uint32_t, uint64_t>> report(gFallbackCounts->begin(),
                                                    gFallbackCounts->end());
  // Sort hottest un-decompiled functions first; break ties by address for
  // deterministic test output and diffable reports across runs.
  std::sort(report.begin(), report.end(), [](const auto& a, const auto& b) {
    if (a.second != b.second) {
      return a.second > b.second;
    }
    return a.first < b.first;
  });
  return report;
}

void ResetDispatchTable() {
  gNativeTable.fill(nullptr);
  gFallbackExecutor = nullptr;
  gTrackFallbacks = true;
  gFallbackCounts->clear();
}

}  // namespace fe7
