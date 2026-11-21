#include "runtime/gba_hal.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"
#include "runtime/rom_loader.h"
#include "src/runtime/mgba_bridge.h"

namespace fe7 {
namespace hal {
namespace {

constexpr int kArmRegR0 = 0;
constexpr int kArmRegR1 = 1;
constexpr int kArmRegR2 = 2;
constexpr int kArmRegR3 = 3;
constexpr int kArmRegSp = 13;
constexpr int kArmRegLr = 14;

mCore* g_core = nullptr;
ARMCore* g_cpu = nullptr;
std::vector<uint8_t> g_rom_bytes;
std::array<uint32_t, kViewportWidth * kViewportHeight> g_framebuffer{};
bool g_bonus_disk_unlocked = false;

int MgbaDispatchHook(struct ARMCore* cpu, uint32_t pc) {
  if (pc == kSentinelLr || pc < kRomBaseAddr || pc >= (kRomBaseAddr + kRomRegionSize)) {
    return 0;
  }
  std::array<uint32_t, 8> stack_args{};
  const uint32_t sp = static_cast<uint32_t>(fe7_mgba_get_gpr(cpu, kArmRegSp));
  for (size_t i = 0; i < stack_args.size(); ++i) {
    stack_args[i] = fe7_mgba_load32(cpu, sp + static_cast<uint32_t>(i * 4u));
  }

  uint32_t out_r0 = 0;
  const bool handled = TryDispatchFromCpu(
      pc, static_cast<uint32_t>(fe7_mgba_get_gpr(cpu, kArmRegR0)),
      static_cast<uint32_t>(fe7_mgba_get_gpr(cpu, kArmRegR1)),
      static_cast<uint32_t>(fe7_mgba_get_gpr(cpu, kArmRegR2)),
      static_cast<uint32_t>(fe7_mgba_get_gpr(cpu, kArmRegR3)), stack_args.data(), &out_r0);
  if (!handled) {
    return 0;
  }

  fe7_mgba_set_gpr(cpu, kArmRegR0, static_cast<int32_t>(out_r0));
  const uint32_t lr = static_cast<uint32_t>(fe7_mgba_get_gpr(cpu, kArmRegLr));
  fe7_mgba_write_pc(cpu, lr, (lr & 1u) != 0u ? 1 : 0);
  return 1;
}

uint32_t MgbaFallbackExec(uint32_t gba_addr, uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                          const uint32_t* stack_args, size_t stack_arg_count) {
  assert(g_cpu != nullptr && "GbaHal not initialized");

  std::vector<uint8_t> saved_state(fe7_mgba_state_size());
  fe7_mgba_save_state(g_cpu, saved_state.data(), saved_state.size());

  if (stack_arg_count > 0 && stack_args != nullptr) {
    uint32_t sp = static_cast<uint32_t>(fe7_mgba_get_gpr(g_cpu, kArmRegSp));
    sp -= static_cast<uint32_t>(stack_arg_count * 4u);
    fe7_mgba_set_gpr(g_cpu, kArmRegSp, static_cast<int32_t>(sp));
    for (size_t i = 0; i < stack_arg_count; ++i) {
      fe7_mgba_store32(g_cpu, sp + static_cast<uint32_t>(i * 4u), stack_args[i]);
    }
  }

  fe7_mgba_set_gpr(g_cpu, kArmRegR0, static_cast<int32_t>(r0));
  fe7_mgba_set_gpr(g_cpu, kArmRegR1, static_cast<int32_t>(r1));
  fe7_mgba_set_gpr(g_cpu, kArmRegR2, static_cast<int32_t>(r2));
  fe7_mgba_set_gpr(g_cpu, kArmRegR3, static_cast<int32_t>(r3));
  fe7_mgba_set_gpr(g_cpu, kArmRegLr, static_cast<int32_t>(kSentinelLr | 1u));
  fe7_mgba_write_pc(g_cpu, gba_addr, 1);

  constexpr int kMaxFallbackSteps = 10000000;
  fe7_mgba_run_until_sentinel(g_cpu, kSentinelLr, kMaxFallbackSteps);

  const uint32_t result = static_cast<uint32_t>(fe7_mgba_get_gpr(g_cpu, kArmRegR0));
  fe7_mgba_restore_state(g_cpu, saved_state.data(), saved_state.size());
  return result;
}

void InvokeSwi(uint8_t swi_num, uint32_t r0, uint32_t r1, uint32_t r2,
               uint32_t* out_r0 = nullptr) {
  assert(g_cpu != nullptr && "GbaHal not initialized");
  std::array<int32_t, 16> saved_gprs{};
  fe7_mgba_get_gprs(g_cpu, saved_gprs.data());

  fe7_mgba_set_gpr(g_cpu, kArmRegR0, static_cast<int32_t>(r0));
  fe7_mgba_set_gpr(g_cpu, kArmRegR1, static_cast<int32_t>(r1));
  fe7_mgba_set_gpr(g_cpu, kArmRegR2, static_cast<int32_t>(r2));
  fe7_mgba_swi16(g_cpu, swi_num);
  if (out_r0 != nullptr) {
    *out_r0 = static_cast<uint32_t>(fe7_mgba_get_gpr(g_cpu, kArmRegR0));
  }

  fe7_mgba_set_gprs(g_cpu, saved_gprs.data());
}

void WriteHalfwords(uint32_t dst_addr, const void* src, size_t size_bytes) {
  assert(g_cpu != nullptr && "GbaHal not initialized");
  const auto* bytes = static_cast<const uint8_t*>(src);
  for (size_t i = 0; i + 1 < size_bytes; i += 2) {
    const uint16_t val =
        static_cast<uint16_t>(bytes[i]) | static_cast<uint16_t>(bytes[i + 1] << 8);
    fe7_mgba_store16(g_cpu, dst_addr + static_cast<uint32_t>(i), val);
  }
}

}  // namespace

bool Init(const RomImage& rom) {
  Shutdown();
  if (rom.bytes.empty()) {
    return false;
  }

  g_rom_bytes = rom.bytes;
  g_core = fe7_mgba_core_create(g_rom_bytes.data(), g_rom_bytes.size(), g_framebuffer.data(),
                                kViewportWidth);
  if (g_core == nullptr) {
    g_rom_bytes.clear();
    return false;
  }

  fe7_mgba_get_gba_ptrs(g_core, &g_mem.ewram, &g_mem.iwram, &g_mem.rom, &g_mem.rom_size, &g_cpu);

  fe7_mgba_dispatch_hook = &MgbaDispatchHook;
  SetFallbackExecutor(&MgbaFallbackExec);
  link::Init();
  return true;
}

void Shutdown() {
  fe7_mgba_dispatch_hook = nullptr;
  SetFallbackExecutor(nullptr);
  g_mem = GbaMemoryMap{};
  g_cpu = nullptr;
  if (g_core != nullptr) {
    fe7_mgba_core_destroy(g_core);
    g_core = nullptr;
  }
  g_rom_bytes.clear();
}

void StepFrame(uint16_t key_input) {
  assert(g_core != nullptr && "GbaHal not initialized");
  fe7_mgba_core_step_frame(g_core, key_input);
}

const uint32_t* GetFramebuffer() { return g_framebuffer.data(); }

uint8_t MmioRead8(uint32_t addr) {
  assert(g_cpu != nullptr);
  return fe7_mgba_load8(g_cpu, addr);
}

uint16_t MmioRead16(uint32_t addr) {
  assert(g_cpu != nullptr);
  return fe7_mgba_load16(g_cpu, addr);
}

uint32_t MmioRead32(uint32_t addr) {
  assert(g_cpu != nullptr);
  return fe7_mgba_load32(g_cpu, addr);
}

void MmioWrite8(uint32_t addr, uint8_t value) {
  assert(g_cpu != nullptr);
  fe7_mgba_store8(g_cpu, addr, value);
}

void MmioWrite16(uint32_t addr, uint16_t value) {
  assert(g_cpu != nullptr);
  fe7_mgba_store16(g_cpu, addr, value);
}

void MmioWrite32(uint32_t addr, uint32_t value) {
  assert(g_cpu != nullptr);
  fe7_mgba_store32(g_cpu, addr, value);
}

void PalWrite(uint32_t dst_addr, const void* src, size_t size_bytes) {
  WriteHalfwords(dst_addr, src, size_bytes);
}

void VramWrite(uint32_t dst_addr, const void* src, size_t size_bytes) {
  WriteHalfwords(dst_addr, src, size_bytes);
}

void OamWrite(uint32_t dst_addr, const void* src, size_t size_bytes) {
  WriteHalfwords(dst_addr, src, size_bytes);
}

void DmaCopy(int channel, uint32_t src_addr, uint32_t dst_addr, uint32_t ctrl) {
  assert(channel >= 0 && channel < 4);
  const uint32_t base = 0x040000B0u + static_cast<uint32_t>(channel) * 12u;
  MmioWrite32(base + 0u, src_addr);
  MmioWrite32(base + 4u, dst_addr);
  MmioWrite32(base + 8u, ctrl);
}

void SwiLz77UncompWram(uint32_t src_addr, uint32_t dst_addr) {
  InvokeSwi(0x11, src_addr, dst_addr, 0);
}

void SwiLz77UncompVram(uint32_t src_addr, uint32_t dst_addr) {
  InvokeSwi(0x12, src_addr, dst_addr, 0);
}

void SwiHuffUncomp(uint32_t src_addr, uint32_t dst_addr) {
  InvokeSwi(0x13, src_addr, dst_addr, 0);
}

void SwiCpuFastSet(uint32_t src_addr, uint32_t dst_addr, uint32_t mode) {
  InvokeSwi(0x0C, src_addr, dst_addr, mode);
}

int32_t SwiDiv(int32_t numer, int32_t denom) {
  uint32_t out = 0;
  InvokeSwi(0x06, static_cast<uint32_t>(numer), static_cast<uint32_t>(denom), 0, &out);
  return static_cast<int32_t>(out);
}

uint32_t SwiSqrt(uint32_t value) {
  uint32_t out = 0;
  InvokeSwi(0x08, value, 0, 0, &out);
  return out;
}

namespace link {
void Init() {}
void Poll() {}
bool IsBonusDiskUnlocked() { return g_bonus_disk_unlocked; }
void SetBonusDiskUnlocked(bool unlocked) { g_bonus_disk_unlocked = unlocked; }
}  // namespace link

void VideoSubmitFrame() {}

void TextHookDrawGlyph(int, int, uint16_t, uint16_t) {}

}  // namespace hal
}  // namespace fe7
