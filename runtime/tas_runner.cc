#include <cstdint>
#include <iostream>
#include <string>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "runtime/dispatch.h"
#include "runtime/gba_hal.h"
#include "runtime/rom_loader.h"

ABSL_FLAG(std::string, rom, "", "Path to a retail FE7 US (AE7E) GBA ROM.");
ABSL_FLAG(int, frames, 60, "Number of video frames to step headlessly.");
ABSL_FLAG(bool, report_fallbacks, false, "Print fallback ROM addresses and hit counts.");
ABSL_FLAG(bool, assert_zero_fallbacks, false,
          "Exit with status 2 if any un-decompiled fallback calls occur.");

int main(int argc, char** argv) {
  absl::SetProgramUsageMessage(
      "Headless FE7 TAS runner and fallback profiler. Example: fe7_headless --rom=<path>");
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

  const int frames = absl::GetFlag(FLAGS_frames);
  for (int f = 0; f < frames; ++f) {
    fe7::hal::StepFrame(0);
  }

  // Capture fallback metrics before Shutdown() detaches the dispatch table.
  const auto fallbacks = fe7::GetFallbackReport();
  if (absl::GetFlag(FLAGS_report_fallbacks)) {
    std::cout << "Fallback addresses hit: " << fallbacks.size() << "\n";
    for (const auto& [addr, count] : fallbacks) {
      std::cout << "  0x" << std::hex << addr << std::dec << ": " << count << "\n";
    }
  }

  fe7::hal::Shutdown();

  // Exit with distinct code 2 so CI/TAS scripts can differentiate fallback regressions from CLI or
  // ROM-loading errors (exit code 1).
  if (absl::GetFlag(FLAGS_assert_zero_fallbacks) && !fallbacks.empty()) {
    std::cerr << "Error: expected 0 fallback calls, got " << fallbacks.size() << " addresses\n";
    return 2;
  }

  return 0;
}
