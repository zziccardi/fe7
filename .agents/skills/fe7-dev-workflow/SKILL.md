---
name: fe7-dev-workflow
description: Build, test, and documentation conventions for the zziccardi/fe7 Bazel C++20 project on the Linux cloud VM (Bazelisk setup, JVM trustStore flags, libclang-rt-18-dev for ASan, and commenting standards). Use whenever building, testing, or modifying code in zziccardi/fe7.
---

# fe7 Development, Build & Documentation Workflow

## 1. Linux Cloud VM Environment Setup

Before running Bazel on a fresh VM for `zziccardi/fe7`:

1. **Install Bazelisk** (fetches Bazel `9.3.2` from `.bazelversion`):
   ```bash
   mkdir -p "$HOME/.local/bin"
   curl -fsSL -o "$HOME/.local/bin/bazel" \
     https://github.com/bazelbuild/bazelisk/releases/download/v1.25.0/bazelisk-linux-amd64
   chmod +x "$HOME/.local/bin/bazel"
   ```
2. **Install Clang 18 sanitizer runtime** (required for `--config=asan` linking `libclang_rt.asan_static-x86_64.a`):
   ```bash
   sudo apt-get update -qq && sudo apt-get install -y -qq libclang-rt-18-dev
   ```

## 2. Running Bazel Builds & Tests

Bazel's embedded JDK does not include the VM's system CA bundle (`PKIX path building failed` when fetching Bzlmod/http_archive deps). Always pass the system Java trustStore startup flags:

```bash
BAZEL="$HOME/.local/bin/bazel"
BAZEL_JVM_FLAGS=(
  --host_jvm_args=-Djavax.net.ssl.trustStore=/etc/ssl/certs/java/cacerts
  --host_jvm_args=-Djavax.net.ssl.trustStorePassword=changeit
)

# Default unit test run (ASan + UBSan + LSan + FE7_DEBUG_BOUNDS=1)
"$BAZEL" "${BAZEL_JVM_FLAGS[@]}" test --config=asan //...

# Also verify default and release (-O2 -DNDEBUG) builds when changing build files or asserts
"$BAZEL" "${BAZEL_JVM_FLAGS[@]}" test //...
"$BAZEL" "${BAZEL_JVM_FLAGS[@]}" test --config=release //...

# Enforce game/ C-with-types guardrails, 80-column Google formatting, & tool tests
python3 tools/check_agent_rules.py
python3 tools/check_format.py
python3 -m unittest discover -s tools -p '*_test.py' -v
```

## 3. Coding, Documentation & Commit Conventions

- **Package layout**: Headers, implementations, and unit tests are colocated in top-level Bazel packages (`runtime/` and `game/`). Include headers relative to workspace root (`#include "runtime/gba_ptr.h"`, `#include "game/..."`).
- **Naming**: Constants use `kCamelCase` (`kRomBaseAddr`); globals and file-scope variables use `gCamelCase` (`gMem`, `gNativeTable`, `gActiveUnit`).
- **Globals & `absl::NoDestructor`**: Never declare globals with non-trivial destructors. Wrap non-trivial runtime globals (e.g., `gFallbackCounts`, `gRomBytes`) in `absl::NoDestructor<T>`, and release heap capacity on teardown (e.g., `std::vector<uint8_t>().swap(*gRomBytes)`) so LSan stays clean.
- **Abseil**: `abseil-cpp` is available in `runtime/` (`absl::flat_hash_map`, `absl::Status`/`absl::StatusOr`, `absl::flags`, `absl::NoDestructor`, `absl_nonnull`/`absl_nullable`). In `game/`, only zero-cost header annotations (`absl_nonnull`/`absl_nullable`) are allowed—heap-allocating Abseil containers are forbidden during Phases 1–4.
- **Include-What-You-Use (IWYU)**: Include every header directly used in each file and remove unused includes.
- **Header files (`.h`)**: Always include a module-level comment block at the top explaining the module's purpose, invariants, and architectural role.
- **Public interfaces**: Document public constants, structs/classes, typedefs, and functions (parameters, return values, ownership/lifetime requirements).
- **Non-trivial implementations**: Include inline comments explaining *why* each non-trivial statement is performed (e.g., hardware bus rules, ARM7TDMI pipeline offsets, memory ownership order, ABI constraints), not merely restating what the code does.
- **C++20 style**: Google C++ Style Guide (`.clang-format`, 80-column limit), `-fno-exceptions`, `-fno-rtti`, `-Werror`. Follow `tools/agent_rules.md` for `game/`.
- **Commits**: Use Conventional Commits (`feat:`, `fix:`, `docs:`, `style:`, `refactor:`, `chore:`).

## 4. Runtime, Bazel & libmgba Gotchas

- `VFileFromConstMemory` in libmgba borrows the ROM byte pointer without copying; HAL (`runtime/mgba_hal.cc`) must keep an owned copy (`gRomBytes`) alive until after `fe7_mgba_core_destroy`.
- `mCoreInitConfig(core, NULL)` allocates config hash tables that `core->deinit(core)` does not free; always call `mCoreConfigDeinit(&core->config)` before `core->deinit(core)` (including error paths) to keep LSan clean.
- Because `fe7` targets compile with `-fno-rtti` while GoogleTest compiles with RTTI, `.bazelrc` keeps `--copt=-fno-sanitize=vptr` under `--config=asan` to prevent false-positive UBSan vptr errors.
- Under Bazel 9 + Clang + `--config=asan`, `absl/debugging:leak_check` includes `<sanitizer/lsan_interface.h>`, which fails `layering_check` unless `.bazelrc` sets `build:asan --features=-layering_check`.
- Keep `//runtime:gba_hal` as a header-only `cc_library` separate from `//runtime:mgba_hal`, and keep `runtime/BUILD.bazel` `default_visibility` restricted to `//runtime:__subpackages__` (exposing only `:gba_ptr`, `:gba_hal`, `:dispatch` to `//game:__subpackages__`) so `game/` targets never transitively depend on `@mgba`.
- On headless Linux VMs without SDL3 display libraries, pass `--config=headless` (e.g., `test --config=asan --config=headless //...`).

## 5. Decompilation & Differential Verification Workflow

- **Two-tier verification**: Every decompiled subsystem in `game/` needs both (1) synthetic GoogleTest unit tests (`*_test.cc`, zero retail ROM, run in CI under `--config=asan --config=headless`) and (2) live differential verification against the local gitignored `fe7_us.gba` via `//runtime:fe7_diff` and `//runtime:fe7_headless`:
  ```bash
  # Record live pre-call traces during pure fallback boot
  ./bazel-bin/runtime/fe7_headless --rom=fe7_us.gba --frames=120 \
    --register_native=false --record_traces=/tmp/wave.fe7t \
    --trace_addrs=0x08000BE0,...
  # Fast triage when debugging a crash/assert in fe7_diff (~2s, line-buffered)
  stdbuf -oL ./bazel-bin/runtime/fe7_diff --rom=fe7_us.gba --fuzz_iters=4 \
    2>&1 | tail -n 35
  # Verify 0 r0 mismatches and 0 EWRAM/IWRAM mismatches
  ./bazel-bin/runtime/fe7_diff --rom=fe7_us.gba --traces=/tmp/wave.fe7t \
    --fuzz_iters=256
  ```
- **Void Thumb epilogues**: Thumb-1 `void` functions return via `pop {r0}; bx r0`, clobbering `r0` with `lr`. Set `DiffOptions::check_return_value = false` for `void` functions in `fe7_diff`.
- **IWRAM stack scratch bytes**: ARM7TDMI prologues write local frames into upper IWRAM (`0x03007800..0x03007FFF`), whereas native C++20 uses the host stack. Both `DiffOptions::iwram_compare_bytes` and `fe7_headless --dump_checkpoints` compare `0x03000000..0x030077FF` (`0x7800` bytes) to avoid false stack-scratch mismatches.
- **Cycle-timing nuance**: Native functions that invoke heavy fallback callbacks (e.g., `ProcRun` calling LZ77 decompression via `GbaCall`) run in 0 GBA cycles inside `fe7_mgba_run_until_sentinel`, which can advance frame-boundary timing during heavy boot loads before converging to identical settled framebuffer/IWRAM state.
- **ARM7TDMI → C++20 bit-exactness**:
  - Perform wrapping 32-bit multiplies in `uint32_t` before casting to `int32_t` to match Thumb `muls` without triggering UBSan signed-overflow errors.
  - Mask signed remainders as `uint32_t` (`static_cast<uint32_t>(mod) & 7u`) to match Thumb `ands` on negative inputs without negative indexing.
  - Cast `uint8_t` to `uint32_t` before shifting left (`static_cast<uint32_t>(byte) << 8`) to avoid `-Wsign-conversion` / `-Wconversion` warnings under `FE7_GAME_COPTS`.
  - When storing function pointers in shared GBA structs (e.g., `Proc::proc_repeat_func`), store the exact GBA Thumb address (`kAddr... | 1u`) so un-decompiled ROM code inspecting the field stays binary-compatible.
  - `GbaPtr<T>` defers `std::is_trivially_copyable_v<T>` to dereference/arithmetic ops and constrains void ops with C++20 `requires`, allowing self-referential structs (`GbaPtr<Proc>`), forward declarations, and opaque `GbaPtr<const void>`.
  - `GbaPtr<T>` exposes `.get()` (host pointer) and public field `.addr` (`uint32_t` GBA address) — not `.raw()` or `.Host()`.
  - `NativeFn` dispatch thunks always take 5 arguments: `(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3, const uint32_t* absl_nullable stack_args)` (include `"absl/base/nullability.h"` and add `"@abseil-cpp//absl/base:nullability"` to `deps`).
- **Synthetic unit tests & `fe7_diff` traces**:
  - `tools/check_agent_rules.py` forbids `std::vector` in all of `game/` (including `*_test.cc`). For unit tests needing synthetic ROM tables (`kIInfoTableAddr = 0x08BE222C`, `PInfo`, `JInfo`), use a file-scope `alignas(16) std::array<uint8_t, 0x01000000u> gSyntheticRom{}`.
  - In `runtime/diff_runner.cc`, bound synthetic FE7 US item IDs to `% 0x9Fu` (`0x00..0x9E`; `0x9F` reads past `gItemData` and triggers `GbaPtr` region asserts).
  - For map/pathfinding and Triangle Attack traces, initialize padded 2D maps via `InitSyntheticMapBuffer` with impassable `0` padding borders, and populate `fe7::GetUnit(uid).addr` in EWRAM for any nonzero `uid` placed on `gMapUnit`. For Enemy AI position-scoring (`AiGetFriendZonePositionScoreComponent` reads `±3` tiles without bounds checks), pass `pad_rows=4, pad_cols=4` on `gMapUnit` so edge-of-map loops stay in-bounds.
  - Functions taking `>4` arguments (stack args, e.g., `AiSetDecision`, `AiUpdateDecision`) use `FuzzMode::kTracesOnly` in `runtime/diff_runner.cc` and must have thorough synthetic unit tests in `*_test.cc`.
  - Because `fe7_diff` steps 5 boot frames before running `DiffFunction()`, `InitArmFuncs` has already populated IWRAM ARM code, enabling direct differential verification of IWRAM-copied ARM routines like `MapFloodCoreRam` (`0x08004408`).
  - Verify exact side-effects against `corpus/fe7_us/asm/code.s` rather than assuming FE8 parity (e.g., `PInfo::bonus_con` vs `JInfo::base_con`, `InitBattleUnit` zeroing fields on both `gBattleActor` and `gBattleTarget`, `BattleGenerateRoundHits` capturing `attrs` before `BattleCheckBrave`).
- **Git & PR hygiene**: Check parent PR state (`gh pr view <N> --json state`) before stacking, pass `--base main --head <branch>` (or `--base <parent-branch> --head <branch>` when stacking on an open PR) to `gh pr create`, and never commit `*.gba`, `corpus/`, or `.ghidra/`.



