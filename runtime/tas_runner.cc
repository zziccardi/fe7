#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"
#include "runtime/dispatch.h"
#include "runtime/gba_hal.h"
#include "runtime/gba_ptr.h"
#include "runtime/rom_loader.h"
#include "runtime/trace_recorder.h"

ABSL_FLAG(std::string, rom, "", "Path to a retail FE7 US (AE7E) GBA ROM.");
ABSL_FLAG(int, frames, 60, "Number of video frames to step headlessly.");
ABSL_FLAG(bool, report_fallbacks, false,
          "Print fallback ROM addresses and hit counts.");
ABSL_FLAG(bool, assert_zero_fallbacks, false,
          "Exit with status 2 if any un-decompiled fallback calls occur.");
ABSL_FLAG(std::string, record_traces, "",
          "Optional output .fe7t path to write recorded pre-call traces.");
ABSL_FLAG(std::string, trace_addrs, "",
          "Comma-separated hex ROM addresses (e.g. 0x08000BE0) to trace.");
ABSL_FLAG(int, max_traces, 64,
          "Maximum number of pre-call trace snapshots to record.");
ABSL_FLAG(bool, dump_checkpoints, false,
          "Print FNV-1a checksums of EWRAM, IWRAM, and framebuffer on exit.");

namespace {

// 64-bit FNV-1a checksum for compact, deterministic RAM/framebuffer lockstep
// checkpoints.
uint64_t Fnv1a64(const void* data, size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  uint64_t hash = 14695981039346656037ull;
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 1099511628211ull;
  }
  return hash;
}

std::vector<uint32_t> ParseHexAddressList(std::string_view csv) {
  std::vector<uint32_t> addrs;
  if (csv.empty()) {
    return addrs;
  }
  for (std::string_view token : absl::StrSplit(csv, ',', absl::SkipEmpty())) {
    if (token.starts_with("0x") || token.starts_with("0X")) {
      token.remove_prefix(2);
    }
    uint32_t val = 0;
    if (absl::SimpleHexAtoi(token, &val)) {
      addrs.push_back(val);
    }
  }
  return addrs;
}

}  // namespace

int main(int argc, char** argv) {
  absl::SetProgramUsageMessage(
      "Headless FE7 TAS runner and trace/fallback profiler. "
      "Example: fe7_headless --rom=<path>");
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

  if (!fe7::hal::Init(*rom)) {
    std::cerr << "Error: failed to initialize GbaHal\n";
    return 1;
  }

  const std::string trace_out = absl::GetFlag(FLAGS_record_traces);
  const auto trace_addrs =
      ParseHexAddressList(absl::GetFlag(FLAGS_trace_addrs));
  if (!trace_out.empty() && !trace_addrs.empty()) {
    fe7::ConfigureTraceRecorder(
        trace_addrs, static_cast<size_t>(absl::GetFlag(FLAGS_max_traces)));
  }

  const int frames = absl::GetFlag(FLAGS_frames);
  for (int f = 0; f < frames; ++f) {
    fe7::hal::StepFrame(0);
  }

  if (!trace_out.empty()) {
    const auto& traces = fe7::GetRecordedTraces();
    if (absl::Status st = fe7::WriteTraceFile(trace_out, traces); !st.ok()) {
      std::cerr << "Error: " << st.message() << "\n";
      fe7::ResetTraceRecorder();
      fe7::hal::Shutdown();
      return 1;
    }
    std::cout << "Recorded " << traces.size() << " trace(s) to " << trace_out
              << "\n";
  }

  if (absl::GetFlag(FLAGS_dump_checkpoints)) {
    const uint64_t ewram_hash = Fnv1a64(fe7::gMem.ewram, fe7::kEwramSize);
    const uint64_t iwram_hash = Fnv1a64(fe7::gMem.iwram, fe7::kIwramSize);
    const uint64_t fb_hash = Fnv1a64(
        fe7::hal::GetFramebuffer(),
        static_cast<size_t>(fe7::kViewportWidth * fe7::kViewportHeight) *
            sizeof(uint32_t));
    std::cout << std::hex << "Checkpoint frame=" << std::dec << frames
              << std::hex << " ewram=0x" << ewram_hash << " iwram=0x"
              << iwram_hash << " fb=0x" << fb_hash << std::dec << "\n";
  }

  // Capture fallback metrics before Shutdown() detaches the dispatch table.
  const auto fallbacks = fe7::GetFallbackReport();
  if (absl::GetFlag(FLAGS_report_fallbacks)) {
    std::cout << "Fallback addresses hit: " << fallbacks.size() << "\n";
    for (const auto& [addr, count] : fallbacks) {
      std::cout << "  0x" << std::hex << addr << std::dec << ": " << count
                << "\n";
    }
  }

  fe7::ResetTraceRecorder();
  fe7::hal::Shutdown();

  // Exit with distinct code 2 so CI/TAS scripts can differentiate fallback
  // regressions from CLI or ROM-loading errors (exit code 1).
  if (absl::GetFlag(FLAGS_assert_zero_fallbacks) && !fallbacks.empty()) {
    std::cerr << "Error: expected 0 fallback calls, got " << fallbacks.size()
              << " addresses\n";
    return 2;
  }

  return 0;
}
