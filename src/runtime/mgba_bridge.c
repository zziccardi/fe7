#include "src/runtime/mgba_bridge.h"

#include <assert.h>
#include <string.h>

#include <mgba-util/vfs.h>
#include <mgba/core/config.h>
#include <mgba/core/core.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/internal/arm/isa-inlines.h>
#include <mgba/internal/gba/gba.h>

struct SavedArmState {
  int32_t gprs[16];
  union PSR cpsr;
  uint32_t prefetch[2];
  int32_t cycles;
  int32_t next_event;
  enum ExecutionMode mode;
};

struct mCore* fe7_mgba_core_create(const uint8_t* rom_bytes, size_t rom_size,
                                   uint32_t* framebuffer, int width) {
  struct mCore* core = mCoreCreate(mPLATFORM_GBA);
  if (core == NULL) {
    return NULL;
  }
  mCoreInitConfig(core, NULL);
  if (!core->init(core)) {
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    return NULL;
  }
  core->setVideoBuffer(core, (color_t*)framebuffer, (size_t)width);
  struct VFile* vf = VFileFromConstMemory(rom_bytes, rom_size);
  if (vf == NULL || !core->loadROM(core, vf)) {
    if (vf != NULL) {
      vf->close(vf);
    }
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
    return NULL;
  }
  core->reset(core);
  return core;
}

void fe7_mgba_core_destroy(struct mCore* core) {
  if (core != NULL) {
    mCoreConfigDeinit(&core->config);
    core->deinit(core);
  }
}

void fe7_mgba_core_step_frame(struct mCore* core, uint16_t key_input) {
  core->setKeys(core, key_input);
  core->runFrame(core);
}

void fe7_mgba_get_gba_ptrs(struct mCore* core, uint8_t** ewram, uint8_t** iwram, uint8_t** rom,
                           size_t* rom_size, struct ARMCore** cpu) {
  struct GBA* gba = (struct GBA*)core->board;
  *ewram = (uint8_t*)gba->memory.wram;
  *iwram = (uint8_t*)gba->memory.iwram;
  *rom = (uint8_t*)gba->memory.rom;
  *rom_size = gba->memory.romSize;
  *cpu = gba->cpu;
}

void fe7_mgba_write_pc(struct ARMCore* cpu, uint32_t addr, int is_thumb) {
  if (is_thumb) {
    cpu->executionMode = MODE_THUMB;
    cpu->cpsr.t = 1;
    cpu->gprs[ARM_PC] = (int32_t)(addr & ~1u);
    ThumbWritePC(cpu);
  } else {
    cpu->executionMode = MODE_ARM;
    cpu->cpsr.t = 0;
    cpu->gprs[ARM_PC] = (int32_t)(addr & ~3u);
    ARMWritePC(cpu);
  }
}

void fe7_mgba_run_until_sentinel(struct ARMCore* cpu, uint32_t sentinel_lr, int max_steps) {
  int steps = 0;
  while (steps < max_steps) {
    uint32_t pc_offset = (cpu->executionMode == MODE_THUMB) ? WORD_SIZE_THUMB : WORD_SIZE_ARM;
    uint32_t cur_pc = (uint32_t)cpu->gprs[ARM_PC] - pc_offset;
    if (cur_pc == sentinel_lr) {
      break;
    }
    cpu->nextEvent = cpu->cycles + 10000;
    ARMRun(cpu);
    ++steps;
  }
  assert(steps < max_steps && "Fallback execution exceeded step limit");
}

void fe7_mgba_get_gprs(struct ARMCore* cpu, int32_t out_gprs[16]) {
  memcpy(out_gprs, cpu->gprs, sizeof(cpu->gprs));
}

void fe7_mgba_set_gprs(struct ARMCore* cpu, const int32_t in_gprs[16]) {
  memcpy(cpu->gprs, in_gprs, sizeof(cpu->gprs));
}

int32_t fe7_mgba_get_gpr(struct ARMCore* cpu, int reg) { return cpu->gprs[reg]; }

void fe7_mgba_set_gpr(struct ARMCore* cpu, int reg, int32_t val) { cpu->gprs[reg] = val; }

size_t fe7_mgba_state_size(void) { return sizeof(struct SavedArmState); }

void fe7_mgba_save_state(struct ARMCore* cpu, void* buf, size_t buf_size) {
  assert(buf_size >= sizeof(struct SavedArmState));
  struct SavedArmState* s = (struct SavedArmState*)buf;
  memcpy(s->gprs, cpu->gprs, sizeof(s->gprs));
  s->cpsr = cpu->cpsr;
  s->prefetch[0] = cpu->prefetch[0];
  s->prefetch[1] = cpu->prefetch[1];
  s->cycles = cpu->cycles;
  s->next_event = cpu->nextEvent;
  s->mode = cpu->executionMode;
}

void fe7_mgba_restore_state(struct ARMCore* cpu, const void* buf, size_t buf_size) {
  assert(buf_size >= sizeof(struct SavedArmState));
  const struct SavedArmState* s = (const struct SavedArmState*)buf;
  memcpy(cpu->gprs, s->gprs, sizeof(cpu->gprs));
  cpu->cpsr = s->cpsr;
  cpu->prefetch[0] = s->prefetch[0];
  cpu->prefetch[1] = s->prefetch[1];
  cpu->cycles = s->cycles;
  cpu->nextEvent = s->next_event;
  cpu->executionMode = s->mode;
  cpu->memory.setActiveRegion(cpu, (uint32_t)cpu->gprs[ARM_PC]);
}

uint8_t fe7_mgba_load8(struct ARMCore* cpu, uint32_t addr) {
  return (uint8_t)cpu->memory.load8(cpu, addr, NULL);
}

uint16_t fe7_mgba_load16(struct ARMCore* cpu, uint32_t addr) {
  return (uint16_t)cpu->memory.load16(cpu, addr, NULL);
}

uint32_t fe7_mgba_load32(struct ARMCore* cpu, uint32_t addr) {
  return cpu->memory.load32(cpu, addr, NULL);
}

void fe7_mgba_store8(struct ARMCore* cpu, uint32_t addr, uint8_t val) {
  cpu->memory.store8(cpu, addr, (int8_t)val, NULL);
}

void fe7_mgba_store16(struct ARMCore* cpu, uint32_t addr, uint16_t val) {
  cpu->memory.store16(cpu, addr, (int16_t)val, NULL);
}

void fe7_mgba_store32(struct ARMCore* cpu, uint32_t addr, uint32_t val) {
  cpu->memory.store32(cpu, addr, (int32_t)val, NULL);
}

void fe7_mgba_swi16(struct ARMCore* cpu, uint8_t swi_num) { cpu->irqh.swi16(cpu, swi_num); }
