#ifndef FE7_RUNTIME_DISPATCH_H_
#define FE7_RUNTIME_DISPATCH_H_

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace fe7 {

inline constexpr uint32_t kRomBaseAddr = 0x08000000u;
inline constexpr uint32_t kRomRegionSize = 0x01000000u;  // 16 MiB
inline constexpr uint32_t kSentinelLr = 0x08FFFFFEu;

// Unified native C++20 ABI for dispatched GBA functions.
// Up to 4 arguments in r0..r3; additional arguments in stack_args.
using NativeFn = uint32_t (*)(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                              const uint32_t* stack_args);

// Fallback executor invoked when GbaCall targets an un-decompiled address.
using FallbackExecutor = uint32_t (*)(uint32_t gba_addr, uint32_t r0, uint32_t r1, uint32_t r2,
                                      uint32_t r3, const uint32_t* stack_args,
                                      size_t stack_arg_count);

// Registers a native C++20 implementation for a Thumb/ARM ROM address.
void RegisterNative(uint32_t gba_addr, NativeFn fn);

// O(1) lookup for a registered native implementation (returns nullptr if not registered).
NativeFn LookupNative(uint32_t gba_addr);

// Sets the active fallback CPU executor (e.g. libmgba ARM7TDMI).
void SetFallbackExecutor(FallbackExecutor executor);

// Enables or disables per-address fallback hit counting.
void SetTrackFallbacks(bool enabled);

// Calls `gba_addr`: executes native C++ if registered, otherwise delegates to FallbackExecutor.
uint32_t GbaCall(uint32_t gba_addr, uint32_t r0 = 0, uint32_t r1 = 0, uint32_t r2 = 0,
                 uint32_t r3 = 0, const uint32_t* stack_args = nullptr,
                 size_t stack_arg_count = 0);

// Hook called by the fallback CPU when branching to a ROM address.
// Returns true if a native implementation was found and executed (writing *out_r0).
bool TryDispatchFromCpu(uint32_t gba_addr, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                        const uint32_t* stack_args, uint32_t* out_r0);

// Returns (gba_addr, hit_count) pairs sorted descending by hit_count.
std::vector<std::pair<uint32_t, uint64_t>> GetFallbackReport();

// Clears all registered native functions, fallback executor, and hit counters.
void ResetDispatchTable();

}  // namespace fe7

#endif  // FE7_RUNTIME_DISPATCH_H_
