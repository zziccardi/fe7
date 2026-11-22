// Internal C11 bridge between C++20 runtime/mgba_hal.cc and libmgba's C
// internals.
//
// libmgba's internal headers rely on C11 idioms and macros that do not compile
// cleanly under fe7's strict C++20 flags (-Werror, -Wconversion,
// -fno-exceptions). This thin C boundary isolates all direct libmgba struct
// access (mCore, GBA, ARMCore) behind a small, opaque C ABI.

#ifndef FE7_RUNTIME_MGBA_BRIDGE_H_
#define FE7_RUNTIME_MGBA_BRIDGE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ARMCore;
struct mCore;

// Dispatch hook symbol patched into libmgba's Thumb/ARM branch instructions
// (see third_party/mgba/dispatch_hook.patch). Called before executing a branch
// target `pc`; returns non-zero if a native C++20 implementation handled the
// call and updated CPU registers.
extern int (*fe7_mgba_dispatch_hook)(struct ARMCore* cpu, uint32_t pc);

// Creates, initializes, and resets a GBA mCore instance backed by `rom_bytes`
// (must remain alive until fe7_mgba_core_destroy) and rendering into
// `framebuffer` with stride `width` pixels. Returns NULL on allocation or
// initialization failure.
struct mCore* fe7_mgba_core_create(const uint8_t* rom_bytes, size_t rom_size,
                                   uint32_t* framebuffer, int width);

// Releases mCore configuration and tears down `core`. No-op if `core` is NULL.
void fe7_mgba_core_destroy(struct mCore* core);

// Applies `key_input` and advances `core` by one full GBA video frame.
void fe7_mgba_core_step_frame(struct mCore* core, uint16_t key_input);

// Extracts raw backing-memory pointers (EWRAM, IWRAM, ROM, ROM size) and the
// ARM7TDMI CPU handle from an initialized GBA mCore so g_mem and HAL operations
// can access them directly.
void fe7_mgba_get_gba_ptrs(struct mCore* core, uint8_t** ewram, uint8_t** iwram,
                           uint8_t** rom, size_t* rom_size,
                           struct ARMCore** cpu);

// Sets the ARM7TDMI execution mode (Thumb when `is_thumb` != 0, ARM otherwise),
// writes `addr` to PC with proper alignment, and refills the 3-stage
// instruction prefetch pipeline.
void fe7_mgba_write_pc(struct ARMCore* cpu, uint32_t addr, int is_thumb);

// Steps the ARM7TDMI CPU until the currently executing PC equals `sentinel_lr`,
// asserting if execution exceeds `max_steps` iterations to guard against
// runaway fallback loops.
void fe7_mgba_run_until_sentinel(struct ARMCore* cpu, uint32_t sentinel_lr,
                                 int max_steps);

// Copies all 16 general-purpose registers (r0..r15) out of or into `cpu`.
void fe7_mgba_get_gprs(struct ARMCore* cpu, int32_t out_gprs[16]);
void fe7_mgba_set_gprs(struct ARMCore* cpu, const int32_t in_gprs[16]);

// Reads or writes a single general-purpose register `reg` (0..15) on `cpu`.
int32_t fe7_mgba_get_gpr(struct ARMCore* cpu, int reg);
void fe7_mgba_set_gpr(struct ARMCore* cpu, int reg, int32_t val);

// Saves/restores the re-entrant ARM7TDMI execution context (GPRs, CPSR,
// prefetch queue, cycle counters, and execution mode) into a caller-provided
// buffer of at least fe7_mgba_state_size().
void fe7_mgba_save_state(struct ARMCore* cpu, void* buf, size_t buf_size);
void fe7_mgba_restore_state(struct ARMCore* cpu, const void* buf,
                            size_t buf_size);
size_t fe7_mgba_state_size(void);

// Performs 8/16/32-bit loads and stores through the full GBA memory bus
// (handling MMIO/VRAM/OAM).
uint8_t fe7_mgba_load8(struct ARMCore* cpu, uint32_t addr);
uint16_t fe7_mgba_load16(struct ARMCore* cpu, uint32_t addr);
uint32_t fe7_mgba_load32(struct ARMCore* cpu, uint32_t addr);
void fe7_mgba_store8(struct ARMCore* cpu, uint32_t addr, uint8_t val);
void fe7_mgba_store16(struct ARMCore* cpu, uint32_t addr, uint16_t val);
void fe7_mgba_store32(struct ARMCore* cpu, uint32_t addr, uint32_t val);

// Invokes a Thumb-mode GBA BIOS Software Interrupt (`swi_num`) directly via
// mGBA's HLE BIOS.
void fe7_mgba_swi16(struct ARMCore* cpu, uint8_t swi_num);

#ifdef __cplusplus
}
#endif

#endif  // FE7_RUNTIME_MGBA_BRIDGE_H_
