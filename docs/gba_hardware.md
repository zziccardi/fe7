# Game Boy Advance Hardware Reference

This guide summarizes the Game Boy Advance (GBA) hardware details that directly
affect decompiling and porting *Fire Emblem: The Blazing Blade* (FE7 US,
`AE7E`) into native C++20.

---

## 1. CPU: ARM7TDMI (ARMv4T)

| Property | Value |
| --- | --- |
| Architecture | ARMv4T (32-bit RISC, little-endian) |
| Clock | `16.777216 MHz` (`2^24` Hz; 280,896 cycles per 59.7275 Hz frame) |
| Instruction Sets | **ARM** (32-bit instructions, 4-byte aligned) and **Thumb-1** (16-bit instructions, 2-byte aligned) |
| Hardware Divide | **None** (uses BIOS `SWI 0x06` or GCC `__aeabi_idiv` / `__divsi3` helpers) |
| Floating Point | **None** (all math is integer / fixed-point) |
| Pipeline | 3-stage (Fetch / Decode / Execute) |

### ARM vs. Thumb & Bit-0 Interworking

Because the GBA Game Pak ROM bus is **16 bits wide**, executing 32-bit ARM
instructions directly from ROM incurs two bus fetches per instruction. As a
result, nearly all FE7 code in ROM (`0x08000000`–`0x08FFFFFF`) is compiled as
**16-bit Thumb-1**.

- **Interworking bit (bit 0):** The `BX` (Branch and Exchange) instruction uses
  bit 0 of the target address to select the CPU state:
  - Bit 0 = `1` → switch to **Thumb** mode (e.g., `0x08004A85` executes Thumb
    code at `0x08004A84`).
  - Bit 0 = `0` → switch to **ARM** mode (e.g., `0x03000000` in IWRAM).
- **FE7 implication:** Whenever native C++20 stores a function pointer into a
  shared GBA struct (such as `Proc::proc_repeat_func` or `Proc::proc_end_func`),
  it **must** store the exact Thumb address with bit 0 set (`kAddr... | 1u`) so
  un-decompiled ROM code branching to it via `bx` stays in Thumb mode.
  `RegisterNative()` and `LookupNative()` in `runtime/dispatch.h` mask off bit 0
  automatically.
- **Hot ARM routines in IWRAM:** A small set of performance-critical routines
  (e.g., `MapFloodCoreRam`, ROM `0x08004408`, copied into IWRAM by
  `InitArmFuncs` during boot) are compiled as 32-bit ARM and executed from
  32-bit IWRAM for maximum throughput.

### 3-Stage Pipeline (`PC` Offset)

When an instruction reads `pc` (`r15`), the value seen is **two instructions
ahead** of the currently executing instruction:

- **Thumb state:** `pc = current_addr + 4`
- **ARM state:** `pc = current_addr + 8`

This is why PC-relative literal pool loads (`ldr r0, [pc, #imm]`) compute their
target address as `((current_addr + 4) & ~3u) + imm`.

---

## 2. 32-Bit GBA Memory Map

The GBA exposes a flat 32-bit little-endian address space divided into regions
selected by the top byte (`addr >> 24`):

| Address Range | Size | Name | Bus Width | Wait States | Access in `fe7` |
| --- | --- | --- | --- | --- | --- |
| `0x00000000–0x00003FFF` | 16 KiB | System BIOS ROM | 32-bit | 0 | `hal::Swi*()` (read-protected from user code) |
| `0x02000000–0x0203FFFF` | 256 KiB | **EWRAM** (External Work RAM) | 16-bit | 2 | `GbaPtr<T>` (`gMem.ewram`) |
| `0x03000000–0x03007FFF` | 32 KiB | **IWRAM** (Internal Work RAM) | 32-bit | 0 | `GbaPtr<T>` (`gMem.iwram`) |
| `0x04000000–0x040003FE` | 1 KiB | **MMIO** (I/O Registers) | 16/32-bit | 0 | `hal::MmioRead{8,16,32}` / `hal::MmioWrite{8,16,32}` |
| `0x05000000–0x050003FF` | 1 KiB | **Palette RAM** (BG + OBJ) | 16-bit | 0 | `hal::PalWrite()` |
| `0x06000000–0x06017FFF` | 96 KiB | **VRAM** (Tile & Map Data) | 16-bit | 0 | `hal::VramWrite()` |
| `0x07000000–0x070003FF` | 1 KiB | **OAM** (128 Sprite Attrs) | 32-bit | 0 | `hal::OamWrite()` |
| `0x08000000–0x09FFFFFF` | Up to 32 MiB | **Game Pak ROM** (WS0) | 16-bit | 3/1 | `GbaPtr<const T>` (`gMem.rom`, FE7 US is 16 MiB) |
| `0x0A000000–0x0DFFFFFF` | Mirrors | Game Pak ROM (WS1 / WS2) | 16-bit | varies | Not used by FE7 data pointers |
| `0x0E000000–0x0E00FFFF` | 64 KiB | Cartridge **SRAM / Flash** | 8-bit | 4 | `hal::LoadSaveData()` / `hal::CopySaveData()` |

> **Why `GbaPtr<T>` restricts regions:** `ResolveGbaRaw()` in
> [`runtime/gba_ptr.h`](../runtime/gba_ptr.h) intentionally only resolves
> EWRAM (`0x02`), IWRAM (`0x03`), and ROM (`0x08`/`0x09`). Attempting to
> dereference MMIO, Palette, VRAM, or OAM through `GbaPtr<T>` asserts in debug
> builds so all hardware-visible side effects go through [`runtime/gba_hal.h`](../runtime/gba_hal.h).

---

## 3. Critical Hardware Rules & Gotchas

### 3.1 Graphics Memory Rejects 8-Bit Writes

Palette RAM (`0x05000000`), VRAM (`0x06000000`), and OAM (`0x07000000`) **do
not support 8-bit byte stores (`strb`)**. On real GBA hardware, an 8-bit write
to VRAM or Palette RAM writes the same byte to *both* halves of the containing
16-bit halfword, and an 8-bit write to OAM is silently ignored.

- `hal::PalWrite`, `hal::VramWrite`, and `hal::OamWrite` perform 16-bit
  halfword writes to honor this bus rule.
- This is also why the GBA BIOS provides two separate LZ77 decompression SWIs:
  - `SWI 0x11` (`hal::SwiLz77UncompWram`) writes 8 bits at a time (safe for EWRAM/IWRAM).
  - `SWI 0x12` (`hal::SwiLz77UncompVram`) buffers pairs of bytes and writes 16
    bits at a time (required for VRAM).

### 3.2 IWRAM Stack Region (`0x03007800–0x03007FFF`)

IWRAM holds both fast engine globals (`gRNSeeds`, `gMapFloodSt`,
`gActiveUnit`, ARM code buffers) and the ARM7TDMI CPU stacks at the top of the
32 KiB window:

- `0x03000000–0x030077FF` (`0x7800` bytes): Static globals, IWRAM ARM functions,
  and working buffers.
- `0x03007800–0x03007EFF`: User/System stack (`SP_usr` starts at `0x03007F00`,
  growing downward).
- `0x03007F00–0x03007FFF`: IRQ stack (`SP_irq` at `0x03007FA0`), BIOS IRQ flags
  (`0x03007FF8`), and user IRQ handler pointer (`0x03007FFC`).

**Decompilation nuance:** When original Thumb code runs on the fallback CPU, its
prologues (`push {r4-r7, lr}`) dirty upper IWRAM stack bytes, whereas native
C++20 uses the host machine stack. Both `fe7_diff` (`iwram_compare_bytes =
0x7800`) and `fe7_headless --dump_checkpoints` therefore compare `0x03000000–
0x030077FF` to avoid false positives from stack scratch bytes.

### 3.3 Alignment & Strict Aliasing

- ARM7TDMI word (`ldr`/`str`) and halfword (`ldrh`/`strh`) accesses must be
  4-byte and 2-byte aligned, respectively. Unaligned word loads rotate the
  loaded word rather than performing an unaligned read.
- `GbaPtr<T>` asserts `(addr % alignof(T)) == 0` in debug/ASan builds.
- Because `GbaPtr<T>` maps directly into `gMem.ewram`/`iwram`/`rom` buffers,
  `game/` compiles with `-fno-strict-aliasing` and assumes a little-endian host
  (x86-64 / ARM64).

---

## 4. PPU (Video), DMA, Timers, Input & BIOS

### PPU (2D Graphics)

- **Resolution:** `240 × 160` pixels (`fe7::kViewportWidth`,
  `fe7::kViewportHeight` — never hardcode `240`/`160` in `game/` so Phase 5
  widescreen seams remain intact).
- **Frame Timing:** 160 visible scanlines (HDraw = 960 cycles, HBlank = 272
  cycles) followed by 68 VBlank scanlines (total 228 scanlines = 280,896 cycles
  ≈ 59.73 Hz).
- **Tile/Background Engine:** Modes 0–2 are tiled (4 BG layers `BG0`–`BG3` in
  Mode 0, used extensively by FE7 for map tiles, UI windows, text, and
  battle/menu layers); Modes 3–5 are bitmapped.
- **Sprites (OAM):** Up to 128 hardware sprites (sizes 8×8 up to 64×64, 4bpp or
  8bpp palettes, optional 32 affine transformation matrices).
- **Colors:** 15-bit BGR555 (`0b0BBBBBGGGGGRRRRR`) in Palette RAM (256 BG
  entries + 256 OBJ entries). `hal::GetFramebuffer()` outputs 32-bit XBGR8888.

### DMA Channels (0–3)

GBA has 4 DMA channels (`0x040000B0–0x040000DF`), exposed via
`hal::DmaCopy(channel, src_addr, dst_addr, ctrl)`:

- **DMA 0:** Highest priority, internal RAM only (often used for HBlank effects).
- **DMA 1 / DMA 2:** FIFO audio streaming (Direct Sound A/B).
- **DMA 3:** General-purpose ROM/RAM/VRAM bulk transfers (tiles, palettes,
  decompressed assets).

### Keypad (`KEYINPUT`, `0x04000130`)

On GBA hardware, `KEYINPUT` is **active-low** (`0` = pressed, `1` = released).
In `fe7`, [`fe7::GbaKey`](../runtime/gba_hal.h) (`kKeyA` through `kKeyL`) is
**active-high** (`1` = pressed), matching `hal::StepFrame(uint16_t key_input)`.

| Bit | Mask | Button |
| --- | --- | --- |
| 0 | `0x0001` | A |
| 1 | `0x0002` | B |
| 2 | `0x0004` | Select |
| 3 | `0x0008` | Start |
| 4 | `0x0010` | Right |
| 5 | `0x0020` | Left |
| 6 | `0x0040` | Up |
| 7 | `0x0080` | Down |
| 8 | `0x0100` | R |
| 9 | `0x0200` | L |

### BIOS Software Interrupts (SWIs)

FE7 calls GBA BIOS routines via the `swi #imm` instruction. `runtime/gba_hal.h`
wraps the SWIs used by game code:

| SWI | `fe7::hal` Function | Purpose |
| --- | --- | --- |
| `0x06` | `hal::SwiDiv(numer, denom)` | Signed 32-bit integer division (`r0 / r1`) |
| `0x08` | `hal::SwiSqrt(value)` | Integer square root (truncated toward zero) |
| `0x0C` | `hal::SwiCpuFastSet(src, dst, mode)` | Fast 32-byte-aligned copy or fill |
| `0x11` | `hal::SwiLz77UncompWram(src, dst)` | LZ77 decompression (8-bit writes, WRAM) |
| `0x12` | `hal::SwiLz77UncompVram(src, dst)` | LZ77 decompression (16-bit writes, VRAM) |
| `0x13` | `hal::SwiHuffUncomp(src, dst)` | Huffman decompression |

---

## 5. How `fe7` Bridges GBA Hardware (Ship-of-Theseus Runtime)

During Phases 1–3, native C++20 code in `game/` and un-decompiled ROM Thumb code
in `libmgba` share the exact same GBA EWRAM, IWRAM, and ROM memory buffers:

```text
  game/*.cc (Native C++20)                libmgba ARM7TDMI (Fallback)
        │                                             │
        ├── GbaPtr<T> ──► gMem.{ewram,iwram,rom} ◄────┤
        ├── hal::*    ──► libmgba PPU/APU/DMA/BIOS ◄──┤
        └── GbaCall() ◄── DispatchTable (O(1)) ──────►┘
```

- **Fallback → Native:** A small patch in `third_party/mgba/dispatch_hook.patch`
  checks `TryDispatchFromCpu()` when branching to ROM addresses. If a native
  function is registered, it executes in C++20, writes the return value into
  `r0`, and sets `PC = LR`.
- **Native → Fallback:** `GbaCall(gba_addr, r0..r3)` checks `LookupNative()`. If
  un-decompiled, it marshals arguments into `libmgba`'s ARM7TDMI registers,
  places `kSentinelLr` (`0x08FFFFFE`) in `LR`, and runs the CPU until
  `PC == kSentinelLr`.
