// Two-way native/fallback function dispatch table for incremental decompilation.
//
// In fe7's Ship-of-Theseus architecture, game functions are replaced one by one with native C++20
// implementations while un-decompiled code continues running on the fallback ARM7TDMI CPU. This
// module provides an O(1) ROM-address dispatch table that routes calls in both directions:
//   - Native -> Native or Fallback via GbaCall()
//   - Fallback CPU -> Native via TryDispatchFromCpu()
// It also tracks fallback hit counts so remaining un-decompiled hot paths can be prioritized.

#ifndef FE7_RUNTIME_DISPATCH_H_
#define FE7_RUNTIME_DISPATCH_H_

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "absl/base/nullability.h"

namespace fe7 {

// Base address of GBA Game Pak ROM (Wait State 0) in the 32-bit GBA address space.
inline constexpr uint32_t kRomBaseAddr = 0x08000000u;

// Size of the FE7 US ROM region (16 MiB), used to bound the direct-mapped dispatch table.
inline constexpr uint32_t kRomRegionSize = 0x01000000u;

// Sentinel Link Register (LR) address used by fallback execution to detect when a called Thumb/ARM
// function returns to native C++20 code. Lies at the top of the 16 MiB ROM window, outside real
// code.
inline constexpr uint32_t kSentinelLr = 0x08FFFFFEu;

// Unified native C++20 ABI for dispatched GBA functions (matches ARM AAPCS).
// Up to 4 arguments are passed in r0..r3; additional arguments are read from stack_args.
// Returns the 32-bit value that the GBA function would leave in r0.
using NativeFn = uint32_t (*)(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                              const uint32_t* absl_nullable stack_args);

// Fallback executor invoked when GbaCall targets an un-decompiled ROM address.
// Responsible for marshaling r0..r3 and stack_args into the fallback CPU, running until
// kSentinelLr is reached, and returning the resulting r0 value.
using FallbackExecutor = uint32_t (*)(uint32_t gba_addr, uint32_t r0, uint32_t r1, uint32_t r2,
                                      uint32_t r3, const uint32_t* absl_nullable stack_args,
                                      size_t stack_arg_count);

// Registers a native C++20 implementation for a Thumb/ARM ROM address.
// Bit 0 (the Thumb interworking bit) is ignored; gba_addr must fall within
// [kRomBaseAddr, kRomBaseAddr + kRomRegionSize).
void RegisterNative(uint32_t gba_addr, NativeFn absl_nullable fn);

// O(1) lookup for a registered native implementation.
// Returns nullptr if gba_addr is outside ROM range or has no registered native function.
NativeFn absl_nullable LookupNative(uint32_t gba_addr);

// Sets the active fallback CPU executor (e.g., libmgba ARM7TDMI). Pass nullptr to detach.
void SetFallbackExecutor(FallbackExecutor absl_nullable executor);

// Enables or disables per-address fallback hit counting for GetFallbackReport().
void SetTrackFallbacks(bool enabled);

// Calls the GBA function at gba_addr: executes the registered NativeFn if present, otherwise
// records a fallback hit (when tracking is enabled) and delegates to FallbackExecutor.
uint32_t GbaCall(uint32_t gba_addr, uint32_t r0 = 0, uint32_t r1 = 0, uint32_t r2 = 0,
                 uint32_t r3 = 0, const uint32_t* absl_nullable stack_args = nullptr,
                 size_t stack_arg_count = 0);

// Hook called by the fallback CPU when branching to a ROM address.
// Returns true if a native implementation was found and executed (writing its return value to
// *out_r0 when non-null); returns false so the fallback CPU continues executing machine code.
bool TryDispatchFromCpu(uint32_t gba_addr, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                        const uint32_t* absl_nullable stack_args, uint32_t* absl_nullable out_r0);

// Returns (aligned_gba_addr, hit_count) pairs for all fallback calls recorded since the last reset,
// sorted descending by hit_count (ties broken ascending by address).
std::vector<std::pair<uint32_t, uint64_t>> GetFallbackReport();

// Clears all registered native functions, detaches the fallback executor, re-enables tracking, and
// zeroes all fallback hit counters. Intended for test isolation and clean shutdown.
void ResetDispatchTable();

}  // namespace fe7

#endif  // FE7_RUNTIME_DISPATCH_H_
