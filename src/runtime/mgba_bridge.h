#ifndef FE7_RUNTIME_MGBA_BRIDGE_H_
#define FE7_RUNTIME_MGBA_BRIDGE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ARMCore;
struct mCore;

extern int (*fe7_mgba_dispatch_hook)(struct ARMCore* cpu, uint32_t pc);

struct mCore* fe7_mgba_core_create(const uint8_t* rom_bytes, size_t rom_size,
                                   uint32_t* framebuffer, int width);
void fe7_mgba_core_destroy(struct mCore* core);
void fe7_mgba_core_step_frame(struct mCore* core, uint16_t key_input);

void fe7_mgba_get_gba_ptrs(struct mCore* core, uint8_t** ewram, uint8_t** iwram, uint8_t** rom,
                           size_t* rom_size, struct ARMCore** cpu);

void fe7_mgba_write_pc(struct ARMCore* cpu, uint32_t addr, int is_thumb);
void fe7_mgba_run_until_sentinel(struct ARMCore* cpu, uint32_t sentinel_lr, int max_steps);

void fe7_mgba_get_gprs(struct ARMCore* cpu, int32_t out_gprs[16]);
void fe7_mgba_set_gprs(struct ARMCore* cpu, const int32_t in_gprs[16]);
int32_t fe7_mgba_get_gpr(struct ARMCore* cpu, int reg);
void fe7_mgba_set_gpr(struct ARMCore* cpu, int reg, int32_t val);

void fe7_mgba_save_state(struct ARMCore* cpu, void* buf, size_t buf_size);
void fe7_mgba_restore_state(struct ARMCore* cpu, const void* buf, size_t buf_size);
size_t fe7_mgba_state_size(void);

uint8_t fe7_mgba_load8(struct ARMCore* cpu, uint32_t addr);
uint16_t fe7_mgba_load16(struct ARMCore* cpu, uint32_t addr);
uint32_t fe7_mgba_load32(struct ARMCore* cpu, uint32_t addr);
void fe7_mgba_store8(struct ARMCore* cpu, uint32_t addr, uint8_t val);
void fe7_mgba_store16(struct ARMCore* cpu, uint32_t addr, uint16_t val);
void fe7_mgba_store32(struct ARMCore* cpu, uint32_t addr, uint32_t val);

void fe7_mgba_swi16(struct ARMCore* cpu, uint8_t swi_num);

#ifdef __cplusplus
}
#endif

#endif  // FE7_RUNTIME_MGBA_BRIDGE_H_
