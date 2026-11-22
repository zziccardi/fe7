# Fire Emblem 7 (US) — AI-Driven Non-Matching Native Port Implementation Plan

## Goal Description

Build a **faithful, 100% native C++20 PC port** of *Fire Emblem: The Blazing Blade* (**FE7 US, `AE7E`**, validated by SHA-1 `c735fdbb9e8abe19e0c6a44708df19acc962e204`) targeting **Windows, Linux (including Steam Deck), and macOS**, engineered from day one with clean seams for post-1.0 modern enhancements (**widescreen 16:9 viewport**, **HD text/fonts**, **fast-forward / save states / turn rewind**, **enemy danger-zone overlay**, **mouse/gamepad support**, and **ImGui debug tooling**).

Rather than attempting a "big bang" translation of ~3,500 functions before the game boots, this plan uses a **Ship-of-Theseus (incremental replacement)** architecture:

1. **Bootable on Week 1 (Mock-Testable Immediately):** Embed `libmgba` (MPL-2.0) behind an owned `GbaHal` interface to provide an accurate GBA hardware runtime (PPU, APU, DMA, timers, IRQs, BIOS SWIs) and an ARM7TDMI fallback CPU. Validate the entire bridge in Phase 1 with **synthetic Thumb unit tests (GoogleTest)** requiring no copyrighted ROM, then drop in any SHA-1-validated FE7 US ROM for live play.
2. **Incremental Native Replacement:** AI coding agents translate batches of 5–15 related functions into strict **Google-style "C-with-types" C++20** (`.h`/`.cc`), leveraging local read-only mirrors of `fireemblem8u` and `fireemblem6j` (which share ~65–75% of Intelligent Systems' GBA engine) plus headless **Ghidra MCP** for FE7-exclusive logic.
3. **Behavioral Proof Instead of Byte-Matching:** Every decompiled function is verified against the original Thumb machine code using a **per-function differential test harness** (recorded call traces + fuzzing + ASan/UBSan) and a **full-game TAS lockstep suite** asserting identical RNG state and RAM mutations against stock mGBA.
4. **Retire the Fallback:** Once 100% of reachable game code executes natively in C++20 across full-game replays, delete the ARM7TDMI fallback core and unlock Phase 5 native PC enhancements.

```mermaid
flowchart TD
    ROM["User-Provided ROM Path (Validated by SHA-1, Not Filename)"] --> MEM
    subgraph App["Native FE7 Executable (Google C++20 / Clang / SDL3)"]
        GAME["Decompiled Game Code (game/*.cc)"] --> DISP["Dispatch Table (gba_addr -> NativeFn)"]
        DISP -->|"Native C++ exists"| GAME
        DISP -->|"Not yet decompiled (Phases 1-3)"| FALLBACK["libmgba ARM7TDMI Fallback CPU"]
        GAME -->|"GbaPtr<T> (EWRAM/IWRAM/ROM)"| MEM["Virtual GBA Memory Bus (32-bit Address Space)"]
        FALLBACK --> MEM
        GAME -->|"Explicit hal::Mmio / hal::Dma / hal::Swi"| HAL["Owned GbaHal Interface"]
        MEM --> HAL
        HAL --> PPU["libmgba PPU/APU/DMA/BIOS (Swappable in Phase 5)"]
        PPU --> SDL["SDL3 Frontend + ImGui Debug & QoL Overlay"]
    end
```

---

## User Review Required

> [!IMPORTANT]
> **ROM Loading & Legal Boundary:** No copyrighted ROM bytes, extracted graphics, dialogue scripts, or music samples from *Fire Emblem 7* will be committed to the repository. The binary accepts **any ROM file path** via `--rom <path>` (or UI file picker/config) and validates **file size (16 MiB) + SHA-1 (`c735fdbb9e8abe19e0c6a44708df19acc962e204`)** rather than hardcoding a filename. All reference repos (`fireemblem8u`, `fireemblem6j`, `FireEmblem7J`, `fe7_us`) live in a gitignored `corpus/` directory for research and cross-matching only.

> [!IMPORTANT]
> **Google C++ Style Guide & Human-Gated Headers (`game/*.h`):**
> - All C++ code follows the **Google C++ Style Guide** (`.h`/`.cc` extensions, `Google` `clang-format`, `#ifndef FE7_..._H_` guards, `-fno-exceptions`, `-fno-rtti`, `PascalCase` types/functions, `kConstantName` constants, `gGlobalName` globals, `snake_case` struct fields/filenames).
> - Headers, implementations, and unit tests are colocated in top-level `runtime/` and `game/` Bazel packages.
> - To prevent struct-layout drift across parallel AI agent batches, **all canonical POD structs (`Unit`, `Proc`, `BattleUnit`, `BmSt`, etc.) and shared headers in `game/*.h` are human-reviewed before merge**, locked with `static_assert(sizeof(...))` and `static_assert(offsetof(...))` checks matching FE7 US's exact RAM layout.

> [!WARNING]
> **Strict Aliasing & Host Endianness Assumption:** `GbaPtr<T>` direct-maps into mGBA's little-endian EWRAM/IWRAM/ROM buffers for clean, readable C++ (`unit->cur_hp -= dmg;`). This requires compiling `game/` with `-fno-strict-aliasing`, assuming little-endian desktop targets (x86-64 / ARM64), and explicitly handling unaligned ROM struct reads via helper templates where needed.

> [!NOTE]
> **mGBA Dispatch Hook Maintenance:** Intercepting Thumb/ARM function calls inside `libmgba` requires a small (~150-line) patch on a pinned `libmgba` archive (hooking `ARMRunLoop` / Thumb branch targets + a clean re-entrant `CallFallback(addr, r0..r3)` bridge). Because the `libmgba` CPU fallback is deleted in Phase 4, this patch only needs to track a single pinned release tag.

---

## Resolved Decisions

- **License:** **MIT** for all new `fe7` code (with pinned `third_party/mgba` patches remaining file-level MPL-2.0 until Phase 4/5 retirement).
- **Build System:** **Bazel (Bzlmod)** with Clang + C++20, GoogleTest and Abseil (`abseil-cpp`) from BCR, strict `visibility` boundaries between `game/` and `runtime/`, and focused `BUILD.bazel` overlays for `libmgba` GBA core + `SDL3`.
- **Repository:** New **private** GitHub repository named `fe7`.

---

## Proposed Changes

Changes are organized into sequential phases.

---

### Component 0: Repository Creation (`fe7`)

#### [NEW] Private GitHub repository `fe7`
- Create a new **private** GitHub repository named `fe7` via `create_github_repository` (requires this conversation to be attached to a Project).
- Initialize with:
  - `.gitignore` (explicitly ignoring `*.gba`, `*.sav`, `corpus/`, `bazel-*`, `.ghidra/`)
  - `LICENSE` (MIT)
  - `README.md` (project overview, SHA-1 ROM requirement, architecture summary)

---

### Component 1: Build System (Bazel + Clang), Google C++ Tooling & Agent Guardrails (Phase 1)

Establish a deterministic, hermetic Bazel (Bzlmod) + Clang + C++20 build with per-target caching, strict `visibility` isolation, and automated CI linting for AI agents.

#### [NEW] `MODULE.bazel`, `.bazelrc`, & `BUILD.bazel`
- Pull `abseil-cpp`, `googletest`, `platforms`, and `rules_cc` via Bzlmod; wrap `libmgba`'s GBA core and `SDL3` with clean `third_party/` `BUILD.bazel` overlays.
- Require **C++20** and **Clang** (`--repo_env=CC=clang`, `--repo_env=CXX=clang++`).
- Enforce Google C++ Style Guide compiler flags + strict warnings in `.bazelrc` and `fe7_game_cc_library` Starlark macro:
  ```python
  # bazel/fe7_rules.bzl
  FE7_GAME_COPTS = [
      "-std=c++20",
      "-fno-exceptions",
      "-fno-rtti",
      "-fno-strict-aliasing",
      "-Wall", "-Wextra", "-Werror",
      "-Wsign-conversion",
      "-Wconversion",
      "-Wimplicit-fallthrough",
      "-Wshadow",
      "-Wno-unused-parameter",
  ]
  ```
- Enforce architectural isolation at the build-graph level: `//game/...` targets may only depend on `//game/...` and `//runtime:{gba_ptr,gba_hal,dispatch}` (never `//runtime:mgba_hal`, `//runtime:rom_loader`, `SDL3`, `ImGui`, or host OS headers).
- Bazel `.bazelrc` configs:
  - `--config=asan`: `-O1 -g -fsanitize=address,undefined -fno-sanitize=vptr -DFE7_DEBUG_BOUNDS=1 --features=-layering_check` (default for unit tests, `verify_fn.py`, and CI)
  - `--config=release`: `-O2 -DNDEBUG`
  - `--config=headless`: strips SDL3 window/audio deps for 1,000+ FPS TAS/differential testing

#### [NEW] `.clang-format`, `.clang-tidy`, & `tools/agent_rules.md`
- `.clang-format`: `BasedOnStyle: Google`, `ColumnLimit: 80`.
- **Strict Google "C-with-types" C++20 rules enforced by `clang-tidy` + `tools/check_agent_rules.py`:**
  - **Allowed in `game/`:** Free functions, POD structs with `static_assert(sizeof/offsetof)`, `GbaPtr<T>`, `GbaSpan<T>`, `ProcAs<T>()`, scoped enums, `constexpr` constants (`kCamelCase`), globals (`gCamelCase`), `kViewportWidth` / `kViewportHeight`, and zero-cost Abseil header annotations (`absl_nonnull`, `absl_nullable`).
  - **Forbidden in `game/`:** Exceptions, RTTI, inheritance/`virtual` methods, heap-allocating STL or Abseil containers (`std::vector`, `std::string`, `std::map`, `absl::flat_hash_map`, etc.) during Phases 1–4, inline `asm`, raw `reinterpret_cast` from `uint32_t`, hardcoded `240`/`160` screen dimensions in camera/UI layout logic.

---

### Component 2: Virtual GBA Memory Bus, `GbaPtr<T>`, ROM Validator, & Dispatch Layer (Phase 1)

Provide compile-time type safety on GBA pointers, hash-based ROM validation, and seamless two-way calls between native C++20 functions and the `libmgba` ARM7TDMI fallback CPU.

#### [NEW] `runtime/rom_loader.h` & `runtime/rom_loader.cc`
- Accepts any file path (`--rom=<path>`), verifies size (`16 * 1024 * 1024` bytes) and computes **SHA-1**, accepting `c735fdbb9e8abe19e0c6a44708df19acc962e204` (FE7 US `AE7E`), returning `absl::Status` / `absl::StatusOr<RomImage>`.
- Exposes a test-only `LoadSyntheticRom(std::span<const uint8_t>)` entry point for Phase 1 mock/unit tests so the full runtime + dispatch bridge is tested without requiring a retail ROM.

#### [NEW] `runtime/gba_ptr.h` & `runtime/gba_ptr.cc`
Zero-cost typed wrapper around 32-bit GBA virtual addresses with debug bounds & alignment assertions:

```cpp
#ifndef FE7_RUNTIME_GBA_PTR_H_
#define FE7_RUNTIME_GBA_PTR_H_

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "absl/base/nullability.h"

namespace fe7 {

struct GbaMemoryMap {
  uint8_t* absl_nullable ewram = nullptr;  // 0x02000000 (256 KiB)
  uint8_t* absl_nullable iwram = nullptr;  // 0x03000000 (32 KiB)
  uint8_t* absl_nullable rom = nullptr;    // 0x08000000 (16 MiB)
  size_t rom_size = 0;
};

extern GbaMemoryMap gMem;

inline void* absl_nullable ResolveGbaRaw(uint32_t addr, size_t size, size_t align) {
  assert(addr != 0 && "Null GBA pointer dereference");
  assert((addr % align) == 0 && "Unaligned GBA pointer dereference");
  const uint32_t region = addr >> 24;
  const uint32_t offset = addr & 0x00FFFFFFu;
  switch (region) {
    case 0x02:
      assert(offset + size <= 0x40000u && "EWRAM out of bounds");
      return gMem.ewram + offset;
    case 0x03:
      assert(offset + size <= 0x8000u && "IWRAM out of bounds");
      return gMem.iwram + offset;
    case 0x08:
    case 0x09:
      assert((addr - 0x08000000u) + size <= gMem.rom_size && "ROM out of bounds");
      return gMem.rom + (addr - 0x08000000u);
    default:
      assert(false && "Use hal::Mmio* or hal::Vram* for IO/VRAM/OAM/PAL regions");
      return nullptr;
  }
}

template <typename T>
struct GbaPtr {
  uint32_t addr = 0;

  constexpr GbaPtr() = default;
  constexpr explicit GbaPtr(uint32_t raw) : addr(raw) {}
  constexpr explicit operator bool() const { return addr != 0; }

  T* absl_nonnull get() const
    requires(!std::is_void_v<T>)
  {
    static_assert(std::is_trivially_copyable_v<T>,
                  "GbaPtr<T> requires trivially copyable POD");
    return static_cast<T*>(ResolveGbaRaw(addr, sizeof(T), alignof(T)));
  }
  T* absl_nonnull operator->() const
    requires(!std::is_void_v<T>)
  {
    return get();
  }
  std::add_lvalue_reference_t<T> operator*() const
    requires(!std::is_void_v<T>)
  {
    return *get();
  }
  // operator[] and operator+ perform 64-bit signed offset math and check
  // trivially-copyable POD on use so self-referential structs (GbaPtr<Proc>),
  // forward declarations, and opaque GbaPtr<const void> compile cleanly.
};

// Explicit typed cast for FE7 Proc* subclasses (replaces unsafe C void* casts).
template <typename SubProc, typename BaseProc>
inline GbaPtr<SubProc> ProcAs(GbaPtr<BaseProc> p) {
  return GbaPtr<SubProc>(p.addr);
}

}  // namespace fe7

#endif  // FE7_RUNTIME_GBA_PTR_H_
```

#### [NEW] `runtime/dispatch.h` & `runtime/dispatch.cc`
- O(1) lookup table mapping Thumb ROM halfword addresses (`0x08000000`–`0x08FFFFFF`) to registered native C++20 functions.
- Handles **two-way interop**:
  - **Fallback → Native:** When mGBA's ARM/Thumb core branches (`BL`, `BX`, `BLX`, or `Proc` callback) to a registered address, mGBA invokes the native wrapper (`r0..r3` + stack), stores the return value in `r0`, and sets `PC = LR`.
  - **Native → Fallback:** `GbaCall(gba_addr, args...)` invokes native C++ if registered, or saves registers, sets a sentinel return address in `LR`, runs mGBA's ARM7TDMI core until `PC == kSentinelLr`, and returns `r0`.
- Tracks per-address **fallback hit counters** (`absl::flat_hash_map`, `--report_fallbacks`) for Phase 4 zero-fallback auditing.

```mermaid
sequenceDiagram
    participant Native as Native C++ (game/)
    participant Disp as Dispatch Table
    participant CPU as libmgba ARM7TDMI Fallback

    Native->>Disp: GbaCall(0x0801A2B4, unit_ptr)
    alt Native implementation registered
        Disp->>Native: Call native C++ directly
        Native-->>Disp: return result
    else Not yet decompiled
        Disp->>CPU: Load r0..r3, LR=kSentinelLr, PC=0x0801A2B4
        CPU->>CPU: Execute Thumb instructions
        CPU-->>Disp: Hit PC == kSentinelLr (read r0)
    end
    Disp-->>Native: return result
```

---

### Component 3: Owned `GbaHal`, `libmgba` Bridge, SDL3/ImGui Frontend, & Mock Unit Tests (Phase 1)

#### [NEW] `runtime/gba_hal.h` & `runtime/mgba_hal.cc`
- Explicit hardware APIs:
  - `hal::MmioRead16` / `hal::MmioWrite16`, `hal::VramWrite` / `hal::PalWrite` / `hal::OamWrite`
  - `hal::DmaCopy(channel, src_addr, dst_addr, ctrl)`
  - `hal::SwiLz77UncompWram` / `hal::SwiLz77UncompVram` / `hal::SwiHuffUncomp` / `hal::SwiCpuFastSet` / `hal::SwiDiv` / `hal::SwiSqrt`
  - `hal::link::*` (clean stub interface for GBA SIO / Link Arena / optional Bonus Disk unlock toggle)
- **Phase 5 Seams preserved from Day 1:**
  - `constexpr int kViewportWidth = 240; constexpr int kViewportHeight = 160;`
  - `hal::VideoSubmitFrame()` and `hal::TextHookDrawGlyph()` seam points (no-ops during Phases 1–4 lockstep).

#### [NEW] `runtime/sdl_frontend.cc` & `runtime/tas_runner.cc`
- **SDL3 interactive frontend (`fe7_native`):** Uses `absl::flags`, 60 Hz pacing, integer/aspect scaling, keyboard + SDL3 Gamepad input, `.sav` Flash/SRAM persistence, fast-forward toggle, instant save-state slots, and optional **ImGui debug overlay** (live RNG state, active `Proc` tree, hovered unit inspector, native vs. fallback execution % meter).
- **Headless TAS runner (`fe7_headless`):** Uses `absl::flags`, runs `.vbm`/`.bk2`/JSON input movies without SDL video/audio, dumping per-frame RNG state, unit array checksums, and framebuffer SHA-256 checkpoints.

#### [NEW] Colocated `runtime/*_test.cc` (Phase 1 Mock / Synthetic Unit Tests via GoogleTest)
Validates the entire Phase 1 runtime **without needing a retail FE7 ROM**:
- `runtime/gba_ptr_test.cc`: EWRAM/IWRAM/ROM resolution, pointer arithmetic, `ProcAs<T>()`, death tests for null/unaligned/out-of-bounds/MMIO-region dereferences.
- `runtime/rom_loader_test.cc`: SHA-1 verification (rejects bad hash or wrong size regardless of filename; accepts valid hash under arbitrary filenames) via `absl::Status` / `absl::StatusOr`.
- `runtime/dispatch_test.cc` & `runtime/mgba_hal_test.cc`: Loads a **synthetic minimal GBA ROM** containing small hand-assembled Thumb functions (e.g., `ThumbAdd(a, b)`, `ThumbCallNativeHelper(x)`, `ThumbMutateEwramStruct(GbaPtr<MockUnit>)`) into `libmgba` and tests:
  1. `Native → Fallback` execution + return value & EWRAM mutation.
  2. `Fallback → Native` dispatch interception + re-entrant `Native → Fallback → Native` nesting.
  3. Fallback hit-counter reporting.
  4. Differential comparison between original Thumb execution and a C++20 reimplementation (validating the Phase 2 diff harness end-to-end on mock code!).

---

### Component 4: Reverse-Engineering Pipeline & Differential Oracle (Phase 2 — Complete)

Activated once you stage your SHA-1-validated FE7 US ROM locally:

#### [IMPLEMENTED] `tools/ghidra_mcp/setup_ghidra.py`
- Headless PyGhidra setup script analyzing the user's validated ROM and importing symbols from `StanHash/fe7_us` (`.ghidra/fe7_us_symbols.csv`) for disassembly, decompiled pseudo-C, xrefs, and call graphs.

#### [IMPLEMENTED] `tools/match_fe8.py` (+ `tools/match_fe8_test.py`)
- Indexes local read-only clones of `StanHash/fe7_us`, `FireEmblemUniverse/fireemblem8u`, `FireEmblemUniverse/fireemblem6j`, and `MokhaLeee/FireEmblem7J` (`corpus/`).
- Maps FE7 US functions to their closest FE8U/FE6J C equivalents using symbol maps + call-graph & constant/opcode similarity, generating gitignored `corpus/work_queue.json` ordered bottom-up by dependency depth.

#### [IMPLEMENTED] `runtime/trace_recorder.{h,cc}`, `runtime/diff_harness.{h,cc}`, `runtime/diff_runner.cc`, & `tools/verify_fn.py`
1. **Trace Capture:** `//runtime:fe7_headless --rom=<path> --register_native=false --record_traces=<path>.fe7t --trace_addrs=<hex_csv>` records pre-call execution snapshots (`r0–r3`, stack args, full EWRAM/IWRAM).
2. **Differential Execution (`//runtime:fe7_diff` & `tools/verify_fn.py`):**
   - Executes **original Thumb** via `CallFallbackDirect()` vs. **decompiled C++20** on recorded traces + deterministic fuzzed inputs.
   - Asserts **exact `r0` equality** (skipped for `void` Thumb epilogues via `DiffOptions::check_return_value`) and **byte-for-byte identical EWRAM and static IWRAM (`0x03000000..0x030077FF`, excluding dead ARM7TDMI stack scratch bytes)**.
   - Verified under `--config=asan --config=headless` (Clang + ASan + UBSan) alongside `tools/check_format.py`, `tools/check_agent_rules.py`, and colocated `tools/*_test.py` (`unittest`).

---

### Component 5: Subsystem-by-Subsystem AI Decompilation (Phases 3–4 — In Progress)

#### [IMPLEMENTED] `game/*.h` (Human-Gated Canonical Headers) & `game/register_all.{h,cc}`
Google-style POD structs layout-locked to FE7 US RAM (`game/proc.h`, `game/unit.h`, `game/item.h`, `game/battle.h`, verified by `game/headers_test.cc`), using `StanHash/fe7_us` canonical field names:

```cpp
// game/unit.h (excerpt)
#ifndef FE7_GAME_UNIT_H_
#define FE7_GAME_UNIT_H_

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "runtime/gba_ptr.h"

namespace fe7 {

struct PInfo;
struct JInfo;
struct UnitSprite;

struct Unit {
  GbaPtr<const PInfo> pinfo;            // 0x00: Character data in ROM
  GbaPtr<const JInfo> jinfo;            // 0x04: Class data in ROM
  int8_t level;                         // 0x08
  uint8_t exp;                          // 0x09
  uint8_t ai_flags;                     // 0x0A
  int8_t id;                            // 0x0B: Faction + 1-based slot index
  uint32_t flags;                       // 0x0C: kUnitFlag* bitmask
  int8_t x;                             // 0x10
  int8_t y;                             // 0x11
  int8_t max_hp;                        // 0x12
  int8_t hp;                            // 0x13
  int8_t pow;                           // 0x14
  int8_t skl;                           // 0x15
  int8_t spd;                           // 0x16
  int8_t def;                           // 0x17
  int8_t res;                           // 0x18
  int8_t lck;                           // 0x19
  int8_t bonus_con;                     // 0x1A
  uint8_t rescue;                       // 0x1B
  uint8_t ballista_trap;                // 0x1C
  int8_t bonus_mov;                     // 0x1D
  uint16_t items[kUnitItemCount];       // 0x1E
  uint8_t wexp[kUnitWexpCount];         // 0x28
  uint8_t status : 4;                   // 0x30
  uint8_t status_duration : 4;
  uint8_t torch : 4;                    // 0x31
  uint8_t barrier : 4;
  uint8_t supports[kUnitSupportCount];  // 0x32
  uint8_t support_bits;                 // 0x39
  uint8_t pad_3a[2];                    // 0x3A
  GbaPtr<UnitSprite> map_sprite;        // 0x3C
  uint16_t ai_config;                   // 0x40
  uint8_t ai_a;                         // 0x42
  uint8_t ai_a_pc;                      // 0x43
  uint8_t ai_b;                         // 0x44
  uint8_t ai_b_pc;                      // 0x45
  uint8_t unk_46;                       // 0x46
  uint8_t pad_47;                       // 0x47
};

static_assert(sizeof(Unit) == 0x48, "Unit must be 0x48 bytes");
static_assert(offsetof(Unit, items) == 0x1E);
static_assert(offsetof(Unit, map_sprite) == 0x3C);
static_assert(std::is_trivially_copyable_v<Unit>);

}  // namespace fe7

#endif  // FE7_GAME_UNIT_H_
```

#### `game/` (Decompilation Waves — 492 / 6,568 ROM functions verified — *Core Enemy AI Engine Complete*)

| Wave | Directory | Scope | Status & Strategy |
| :--- | :--- | :--- | :--- |
| **Wave 1** | `game/engine/` | `rng` (10), `clock` (4), `proc` (23 + 26-opcode interpreter), plus `hardware`, `dma`, `oam`, `bg`, `irq`, `main` (~10%) | **In Progress** (`rng`, `clock`, `proc` complete & verified; remaining: low-level `hardware`/`dma`/`oam`/`bg`/`irq`/`main`). |
| **Wave 2** | `game/unit/`, `game/item/`, `game/map/`, `game/battle/`, `game/save/` | Core gameplay data & math (`unit` 29, `item` 43, `battle` 20, `map` 18, `save`, ~25%) | **Mostly Complete** (`unit`, `item`, `battle`, `map` complete & verified; remaining: `game/save/`). |
| **Wave 3** | `game/battle/`, `game/phase/`, `game/support/`, `game/map/`, `game/action/`, `game/ai/` | Tactical gameplay & Core Enemy AI engine: `battle_sim` (22), `battle_exp` (26), `phase` + events/flags (31), `support` (22), `trap` traps/obstacles/light runes/fog/entity maps (27), `action` unit actions/rescue/drop/give/autolevel/stat boosters (26), and complete **Core Enemy AI Engine** (`ai` 28, `ai_decide` 24, `ai_script` 30, `ai_opcodes` 41, `ai_actions` 28, `ai_combat` 18, `ai_cp` 28 — 197 AI functions) | **Milestone Complete** (all tactical gameplay & core Enemy AI modules + pure-native multi-turn E2E skirmishes verified across 28 ASan test suites and 492/492 `fe7_diff` functions). |
| **Wave 4** | `game/audio/`, `game/save/` | `m4a` C driver + clean C++ `SoundMainRAM` mixer, `libc`/BIOS wrappers, save/suspend (`~10%`) | Planned next (`libmgba` APU runs `m4a` cleanly in fallback during Waves 1–3). |
| **Wave 5** | `game/ui/` | `talk`, `face`, `font`, menus, prep screen, shop, arena, world map, event interpreter, Lyn tutorials, Link Arena UI (~20%) | Use `kViewportWidth`/`kViewportHeight` and `hal::TextHookDrawGlyph` seams; stub low-level SIO via `hal::link`. |
| **Wave 6** | `game/banim/` | `ekrbattle`, `efx`, spell scripts, Quintessence, Fire Dragon, CGs, sound room, credits (~20%) | Highly parallelizable state-machine procs across agent PR batches. |

#### [DELETE] Fallback CPU Bridge (End of Phase 4)
- Once `--report_fallbacks` reports **0 fallback calls** across the full-game TAS suite, remove the `libmgba` ARM7TDMI CPU fallback execution path from the build.

---

### Component 6: Phase 5 Modern PC Enhancements (Post-1.0)

1. **QoL & UX Layer (`runtime/qol/`):** Turn rewind (compressed ring buffer of EWRAM/IWRAM snapshots), enemy danger-zone overlay, mouse hover/click-to-move, fast-forward (2×/4×/uncapped), instant save states, controller remapping.
2. **Widescreen (16:9) & HD Text (`runtime/renderer_hd/`):** Swap `libmgba`'s 240×160 PPU renderer behind `GbaHal` with an SDL3 GPU/OpenGL tile/sprite renderer expanding `kViewportWidth` to 16:9, and hook `hal::TextHookDrawGlyph()` to draw crisp native-resolution TTF glyphs over dialogue boxes and stat screens.

---

## Verification Plan

### Automated Tests

1. **Synthetic Unit, E2E Skirmish & Lint Suite (runs in CI with zero retail ROM):**
   ```bash
   bazel test --config=asan --config=headless //...
   python3 tools/check_format.py
   python3 tools/check_agent_rules.py
   python3 -m unittest discover -s tools -p '*_test.py' -v
   ```
   Covers `GbaPtr<T>` bounds/alignment, SHA-1 ROM validation, `DispatchTable` two-way Thumb↔C++ round-trips on synthetic Thumb payloads inside `libmgba`, `DiffFunction` harness, `.fe7in` input script parser, canonical `game/` header `static_assert`s, unit tests for every decompiled subsystem, and `//testing/e2e:skirmish_integration_test` (multi-turn pure-native combat level exercising all 11 subsystems with zero fallback).

2. **Per-PR Live Differential & Fuzz Verification (Phases 2–4, with local retail ROM):**
   ```bash
   bazel run --config=headless //runtime:fe7_diff -- \
     --rom=<path_to_rom> \
     --traces=/tmp/wave.fe7t \
     --fuzz_iters=256
   ```
   Asserts 100% exact `r0` return values (for non-void functions) and byte-identical EWRAM + static IWRAM (`0x03000000..0x030077FF`) vs. original Thumb in `libmgba` (**492/492** functions passing).

3. **Headless Lockstep & Fallback Profiling (`tools/e2e_lockstep.py`):**
   ```bash
   python3 tools/e2e_lockstep.py \
     --rom=<path_to_rom> \
     --input_script=<optional.fe7in> \
     --frames=240 \
     --checkpoint_frames=120,240 \
     --fallback_top=25
   ```
   Compares EWRAM, static IWRAM, and framebuffer FNV-1a hashes between `--register_native=false` (pure fallback) and `--register_native=true` (native C++20 active) and prints symbol-annotated top-N remaining fallback targets.

4. **Full-Game Lockstep Suite (Phase 4 gate):**
   ```bash
   bazel run --config=release --config=headless //runtime:fe7_headless -- \
     --rom=<path_to_rom> \
     --report_fallbacks \
     --assert_zero_fallbacks
   ```

### Manual Verification

- **Phase 1 Gate (once ROM is staged):** Launch `bazel run --config=release //runtime:fe7_native -- --rom=<path_to_rom>` on desktop; verify SHA-1 acceptance/rejection messaging, 60 FPS video/audio, keyboard & gamepad input, Prologue gameplay, save/reload persistence, and ImGui overlay toggling.
- **Wave Gates (Phases 3–4):** Spot-check subjective visual/audio fidelity (HBlank weather/water effects, battle animation transitions, `m4a` music/SFX mixing, Huffman dialogue boxes) alongside automated RNG/RAM lockstep checks.
- **Phase 5 Gate:** Verify 16:9 map camera panning without tile-edge artifacts, crisp HD text legibility across dialogue/stat/battle-forecast menus, mouse clicking, danger-zone overlay accuracy, and turn rewind stability.
