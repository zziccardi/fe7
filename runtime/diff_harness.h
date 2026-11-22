// Differential verification harness comparing original GBA Thumb execution
// against decompiled native C++20 functions.
//
// For each recorded trace (and/or fuzzed register input), executes the target
// ROM address on the fallback ARM7TDMI CPU as an oracle, restores pre-call
// EWRAM/IWRAM, executes the candidate NativeFn, and asserts exact r0 equality
// plus byte-for-byte identical EWRAM and IWRAM state.

#ifndef FE7_RUNTIME_DIFF_HARNESS_H_
#define FE7_RUNTIME_DIFF_HARNESS_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "absl/base/nullability.h"
#include "runtime/dispatch.h"
#include "runtime/trace_recorder.h"

namespace fe7 {

// Outcome of a differential test run across recorded traces and fuzz cases.
struct DiffResult {
  bool ok = true;
  size_t traces_checked = 0;
  size_t fuzz_checked = 0;
  std::string failure_detail;
};

// Options controlling differential comparison strictness.
struct DiffOptions {
  // Set false for void GBA functions whose Thumb epilogue (`pop {r0}; bx r0`)
  // clobbers r0 with the caller's link register rather than returning a value.
  bool check_return_value = true;

  // Number of low IWRAM bytes to compare (default 0x7800 covers all static
  // IWRAM globals 0x03000000..0x030077FF while excluding the 0x03007800..7FFF
  // ARM7TDMI stack scratch area dirtied by Thumb prologues).
  size_t iwram_compare_bytes = 0x7800u;
};

// Compares original Thumb machine code at `gba_addr` against `native_fn` across
// all matching `traces` plus `fuzz_iters` deterministic pseudo-random r0..r3
// inputs seeded by `seed`. Requires GbaHal to be initialized.
DiffResult DiffFunction(uint32_t gba_addr, NativeFn absl_nonnull native_fn,
                        std::span<const TraceRecord> traces = {},
                        size_t fuzz_iters = 0, uint32_t seed = 0xFE700001u,
                        DiffOptions options = {});

}  // namespace fe7

#endif  // FE7_RUNTIME_DIFF_HARNESS_H_
