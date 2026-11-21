#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "runtime/dispatch.h"
#include "runtime/gba_hal.h"
#include "runtime/rom_loader.h"

namespace {

void PrintUsage(const char* argv0) {
  std::cerr << "Usage: " << argv0
            << " --rom <path> [--frames <N>] [--report-fallbacks] [--assert-zero-fallbacks]\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::string rom_path;
  int frames = 60;
  bool report_fallbacks = false;
  bool assert_zero_fallbacks = false;

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--rom" && i + 1 < argc) {
      rom_path = argv[++i];
    } else if (arg == "--frames" && i + 1 < argc) {
      frames = std::atoi(argv[++i]);
    } else if (arg == "--report-fallbacks") {
      report_fallbacks = true;
    } else if (arg == "--assert-zero-fallbacks") {
      assert_zero_fallbacks = true;
    } else if (arg == "--help" || arg == "-h") {
      PrintUsage(argv[0]);
      return 0;
    }
  }

  if (rom_path.empty()) {
    PrintUsage(argv[0]);
    return 1;
  }

  fe7::RomImage rom;
  std::string error;
  if (!fe7::LoadRomFile(rom_path, &rom, &error)) {
    std::cerr << "Error: " << error << "\n";
    return 1;
  }

  if (!fe7::hal::Init(rom)) {
    std::cerr << "Error: failed to initialize GbaHal\n";
    return 1;
  }

  for (int f = 0; f < frames; ++f) {
    fe7::hal::StepFrame(0);
  }

  const auto fallbacks = fe7::GetFallbackReport();
  if (report_fallbacks) {
    std::cout << "Fallback addresses hit: " << fallbacks.size() << "\n";
    for (const auto& [addr, count] : fallbacks) {
      std::cout << "  0x" << std::hex << addr << std::dec << ": " << count << "\n";
    }
  }

  fe7::hal::Shutdown();

  if (assert_zero_fallbacks && !fallbacks.empty()) {
    std::cerr << "Error: expected 0 fallback calls, got " << fallbacks.size() << " addresses\n";
    return 2;
  }

  return 0;
}
