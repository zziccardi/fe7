#include "runtime/dispatch.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <unordered_map>

namespace fe7 {
namespace {

constexpr size_t kRomHalfwordSlots = kRomRegionSize / 2u;  // 8,388,608 slots

// Zero-initialized in BSS so untouched pages consume no physical memory.
std::array<NativeFn, kRomHalfwordSlots> g_native_table{};
FallbackExecutor g_fallback_executor = nullptr;
bool g_track_fallbacks = true;
std::unordered_map<uint32_t, uint64_t>& FallbackCounts() {
  static auto* counts = new std::unordered_map<uint32_t, uint64_t>();
  return *counts;
}

inline uint32_t NormalizeRomAddr(uint32_t gba_addr) { return gba_addr & ~1u; }

inline bool IsInRomRange(uint32_t aligned_addr) {
  return aligned_addr >= kRomBaseAddr && aligned_addr < (kRomBaseAddr + kRomRegionSize);
}

}  // namespace

void RegisterNative(uint32_t gba_addr, NativeFn fn) {
  const uint32_t aligned = NormalizeRomAddr(gba_addr);
  assert(IsInRomRange(aligned) && "RegisterNative address must be in 0x08000000..0x08FFFFFF");
  g_native_table[(aligned - kRomBaseAddr) >> 1u] = fn;
}

NativeFn LookupNative(uint32_t gba_addr) {
  const uint32_t aligned = NormalizeRomAddr(gba_addr);
  if (!IsInRomRange(aligned)) {
    return nullptr;
  }
  return g_native_table[(aligned - kRomBaseAddr) >> 1u];
}

void SetFallbackExecutor(FallbackExecutor executor) { g_fallback_executor = executor; }

void SetTrackFallbacks(bool enabled) { g_track_fallbacks = enabled; }

uint32_t GbaCall(uint32_t gba_addr, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                 const uint32_t* stack_args, size_t stack_arg_count) {
  if (NativeFn fn = LookupNative(gba_addr); fn != nullptr) {
    return fn(r0, r1, r2, r3, stack_args);
  }
  const uint32_t aligned = NormalizeRomAddr(gba_addr);
  if (g_track_fallbacks) {
    ++FallbackCounts()[aligned];
  }
  assert(g_fallback_executor != nullptr && "FallbackExecutor not set for un-decompiled call");
  return g_fallback_executor(aligned, r0, r1, r2, r3, stack_args, stack_arg_count);
}

bool TryDispatchFromCpu(uint32_t gba_addr, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                        const uint32_t* stack_args, uint32_t* out_r0) {
  NativeFn fn = LookupNative(gba_addr);
  if (fn == nullptr) {
    return false;
  }
  const uint32_t res = fn(r0, r1, r2, r3, stack_args);
  if (out_r0 != nullptr) {
    *out_r0 = res;
  }
  return true;
}

std::vector<std::pair<uint32_t, uint64_t>> GetFallbackReport() {
  const auto& counts = FallbackCounts();
  std::vector<std::pair<uint32_t, uint64_t>> report(counts.begin(), counts.end());
  std::sort(report.begin(), report.end(), [](const auto& a, const auto& b) {
    if (a.second != b.second) {
      return a.second > b.second;
    }
    return a.first < b.first;
  });
  return report;
}

void ResetDispatchTable() {
  g_native_table.fill(nullptr);
  g_fallback_executor = nullptr;
  g_track_fallbacks = true;
  FallbackCounts().clear();
}

}  // namespace fe7
