# Decompilation & Differential Verification Workflow

This guide explains the end-to-end workflow for translating a Thumb-1 function
from FE7 US (`AE7E`) into native C++20 in `game/` and proving bit-exact
equivalence against the original ROM.

---

## 1. Pick & Inspect a Target Function

1. **Profile remaining fallback hotspots** (requires a local gitignored
   `fe7_us.gba`):
   ```bash
   python3 tools/e2e_lockstep.py --rom=fe7_us.gba --frames=240 \
     --checkpoint_frames=120,240 --fallback_top=25
   ```
2. **Cross-reference & disassemble:** Check `corpus/` (`fe7_us/asm/code.s`,
   `fireemblem8u`, `fireemblem6j`) or Ghidra MCP to inspect the exact FE7 US
   Thumb-1 instructions and all RAM/ROM addresses touched. Never assume FE8
   parity without reading the FE7 US assembly.

---

## 2. Implement in Strict "C-with-types" C++20 (`game/`)

Follow [`tools/agent_rules.md`](../tools/agent_rules.md) and the Google C++
Style Guide (80-column limit, `-fno-exceptions`, `-fno-rtti`, `-Werror`):

1. **Declare the ROM address & C++ prototype** in the subsystem header
   (e.g., `game/battle/battle.h`):
   ```cpp
   inline constexpr uint32_t kAddrComputeBattleUnitSpeed = 0x08028B68u;
   void ComputeBattleUnitSpeed(GbaPtr<BattleUnit> bu);
   ```
2. **Implement using `GbaPtr<T>` and `hal::*`** in the `.cc` file:
   - Call un-decompiled helpers via `GbaCall(kAddr..., args...)`.
   - Use `uint32_t` for wrapping 32-bit multiplies (`muls`) and bitmasks on
     signed remainders (`ands`).
   - Store Thumb function pointers in GBA structs with bit 0 set (`addr | 1u`).
3. **Add a 5-argument `NativeFn` thunk and register it:**
   ```cpp
   uint32_t ThunkComputeBattleUnitSpeed(
       uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
       const uint32_t* absl_nullable /*stack*/) {
     ComputeBattleUnitSpeed(GbaPtr<BattleUnit>(r0));
     return 0u;
   }
   ```
   Register via `RegisterNative(kAddrComputeBattleUnitSpeed, &ThunkComputeBattleUnitSpeed)`
   inside the module's `Register*Functions()` called from
   [`game/register_all.cc`](../game/register_all.cc).

---

## 3. Tier 1 Verification: Synthetic GoogleTest Unit Tests (No ROM)

Every decompiled module must have colocated `*_test.cc` tests that run in CI
under ASan + UBSan + LSan with **zero copyrighted ROM data**:

- Allocate synthetic EWRAM/IWRAM/ROM buffers (use `std::array`, never
  `std::vector`, because `tools/check_agent_rules.py` forbids `std::vector` in
  `game/`), bind `gMem`, and test normal cases, boundary values, negative inputs,
  and FE7-specific edge cases.
- Run the full check suite:
  ```bash
  bazel test --config=asan --config=headless //...
  python3 tools/check_agent_rules.py
  python3 tools/check_format.py
  python3 -m unittest discover -s tools -p '*_test.py' -v
  ```

---

## 4. Tier 2 Verification: Live Differential Oracle (`fe7_diff`)

Register the new function in [`runtime/diff_runner.cc`](../runtime/diff_runner.cc)
and compare native C++20 execution directly against `libmgba`'s ARM7TDMI
execution on identical initial RAM/register snapshots:

1. **Configure `DiffOptions` accurately:**
   - Set `check_return_value = false` for `void` functions (Thumb `pop {r0}; bx r0`
     clobbers `r0` with `lr`).
   - Use `FuzzMode::kTracesOnly` for functions taking `>4` arguments (stack
     args), paired with thorough synthetic unit tests.
   - `iwram_compare_bytes` defaults to `0x7800` (`0x03000000..0x030077FF`) so
     ARM7TDMI stack-frame scratch bytes in upper IWRAM do not cause false diffs.
2. **Record live traces (optional) & run `fe7_diff`:**
   ```bash
   # Record live pre-call traces during pure fallback boot
   bazel run --config=headless //runtime:fe7_headless -- \
     --rom=fe7_us.gba --frames=120 --register_native=false \
     --record_traces=/tmp/wave.fe7t --trace_addrs=0x08028B68

   # Differential test: must report 0 r0 mismatches and 0 EWRAM/IWRAM mismatches
   bazel run --config=headless //runtime:fe7_diff -- \
     --rom=fe7_us.gba --traces=/tmp/wave.fe7t --fuzz_iters=256
   ```
3. **Run full-frame E2E lockstep** to confirm zero drift across frame
   checkpoints:
   ```bash
   python3 tools/e2e_lockstep.py --rom=fe7_us.gba --frames=240 \
     --checkpoint_frames=120,240
   ```
