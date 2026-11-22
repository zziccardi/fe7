#include "runtime/diff_harness.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "absl/base/nullability.h"
#include "absl/strings/str_format.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"
#include "runtime/trace_recorder.h"

namespace fe7 {
namespace {

// Runs one differential case: executes original Thumb first to obtain the
// oracle (r0, EWRAM, IWRAM), restores pre-call RAM, runs native_fn, and diffs.
bool RunSingleDiffCase(uint32_t aligned_addr, NativeFn absl_nonnull native_fn,
                       const std::array<uint32_t, 4>& regs,
                       const std::array<uint32_t, 4>& stack_args,
                       const std::vector<uint8_t>& pre_ewram,
                       const std::vector<uint8_t>& pre_iwram,
                       std::string* absl_nonnull failure_out) {
  std::memcpy(gMem.ewram, pre_ewram.data(), kEwramSize);
  std::memcpy(gMem.iwram, pre_iwram.data(), kIwramSize);

  // Execute original Thumb machine code directly, bypassing any registered
  // native override for `aligned_addr` (while still allowing nested native
  // helpers to dispatch if registered).
  const uint32_t thumb_r0 =
      CallFallbackDirect(aligned_addr, regs[0], regs[1], regs[2], regs[3],
                         stack_args.data(), stack_args.size());

  std::vector<uint8_t> thumb_ewram(kEwramSize);
  std::vector<uint8_t> thumb_iwram(kIwramSize);
  std::memcpy(thumb_ewram.data(), gMem.ewram, kEwramSize);
  std::memcpy(thumb_iwram.data(), gMem.iwram, kIwramSize);

  // Restore exact pre-call RAM before running the native C++20 candidate.
  std::memcpy(gMem.ewram, pre_ewram.data(), kEwramSize);
  std::memcpy(gMem.iwram, pre_iwram.data(), kIwramSize);

  const uint32_t native_r0 =
      native_fn(regs[0], regs[1], regs[2], regs[3], stack_args.data());

  if (native_r0 != thumb_r0) {
    *failure_out = absl::StrFormat(
        "r0 mismatch at 0x%08X (r0=0x%08X r1=0x%08X r2=0x%08X r3=0x%08X): "
        "thumb=0x%08X native=0x%08X",
        aligned_addr, regs[0], regs[1], regs[2], regs[3], thumb_r0, native_r0);
    return false;
  }

  for (size_t i = 0; i < kEwramSize; ++i) {
    if (gMem.ewram[i] != thumb_ewram[i]) {
      *failure_out = absl::StrFormat(
          "EWRAM mismatch at 0x%08X (addr=0x%08X): thumb=0x%02X native=0x%02X",
          0x02000000u + static_cast<uint32_t>(i), aligned_addr, thumb_ewram[i],
          gMem.ewram[i]);
      return false;
    }
  }

  for (size_t i = 0; i < kIwramSize; ++i) {
    if (gMem.iwram[i] != thumb_iwram[i]) {
      *failure_out = absl::StrFormat(
          "IWRAM mismatch at 0x%08X (addr=0x%08X): thumb=0x%02X native=0x%02X",
          0x03000000u + static_cast<uint32_t>(i), aligned_addr, thumb_iwram[i],
          gMem.iwram[i]);
      return false;
    }
  }

  return true;
}

// Simple deterministic xorshift32 PRNG for reproducible fuzzing across runs.
uint32_t XorShift32(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

}  // namespace

DiffResult DiffFunction(uint32_t gba_addr, NativeFn absl_nonnull native_fn,
                        std::span<const TraceRecord> traces, size_t fuzz_iters,
                        uint32_t seed) {
  assert(gMem.ewram != nullptr && gMem.iwram != nullptr &&
         "GbaHal must be initialized before DiffFunction");
  const uint32_t aligned = gba_addr & ~1u;

  // Save initial RAM so fuzz iterations start from a clean, deterministic
  // baseline and caller RAM is restored after verification.
  std::vector<uint8_t> baseline_ewram(kEwramSize);
  std::vector<uint8_t> baseline_iwram(kIwramSize);
  std::memcpy(baseline_ewram.data(), gMem.ewram, kEwramSize);
  std::memcpy(baseline_iwram.data(), gMem.iwram, kIwramSize);

  DiffResult result;

  for (const TraceRecord& rec : traces) {
    if ((rec.gba_addr & ~1u) != aligned) {
      continue;
    }
    if (!RunSingleDiffCase(aligned, native_fn, rec.regs, rec.stack_args,
                           rec.ewram, rec.iwram, &result.failure_detail)) {
      result.ok = false;
      std::memcpy(gMem.ewram, baseline_ewram.data(), kEwramSize);
      std::memcpy(gMem.iwram, baseline_iwram.data(), kIwramSize);
      return result;
    }
    ++result.traces_checked;
  }

  uint32_t rng = (seed == 0u) ? 0xFE700001u : seed;
  constexpr std::array<uint32_t, 4> kZeroStack{};
  for (size_t i = 0; i < fuzz_iters; ++i) {
    const std::array<uint32_t, 4> regs = {
        XorShift32(rng),
        XorShift32(rng),
        XorShift32(rng),
        XorShift32(rng),
    };
    if (!RunSingleDiffCase(aligned, native_fn, regs, kZeroStack, baseline_ewram,
                           baseline_iwram, &result.failure_detail)) {
      result.ok = false;
      std::memcpy(gMem.ewram, baseline_ewram.data(), kEwramSize);
      std::memcpy(gMem.iwram, baseline_iwram.data(), kIwramSize);
      return result;
    }
    ++result.fuzz_checked;
  }

  std::memcpy(gMem.ewram, baseline_ewram.data(), kEwramSize);
  std::memcpy(gMem.iwram, baseline_iwram.data(), kIwramSize);
  return result;
}

}  // namespace fe7
