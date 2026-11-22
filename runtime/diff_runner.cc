// Live retail-ROM differential verification CLI (`fe7_diff`).
//
// Loads a user-supplied retail FE7 US ROM, optionally loads recorded `.fe7t`
// traces, and runs DiffFunction() against every decompiled native C++20
// function to verify bit-exact r0 and EWRAM/IWRAM parity with the original
// ARM7TDMI Thumb machine code.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "game/engine/rng.h"
#include "game/register_all.h"
#include "runtime/diff_harness.h"
#include "runtime/dispatch.h"
#include "runtime/gba_hal.h"
#include "runtime/gba_ptr.h"
#include "runtime/rom_loader.h"
#include "runtime/trace_recorder.h"

ABSL_FLAG(std::string, rom, "", "Path to a retail FE7 US (AE7E) GBA ROM.");
ABSL_FLAG(std::string, traces, "", "Optional path to a .fe7t trace file.");
ABSL_FLAG(int, fuzz_iters, 128,
          "Number of deterministic fuzzed inputs per scalar function.");

namespace {

struct VerifiedEntry {
  const char* name;
  uint32_t gba_addr;
  bool check_return_value;
  // When true, r0 is a GBA pointer to a 6-byte state buffer rather than an
  // arbitrary scalar integer, so synthetic traces pass aligned EWRAM buffers.
  bool r0_is_u16x3_ptr;
};

constexpr VerifiedEntry kVerifiedFunctions[] = {
    {"NextRN", fe7::kAddrNextRn, true, false},
    {"RandInit", fe7::kAddrRandInit, false, false},
    {"RandSetSt", fe7::kAddrRandSetSt, false, true},
    {"RandGetSt", fe7::kAddrRandGetSt, false, true},
    {"RandNext100", fe7::kAddrRandNext100, true, false},
    {"RandNext", fe7::kAddrRandNext, true, false},
    {"RandRoll", fe7::kAddrRandRoll, true, false},
    {"RandRoll2Rn", fe7::kAddrRandRoll2Rn, true, false},
    {"RandInitB", fe7::kAddrRandInitB, false, false},
    {"RandNextB", fe7::kAddrRandNextB, true, false},
};

// Synthesizes valid pre-call traces for functions taking a uint16_t[3] pointer
// in r0, with varied EWRAM/IWRAM state and aligned EWRAM buffer addresses.
std::vector<fe7::TraceRecord> MakePtrBufferTraces(uint32_t gba_addr,
                                                  size_t count) {
  std::vector<fe7::TraceRecord> out;
  out.reserve(count);
  uint32_t rng = 0x5EED1234u ^ gba_addr;
  for (size_t i = 0; i < count; ++i) {
    fe7::TraceRecord rec;
    rec.gba_addr = gba_addr;
    rec.ewram.assign(fe7::kEwramSize, 0u);
    rec.iwram.assign(fe7::kIwramSize, 0u);
    std::memcpy(rec.ewram.data(), fe7::gMem.ewram, fe7::kEwramSize);
    std::memcpy(rec.iwram.data(), fe7::gMem.iwram, fe7::kIwramSize);

    const uint32_t offset = 0x1000u + static_cast<uint32_t>(i * 8u);
    rec.regs[0] = 0x02000000u + offset;
    for (size_t b = 0; b < 6; ++b) {
      rng ^= rng << 13;
      rng ^= rng >> 17;
      rng ^= rng << 5;
      rec.ewram[offset + b] = static_cast<uint8_t>(rng & 0xFFu);
      rec.iwram[b] = static_cast<uint8_t>((rng >> 8) & 0xFFu);
    }
    out.push_back(std::move(rec));
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  absl::SetProgramUsageMessage(
      "Differential oracle verifier for decompiled FE7 native functions.");
  absl::ParseCommandLine(argc, argv);

  const std::string rom_path = absl::GetFlag(FLAGS_rom);
  if (rom_path.empty()) {
    std::cerr << "Error: --rom is required.\n";
    return 1;
  }

  auto rom = fe7::LoadRomFile(rom_path);
  if (!rom.ok()) {
    std::cerr << "Error: " << rom.status().message() << "\n";
    return 1;
  }

  std::vector<fe7::TraceRecord> loaded_traces;
  const std::string trace_path = absl::GetFlag(FLAGS_traces);
  if (!trace_path.empty()) {
    auto traces_or = fe7::ReadTraceFile(trace_path);
    if (!traces_or.ok()) {
      std::cerr << "Error: " << traces_or.status().message() << "\n";
      return 1;
    }
    loaded_traces = std::move(*traces_or);
  }

  if (!fe7::hal::Init(*rom)) {
    std::cerr << "Error: failed to initialize GbaHal\n";
    return 1;
  }

  // Step a few frames so the GBA stack and IWRAM/EWRAM are initialized by the
  // retail ROM's crt0 and main loop before running differential checks.
  for (int f = 0; f < 5; ++f) {
    fe7::hal::StepFrame(0);
  }

  fe7::RegisterAllNativeFunctions();
  const size_t fuzz_iters =
      static_cast<size_t>(absl::GetFlag(FLAGS_fuzz_iters));

  int failures = 0;
  for (const VerifiedEntry& entry : kVerifiedFunctions) {
    fe7::NativeFn fn = fe7::LookupNative(entry.gba_addr);
    if (fn == nullptr) {
      std::cerr << "FAIL " << entry.name << ": not registered\n";
      ++failures;
      continue;
    }

    fe7::DiffOptions opts;
    opts.check_return_value = entry.check_return_value;

    fe7::DiffResult res;
    if (entry.r0_is_u16x3_ptr) {
      auto ptr_traces = MakePtrBufferTraces(entry.gba_addr, fuzz_iters);
      res = fe7::DiffFunction(entry.gba_addr, fn, ptr_traces, 0, 0xFE700001u,
                              opts);
    } else {
      res = fe7::DiffFunction(entry.gba_addr, fn, loaded_traces, fuzz_iters,
                              0xFE700001u ^ entry.gba_addr, opts);
    }

    if (res.ok) {
      std::cout << "PASS " << entry.name << " (0x" << std::hex << entry.gba_addr
                << std::dec << "): traces=" << res.traces_checked
                << " fuzz=" << res.fuzz_checked << "\n";
    } else {
      std::cerr << "FAIL " << entry.name << ": " << res.failure_detail << "\n";
      ++failures;
    }
  }

  fe7::ResetDispatchTable();
  fe7::hal::Shutdown();
  return failures == 0 ? 0 : 1;
}
