# fe7

A faithful, native C++20 PC port of *Fire Emblem: The Blazing Blade* (**FE7 US, `AE7E`**) targeting Windows, Linux, and macOS.

## Legal & ROM Requirement

No copyrighted ROM bytes, extracted graphics, dialogue scripts, or music samples are included in this repository.

To run the game, you must provide your own legally obtained **FE7 US (`AE7E`)** ROM via `--rom=<path>`. The loader validates the ROM by file size and SHA-1 rather than filename:

- **Expected Size:** `16,777,216` bytes (16 MiB)
- **Expected SHA-1:** `c735fdbb9e8abe19e0c6a44708df19acc962e204`

## Architecture Overview

This project uses an incremental **Ship-of-Theseus** replacement strategy (see [docs/implementation_plan.md](docs/implementation_plan.md) for full details):

1. **Hardware Abstraction (`GbaHal` + `libmgba`):** Embeds `libmgba` (MPL-2.0) behind an owned `GbaHal` interface (`runtime/gba_hal.h`) for accurate GBA PPU, APU, DMA, timers, IRQs, BIOS SWIs, and an ARM7TDMI fallback CPU.
2. **Two-Way Dispatch & Virtual Bus (`GbaPtr<T>`):** Maps GBA EWRAM/IWRAM/ROM addresses into native C++20 with bounds and alignment checks (`runtime/gba_ptr.h`), and routes calls seamlessly between decompiled C++20 functions (`game/`) and the fallback CPU (`runtime/dispatch.h`).
3. **Behavioral Verification:** Decompiled functions are verified against original Thumb machine code using colocated GoogleTest suites (zero retail ROM required in CI) plus a live differential oracle (`//runtime:fe7_diff`) and headless trace/checkpoint runner (`//runtime:fe7_headless`).
4. **Fallback Retirement & Modern Enhancements:** Once 100% of reachable game code runs natively in C++20, the ARM7TDMI fallback core is retired, unlocking native PC enhancements (16:9 widescreen, HD text, turn rewind, danger-zone overlay, mouse/gamepad support, and ImGui debug tooling).

## Current Progress

- **Phase 1 (Complete):** Bazel Bzlmod + Clang C++20 build, `GbaPtr<T>`, `DispatchTable`, SHA-1 ROM loader, `libmgba` HAL, SDL3 interactive frontend (`//runtime:fe7_native`), headless runner (`//runtime:fe7_headless`), CI, and coding-rule/format linters.
- **Phase 2 (Complete):** Trace recorder (`runtime/trace_recorder.{h,cc}`), differential harness (`runtime/diff_harness.{h,cc}`, `//runtime:fe7_diff`), FE8U/FE6J cross-matcher (`tools/match_fe8.py`), PyGhidra setup (`tools/ghidra_mcp/setup_ghidra.py`), and `tools/verify_fn.py`.
- **Phase 3 (In Progress):**
  - Canonical headers: `game/proc.h`, `game/unit.h`, central registration in `game/register_all.{h,cc}`.
  - **Wave 1 (`game/engine/`):** RNG (`rng.{h,cc}`, 10 functions), Game Clock (`clock.{h,cc}`, 4 functions), and 6C Proc Scheduler (`proc.{h,cc}`, 23 functions + 26-opcode bytecode interpreter).
  - **Wave 2 (`game/unit/`):** Unit lookup, lifecycle, and stat getters (`unit.{h,cc}`, 25 functions).

## Building & Testing

```bash
# Run all synthetic ASan/UBSan unit tests (no retail ROM required)
bazel test --config=asan --config=headless //...

# Style, agent guardrails, and Python tool tests
python3 tools/check_format.py
python3 tools/check_agent_rules.py
python3 -m unittest discover -s tools -p '*_test.py' -v

# Live differential verification against a local FE7 US ROM
bazel run --config=headless //runtime:fe7_diff -- --rom=/path/to/fe7_us.gba --fuzz_iters=256

# Headless boot & checkpoint comparison
bazel run --config=headless //runtime:fe7_headless -- --rom=/path/to/fe7_us.gba --frames=120 --dump_checkpoints

# Interactive desktop build (SDL3)
bazel run --config=release //runtime:fe7_native -- --rom=/path/to/fe7_us.gba
```

## License

All new `fe7` code is licensed under the [MIT License](LICENSE). Third-party `libmgba` patches remain under MPL-2.0 until fallback retirement.
