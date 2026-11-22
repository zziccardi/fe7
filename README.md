# fe7

A faithful, native C++20 PC port of *Fire Emblem: The Blazing Blade* (**FE7 US, `AE7E`**) targeting Windows, Linux, and macOS.

## Legal & ROM Requirement

No copyrighted ROM bytes, extracted graphics, dialogue scripts, or music samples are included in this repository.

To run the game, you must provide your own legally obtained **FE7 US (`AE7E`)** ROM via `--rom <path>`. The loader validates the ROM by file size and SHA-1 rather than filename:

- **Expected Size:** `16,777,216` bytes (16 MiB)
- **Expected SHA-1:** `c735fdbb9e8abe19e0c6a44708df19acc962e204`

## Architecture Overview

This project uses an incremental **Ship-of-Theseus** replacement strategy:

1. **Hardware Abstraction (`GbaHal` + `libmgba`):** Embeds `libmgba` (MPL-2.0) behind an owned `GbaHal` interface for accurate GBA PPU, APU, DMA, timers, IRQs, BIOS SWIs, and an ARM7TDMI fallback CPU.
2. **Two-Way Dispatch & Virtual Bus (`GbaPtr<T>`):** Maps GBA EWRAM/IWRAM/ROM addresses into native C++20 with bounds and alignment checks, and routes calls seamlessly between decompiled C++20 functions (`game/`) and the fallback CPU.
3. **Behavioral Verification:** Decompiled functions are verified against original Thumb machine code using a per-function differential test harness (recorded traces + fuzzing + ASan/UBSan) and headless TAS lockstep suites.
4. **Fallback Retirement & Modern Enhancements:** Once 100% of reachable game code runs natively in C++20, the ARM7TDMI fallback core is retired, unlocking native PC enhancements (16:9 widescreen, HD text, turn rewind, danger-zone overlay, mouse/gamepad support, and ImGui debug tooling).

## Build System & Code Style

- **Build:** Bazel (Bzlmod) with Clang and C++20 (`-fno-exceptions`, `-fno-rtti`, `-fno-strict-aliasing`).
- **Style:** Google C++ Style Guide (`.h`/`.cc`, `clang-format`, `clang-tidy`).

## License

All new `fe7` code is licensed under the [MIT License](LICENSE). Third-party `libmgba` patches remain under MPL-2.0 until fallback retirement.
