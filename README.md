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
- **Phase 2 (Complete):** Trace recorder (`runtime/trace_recorder.{h,cc}`), differential harness (`runtime/diff_harness.{h,cc}`, `//runtime:fe7_diff`), FE8U/FE6J cross-matcher (`tools/match_fe8.py`), PyGhidra setup (`tools/ghidra_mcp/setup_ghidra.py`), `tools/verify_fn.py`, and two-tier E2E testing (`//testing/e2e/...` + `runtime/input_script.{h,cc}` + `tools/e2e_lockstep.py`).
- **Phase 3 (In Progress — 555 / 6,568 ROM functions differentially verified, 0 mismatches — *Tactical Gameplay & Enemy AI Complete*):**
  - Canonical headers: `game/proc.h`, `game/unit.h`, `game/item.h`, `game/battle.h`, central registration in `game/register_all.{h,cc}`.
  - **Wave 1 (`game/engine/`):** RNG (`rng`, 10), Game Clock (`clock`, 4), and 6C Proc Scheduler (`proc`, 23 + 26-opcode bytecode interpreter) — **37 functions**.
  - **Wave 2 (`game/unit/`, `game/item/`, `game/battle/`, `game/map/`):** Unit lookup/lifecycle/stat getters/overflow/PID stats (`unit`, 29), Items & weapon stats (`item`, 45), Battle formulas & WTA (`battle`, 20), Map & BFS pathfinding incl. IWRAM `MapFloodCoreRam` & `GetUnitMovementCost` (`map`, 18), plus Map range overlays, move-script backtracking & danger-zone helpers (`map_range`, 13) — **125 functions**.
  - **Wave 3 — Tactical Gameplay & Complete Enemy AI Engine (`game/battle/`, `game/phase/`, `game/support/`, `game/map/`, `game/action/`, `game/ai/`):** Battle simulation & hit unwinding (`battle_sim`, 22), Battle simulation/real-combat entrypoints (`battle_gen`, 9), Post-battle EXP/level-up/promotion/WEXP (`battle_exp`, 26), Turn flow, phase management, chapter events & flags (`phase`, 31), Supports & affinity bonuses (`support`, 22), Traps, obstacles, light runes & fog/entity maps (`trap`, 27), Ballista trap query/creation/mount/dismount (`ballista`, 8), Unit actions, rescue/drop/give, autolevel & stat boosters (`action`, 26), and the complete **Enemy AI Engine** (`ai` 28, `ai_decide` 24, `ai_script` 30, `ai_opcodes` 41, `ai_actions` 28, `ai_combat` 18, `ai_cp` 28, `ai_staff_loot` 33 — **230 AI functions** covering `0x08034E00..0x0803C100`) *(563 registered native functions, 555 unique verified in `fe7_diff`, 34 ASan test suites)*.

## Building & Testing

```bash
# Run all synthetic ASan/UBSan unit & pure-native E2E tests (no retail ROM required)
bazel test --config=asan --config=headless //...

# Style, agent guardrails, and Python tool tests
python3 tools/check_format.py
python3 tools/check_agent_rules.py
python3 -m unittest discover -s tools -p '*_test.py' -v

# Live differential verification against a local FE7 US ROM (555/555 functions)
bazel run --config=headless //runtime:fe7_diff -- --rom=/path/to/fe7_us.gba --fuzz_iters=256

# Headless lockstep & top-N fallback profiling (with optional .fe7in input script)
python3 tools/e2e_lockstep.py --rom=/path/to/fe7_us.gba --frames=240 --checkpoint_frames=120,240 --fallback_top=25

# Interactive desktop build (SDL3)
bazel run --config=release //runtime:fe7_native -- --rom=/path/to/fe7_us.gba
```

## Documentation

See [`docs/README.md`](docs/README.md) for the full documentation index:

- [GBA Hardware Reference](docs/gba_hardware.md) — ARM7TDMI CPU, 32-bit memory map, bus rules, PPU/DMA/BIOS SWIs, and `fe7` runtime bridge.
- [Thumb-1 Assembly & C++20 Translation](docs/thumb_assembly.md) — Calling convention, instruction reference, annotated FE7 function walkthroughs, and bit-exactness pitfalls.
- [FE7 US Engine & Business Logic](docs/fe7_business_logic.md) — Canonical RAM/ROM tables, RNG, 6C Proc scheduler, combat formulas, EXP/promotion, BFS pathfinding, phases, supports, and Enemy AI.
- [Decompilation & Verification Workflow](docs/decompilation_workflow.md) — End-to-end guide for adding and verifying native C++20 functions.
- [Implementation Plan](docs/implementation_plan.md) — Master Phase 1–5 roadmap.

## License

This repository uses a split license (see [LICENSE](LICENSE) for full details):

- **Original runtime, tooling & test suites (`runtime/`, `tools/`, `testing/`):** Licensed under the [GNU General Public License v3.0](LICENSE-GPLv3) (GPL-3.0).
- **Decompiled game logic (`game/`):** **Explicitly excluded** from GPL-3.0 (no license granted). Provided strictly for non-commercial educational, research, and preservation purposes with a legally obtained FE7 US ROM.
- **Third-party `libmgba` patches (`third_party/mgba/`):** Remain under MPL-2.0 until fallback retirement.

