# ARM7TDMI Thumb-1 Assembly & C++20 Translation Guide

Nearly all game logic in FE7 US (`0x08000000`–`0x08FFFFFF`) was compiled by
Nintendo's GBA GCC toolchain into **16-bit Thumb-1** instructions. This guide
covers the registers, calling convention, core instructions, step-by-step
walkthroughs of real FE7 functions, and the subtle bit-exactness idioms needed
when translating Thumb-1 to C++20 in `game/`.

---

## 1. Registers & Calling Convention (ARM AAPCS)

The ARM7TDMI has 16 general-purpose 32-bit registers (`r0`–`r15`) plus the
status register (`CPSR`). Most Thumb-1 instructions can only address the **low
registers (`r0`–`r7`)**; only `mov`, `add`, `cmp`, and `bx` can access **high
registers (`r8`–`r15`)**.

| Register | Alias | Role in AAPCS | Preserved Across Call? |
| --- | --- | --- | --- |
| `r0` | `a1` | Argument 1 & **return value** | No (caller-saved) |
| `r1`–`r3` | `a2`–`a4` | Arguments 2–4 & scratch | No (caller-saved) |
| `r4`–`r7` | `v1`–`v4` | Local variables (low regs) | **Yes** (callee-saved via `push`/`pop`) |
| `r8`–`r11` | `v5`–`v8` | Local variables (high regs, rare in Thumb) | **Yes** (callee-saved) |
| `r12` | `ip` | Intra-procedure scratch | No |
| `r13` | `sp` | Stack Pointer (full-descending, 4-byte aligned) | **Yes** |
| `r14` | `lr` | Link Register (return address set by `bl`) | Saved on stack in non-leaf functions |
| `r15` | `pc` | Program Counter (`current_addr + 4` in Thumb) | — |

- **Arguments 5+:** Passed on the stack at `[sp, #0]`, `[sp, #4]`, ... before
  the callee's `push`. In `fe7`, every dispatch thunk (`fe7::NativeFn` in
  [`runtime/dispatch.h`](../runtime/dispatch.h)) receives these directly:
  ```cpp
  uint32_t ThunkExample(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                        const uint32_t* absl_nullable stack_args);
  ```
- **Condition flags (`CPSR`):** Almost all Thumb-1 ALU instructions end in `s`
  (`adds`, `subs`, `lsls`, `ands`, `muls`, etc.) because they unconditionally
  update the four condition flags:
  - `N` (Negative): bit 31 of result is 1
  - `Z` (Zero): result is 0
  - `C` (Carry): unsigned overflow / shift-out bit
  - `V` (oVerflow): signed 32-bit overflow

---

## 2. Thumb-1 Instruction Quick Reference

### Data Movement & Arithmetic

| Instruction | Operation | Notes |
| --- | --- | --- |
| `movs rd, #imm8` | `rd = imm8` (0..255) | Sets `N, Z` |
| `mov rd, rs` | `rd = rs` | Works on high regs (`r8`–`r15`) |
| `adds rd, rs, rn` / `#imm` | `rd = rs + rn` (or `imm`) | Sets `N, Z, C, V` |
| `subs rd, rs, rn` / `#imm` | `rd = rs - rn` (or `imm`) | Sets `N, Z, C, V` |
| `negs rd, rs` | `rd = -rs` (`0 - rs`) | Two's complement negate |
| `muls rd, rs` | `rd = rd * rs` (low 32 bits) | Wrapping 32-bit multiply |

### Shifts & Bitwise

| Instruction | Operation | Notes |
| --- | --- | --- |
| `lsls rd, rs, #imm5` | `rd = rs << imm5` | Logical shift left |
| `lsrs rd, rs, #imm5` | `rd = (uint32_t)rs >> imm5` | **Unsigned** shift right (zero-fill) |
| `asrs rd, rs, #imm5` | `rd = (int32_t)rs >> imm5` | **Signed** shift right (sign-extend) |
| `ands rd, rs` | `rd &= rs` | Bitwise AND |
| `orrs rd, rs` | `rd |= rs` | Bitwise OR |
| `eors rd, rs` | `rd ^= rs` | Bitwise XOR |
| `bics rd, rs` | `rd &= ~rs` | Bit clear (AND NOT) |
| `mvns rd, rs` | `rd = ~rs` | Bitwise NOT |

### Memory Loads & Stores

| Instruction | Width & Signedness | C/C++ Equivalent |
| --- | --- | --- |
| `ldr rd, [rb, #imm]` | 32-bit word | `*(uint32_t*)(rb + imm)` |
| `ldrh rd, [rb, #imm]` | 16-bit unsigned | `*(uint16_t*)(rb + imm)` |
| `ldrb rd, [rb, #imm]` | 8-bit unsigned | `*(uint8_t*)(rb + imm)` |
| `ldrsh rd, [rb, ro]` | 16-bit **signed** (register offset only) | `*(int16_t*)(rb + ro)` |
| `ldrsb rd, [rb, ro]` | 8-bit **signed** (register offset only) | `*(int8_t*)(rb + ro)` |
| `str` / `strh` / `strb` | 32 / 16 / 8-bit store | `*(uint{32,16,8}_t*)(...) = rs` |
| `ldr rd, [pc, #imm]` | 32-bit **literal pool** load | Loads a 32-bit constant/address stored near the function |
| `push {rlist, lr}` | Decrement `sp`, store regs | Function prologue |
| `pop {rlist, pc}` | Load regs, increment `sp` | Function epilogue (ARMv4T: `pop {pc}` does **not** switch mode; GCC emits `pop {r0}; bx r0` when interworking) |

### Comparisons & Branches

| Instruction | Condition | Meaning |
| --- | --- | --- |
| `cmp rn, rm` / `#imm8` | `rn - rm` (flags only) | Used before conditional branches |
| `tst rn, rm` | `rn & rm` (flags only) | Bitmask test (`(rn & rm) == 0` → `beq`) |
| `beq` / `bne` | `Z == 1` / `Z == 0` | `==` / `!=` |
| `blt` / `ble` / `bgt` / `bge` | Signed flags (`N, V, Z`) | Signed `<` / `<=` / `>` / `>=` |
| `bcc` (`blo`) / `bls` / `bhi` / `bcs` (`bhs`) | Unsigned flags (`C, Z`) | Unsigned `<` / `<=` / `>` / `>=` |
| `b label` | Unconditional | Local jump (`±2 KiB`) |
| `bl target` | Call subroutine | 32-bit pair; sets `lr = next_pc | 1` and jumps (`±4 MiB`) |
| `bx rm` | Return / indirect call | Jumps to `rm & ~1`, switches ARM/Thumb via `rm & 1` |

---

## 3. Walkthrough #1: Leaf Function (`RandNextB`, `0x08000EDC`)

Let's trace a compact real FE7 function from [`game/engine/rng.cc`](../game/engine/rng.cc):
`RandNextB()`, which advances the secondary 32-bit LCG at IWRAM `0x03000008`
(`gLCGRNValue`) and returns a 30-bit pseudo-random number.

### Annotated Thumb-1 Disassembly

```asm
; uint32_t RandNextB(void) — ROM 0x08000EDC
; Inputs:  none
; Outputs: r0 = next 30-bit LCG value
; Side effects: updates *(uint32_t*)0x03000008

08000EDC:  ldr   r2, [pc, #0x14]   ; r2 = *(uint32_t*)0x08000EF4 = 0x03000008 (&gLCGRNValue)
                                   ;   (Thumb PC = 0x08000EDC + 4 = 0x08000EE0; +0x14 = 0x08000EF4)
08000EDE:  ldr   r0, [r2, #0]      ; r0 = s = *state  (load 32-bit word from 0x03000008)
08000EE0:  lsls  r1, r0, #2        ; r1 = s << 2      (s * 4)
08000EE2:  adds  r0, r1, #2        ; r0 = (s * 4) + 2
08000EE4:  adds  r1, r1, #3        ; r1 = (s * 4) + 3
08000EE6:  muls  r0, r1            ; r0 = ((s * 4) + 2) * ((s * 4) + 3)  (mod 2^32)
08000EE8:  lsrs  r0, r0, #2        ; r0 = (uint32_t)r0 >> 2  (unsigned logical shift right!)
08000EEA:  str   r0, [r2, #0]      ; *state = r0  (store back to 0x03000008)
08000EEC:  bx    lr                ; return r0 to caller
08000EEE:  nop                     ; 2-byte alignment padding before 4-byte literal pool
...
08000EF4:  .word 0x03000008        ; Literal pool entry: kLcgRnValueAddr
```

### Step-by-Step Translation Notes

1. **PC-relative literal pool (`ldr r2, [pc, #0x14]`):** Thumb instructions are
   only 16 bits wide, so they cannot embed a 32-bit address like `0x03000008`.
   The compiler places the constant in a 4-byte-aligned literal pool right after
   the function and loads it relative to `(PC + 4) & ~3`.
2. **Wrapping multiply (`muls r0, r1`):** ARM7TDMI `muls` silently truncates to
   the low 32 bits. In C++20, signed 32-bit overflow is **Undefined Behavior**
   (flagged by `--config=asan` / UBSan), so we compute in `uint32_t`.
3. **Unsigned shift (`lsrs r0, r0, #2`):** `lsrs` (not `asrs`) shifts in zeros
   at bits 31–30, matching `uint32_t >> 2`.
4. **Leaf epilogue (`bx lr`):** Because `RandNextB` makes no `bl` calls and uses
   only `r0`–`r2`, it never touches the stack (`push`/`pop`).

### Translated C++20 ([`game/engine/rng.cc`](../game/engine/rng.cc))

```cpp
uint32_t RandNextB() {
  GbaPtr<uint32_t> state(kLcgRnValueAddr);
  const uint32_t s = *state;
  // Unsigned 32-bit multiply matches Thumb `muls` wraparound, followed by
  // logical shift right (`lsrs #2`).
  const uint32_t rn = (s * 4u + 2u) * (s * 4u + 3u);
  const uint32_t next = rn >> 2;
  *state = next;
  return next;
}
```

---

## 4. Walkthrough #2: Non-Leaf Function with Calls & Signed Division (`RandRoll2Rn`, `0x08000E84`)

Next, let's trace a non-leaf function that saves callee registers, makes two
subroutine calls, performs a signed divide-by-2, and returns a boolean:
`RandRoll2Rn(int threshold)` from [`game/engine/rng.cc`](../game/engine/rng.cc).

### Annotated Thumb-1 Disassembly

```asm
; bool RandRoll2Rn(int threshold) — ROM 0x08000E84
; Inputs:  r0 = threshold (0..100)
; Outputs: r0 = 1 if threshold > (rn1 + rn2) / 2, else 0

08000E84:  push  {r4, r5, lr}      ; Save callee-saved r4, r5 and return address lr on stack
08000E86:  movs  r5, r0            ; r5 = threshold  (preserve across bl calls)
08000E88:  bl    0x08000E04        ; r0 = RandNext100()  (first RN in [0, 99])
08000E8C:  movs  r4, r0            ; r4 = rn1
08000E8E:  bl    0x08000E04        ; r0 = RandNext100()  (second RN in [0, 99])
08000E92:  adds  r0, r4, r0        ; r0 = sum = rn1 + rn2
08000E94:  lsrs  r1, r0, #31       ; r1 = (sum < 0) ? 1 : 0  (sign bit for trunc-toward-zero)
08000E96:  adds  r0, r0, r1        ; r0 = sum + (sum < 0 ? 1 : 0)
08000E98:  asrs  r0, r0, #1        ; r0 = avg = sum / 2  (signed C division toward zero!)
08000E9A:  cmp   r5, r0            ; compare threshold (r5) vs avg (r0)
08000E9C:  ble   0x08000EA2        ; if (threshold <= avg) goto return_false
08000E9E:  movs  r0, #1            ; return true (1)
08000EA0:  b     0x08000EA4        ; goto epilogue
08000EA2:  movs  r0, #0            ; return_false: return false (0)
08000EA4:  pop   {r4, r5}          ; Restore callee-saved r4, r5
08000EA6:  pop   {r1}              ; Pop saved lr into scratch register r1
08000EA8:  bx    r1                ; Return to caller (preserving ARM/Thumb mode via bit 0)
```

### Step-by-Step Translation Notes

1. **Saving registers across `bl`:** `bl` clobbers `r0`–`r3` and `lr`, so the
   compiler saves `threshold` in `r5`, the first `RandNext100()` result in `r4`,
   and pushes `{r4, r5, lr}` on entry.
2. **Signed divide-by-2 idiom (`lsrs #31; adds; asrs #1`):** Arithmetic shift
   right (`asrs #1`) rounds toward $-\infty$ (so `-1 >> 1 == -1`), whereas C
   integer division rounds toward zero (`-1 / 2 == 0`). GCC adds the sign bit
   (`lsrs #31`) before `asrs #1` so the result matches C's `sum / 2`.
3. **ARMv4T interworking epilogue (`pop {r4, r5}; pop {r1}; bx r1`):** On
   ARM7TDMI (ARMv4T), `pop {pc}` does **not** honor bit 0 for ARM/Thumb
   interworking, so GCC pops `lr` into a scratch register (`r0` or `r1`) and
   executes `bx`.

### Translated C++20 ([`game/engine/rng.cc`](../game/engine/rng.cc))

```cpp
bool RandRoll2Rn(int threshold) {
  const int avg = (RandNext100() + RandNext100()) / 2;
  return threshold > avg;
}
```

---

## 5. Common Compiler Idioms & Bit-Exactness Pitfalls

When reading FE7 Thumb-1 disassembly and verifying with `fe7_diff`, watch for
these recurring patterns:

### 5.1 Narrow Type Truncation & Sign/Zero Extension

Thumb-1 has no `sxth`/`uxth` instructions (those were added in ARMv6). Instead,
GCC truncates and extends narrow values with **shift pairs**:

| Thumb Sequence | Meaning | C++20 Equivalent |
| --- | --- | --- |
| `lsls r0, r0, #24` / `lsrs r0, r0, #24` | Zero-extend 8-bit | `static_cast<uint8_t>(x)` |
| `lsls r0, r0, #24` / `asrs r0, r0, #24` | Sign-extend 8-bit | `static_cast<int8_t>(x)` |
| `lsls r0, r0, #16` / `lsrs r0, r0, #16` | Zero-extend 16-bit | `static_cast<uint16_t>(x)` |
| `lsls r0, r0, #16` / `asrs r0, r0, #16` | Sign-extend 16-bit | `static_cast<int16_t>(x)` |
| `lsls r0, r0, #19` / `lsrs r0, r0, #23` | Extract 9-bit unsigned bitfield (bits 4..12) | `(static_cast<uint32_t>(raw) << 19) >> 23` (used in `ComputeTacticianStarBonus()`) |

### 5.2 Signed 32-Bit Multiply Wraparound (`muls`)

In `RandNext(int max)` (`game/engine/rng.cc`), Thumb executes `muls r0, r1`
followed by a signed division by `0x10000`. If `max` is large (e.g., during
random fuzzing in `fe7_diff`), signed `int32_t * int32_t` triggers UBSan.
Always multiply in `uint32_t` first, then cast back to `int32_t`:

```cpp
const uint32_t raw_prod =
    static_cast<uint32_t>(NextRN()) * static_cast<uint32_t>(max);
const int32_t prod = static_cast<int32_t>(raw_prod);
return prod / 0x10000;
```

### 5.3 Negative Modulo + Bitmask (`ands`)

In `RandInit(int seed)` (`game/engine/rng.cc`), Thumb computes `seed % 7` and
then masks with `ands r2, #7`. For negative `seed`, C's `seed % 7` is negative
(e.g., `-1`), and masking in two's complement produces `7` (`0xFFFFFFFF & 7`),
never a negative array index:

```cpp
int mod = seed % 7;
seeds[0] = kRngInitTable[static_cast<uint32_t>(mod++) & 7u];
```

### 5.4 Void Function Epilogue Clobbering `r0`

When a `void` function returns via `pop {r0}; bx r0`, the original Thumb code
leaves its own return address (`lr`) in `r0`, whereas our C++20 thunk returns
`0u`. In `runtime/diff_runner.cc`, always set `DiffOptions::check_return_value =
false` for `void` functions.

### 5.5 Byte Shifts & `-Wconversion` / `-Wsign-conversion`

`FE7_GAME_COPTS` enables `-Werror -Wconversion -Wsign-conversion`. Always cast
`uint8_t` / `uint16_t` operands to `uint32_t` before left-shifting (e.g.,
`static_cast<uint32_t>(byte) << 8`) and cast explicitly when narrowing back to
`int16_t` or `uint16_t`.

### 5.6 Stored Function Pointers Need Thumb Bit 0

Any GBA struct field holding a function pointer (e.g., `Proc::proc_repeat_func`,
`Proc::proc_end_func`) must store the ROM address with bit 0 set (`kAddr... |
1u`) so fallback ARM7TDMI code calling it via `bx` enters Thumb mode.
