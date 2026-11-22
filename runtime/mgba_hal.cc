#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/base/nullability.h"
#include "runtime/dispatch.h"
#include "runtime/gba_hal.h"
#include "runtime/gba_ptr.h"
#include "runtime/mgba_bridge.h"
#include "runtime/rom_loader.h"

namespace fe7 {
namespace hal {
namespace {

constexpr int kArmRegR0 = 0;
constexpr int kArmRegR1 = 1;
constexpr int kArmRegR2 = 2;
constexpr int kArmRegR3 = 3;
constexpr int kArmRegSp = 13;
constexpr int kArmRegLr = 14;

mCore* absl_nullable gCore = nullptr;
ARMCore* absl_nullable gCpu = nullptr;
// Owned copy of the loaded ROM image; mGBA's VFileFromConstMemory borrows this
// pointer directly without copying, so it must outlive gCore. Wrapped in
// absl::NoDestructor to avoid non-trivial static destruction.
absl::NoDestructor<std::vector<uint8_t>> gRomBytes;
std::array<uint32_t, kViewportWidth * kViewportHeight> gFramebuffer{};
bool gBonusDiskUnlocked = false;

// Called by libmgba's patched branch instructions to intercept fallback CPU
// calls into decompiled native C++20 functions.
int MgbaDispatchHook(struct ARMCore* cpu, uint32_t pc) {
  // Never intercept returns to kSentinelLr (which terminates
  // fe7_mgba_run_until_sentinel) or branches outside Game Pak ROM (e.g.,
  // BIOS/IWRAM routines).
  if (pc == kSentinelLr || pc < kRomBaseAddr ||
      pc >= (kRomBaseAddr + kRomRegionSize)) {
    return 0;
  }
  // Snapshot up to 8 AAPCS stack-passed arguments from the GBA stack in case
  // the native function takes more than 4 parameters.
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
      static_cast<uint32_t>(fe7_mgba_get_gpr(cpu, kArmRegR3)),
      stack_args.data(), &out_r0);
  if (!handled) {
    // Return 0 so libmgba executes the original Thumb/ARM code at `pc`.
    return 0;
  }

  // Synthesize an AAPCS function return: place the return value in r0 and
  // branch back to LR, preserving Thumb/ARM mode via LR bit 0.
  fe7_mgba_set_gpr(cpu, kArmRegR0, static_cast<int32_t>(out_r0));
  const uint32_t lr = static_cast<uint32_t>(fe7_mgba_get_gpr(cpu, kArmRegLr));
  fe7_mgba_write_pc(cpu, lr, (lr & 1u) != 0u ? 1 : 0);
  return 1;
}

// Executes an un-decompiled GBA function on the fallback ARM7TDMI CPU on behalf
// of GbaCall().
uint32_t MgbaFallbackExec(uint32_t gba_addr, uint32_t r0, uint32_t r1,
                          uint32_t r2, uint32_t r3,
                          const uint32_t* absl_nullable stack_args,
                          size_t stack_arg_count) {
  assert(gCpu != nullptr && "GbaHal not initialized");

  // Save the current ARM7TDMI context so nested Native -> Fallback -> Native ->
  // Fallback calls do not clobber the outer caller's registers, CPSR, or
  // prefetch pipeline.
  std::vector<uint8_t> saved_state(fe7_mgba_state_size());
  fe7_mgba_save_state(gCpu, saved_state.data(), saved_state.size());

  if (stack_arg_count > 0 && stack_args != nullptr) {
    // Push extra arguments onto the GBA stack in AAPCS order (arg 4 at [sp+0],
    // arg 5 at [sp+4]...). SP is restored automatically when saved_state is
    // restored after the call returns.
    uint32_t sp = static_cast<uint32_t>(fe7_mgba_get_gpr(gCpu, kArmRegSp));
    sp -= static_cast<uint32_t>(stack_arg_count * 4u);
    fe7_mgba_set_gpr(gCpu, kArmRegSp, static_cast<int32_t>(sp));
    for (size_t i = 0; i < stack_arg_count; ++i) {
      fe7_mgba_store32(gCpu, sp + static_cast<uint32_t>(i * 4u), stack_args[i]);
    }
  }

  fe7_mgba_set_gpr(gCpu, kArmRegR0, static_cast<int32_t>(r0));
  fe7_mgba_set_gpr(gCpu, kArmRegR1, static_cast<int32_t>(r1));
  fe7_mgba_set_gpr(gCpu, kArmRegR2, static_cast<int32_t>(r2));
  fe7_mgba_set_gpr(gCpu, kArmRegR3, static_cast<int32_t>(r3));
  // Set LR to kSentinelLr | 1 (Thumb return bit) so `bx lr` / `pop {pc}` lands
  // on kSentinelLr in Thumb mode, letting fe7_mgba_run_until_sentinel detect
  // completion.
  fe7_mgba_set_gpr(gCpu, kArmRegLr, static_cast<int32_t>(kSentinelLr | 1u));
  fe7_mgba_write_pc(gCpu, gba_addr, 1);

  constexpr int kMaxFallbackSteps = 10000000;
  fe7_mgba_run_until_sentinel(gCpu, kSentinelLr, kMaxFallbackSteps);

  // Capture r0 before restoring the outer CPU state so we return the callee's
  // result.
  const uint32_t result =
      static_cast<uint32_t>(fe7_mgba_get_gpr(gCpu, kArmRegR0));
  fe7_mgba_restore_state(gCpu, saved_state.data(), saved_state.size());
  return result;
}

// Invokes a GBA BIOS SWI through mGBA's HLE BIOS while preserving all caller
// GPRs.
void InvokeSwi(uint8_t swi_num, uint32_t r0, uint32_t r1, uint32_t r2,
               uint32_t* absl_nullable out_r0 = nullptr) {
  assert(gCpu != nullptr && "GbaHal not initialized");
  // Save and restore GPRs because SWI handlers clobber r0..r3 and this may be
  // called mid-frame from native code running inside a fallback dispatch hook.
  std::array<int32_t, 16> saved_gprs{};
  fe7_mgba_get_gprs(gCpu, saved_gprs.data());

  fe7_mgba_set_gpr(gCpu, kArmRegR0, static_cast<int32_t>(r0));
  fe7_mgba_set_gpr(gCpu, kArmRegR1, static_cast<int32_t>(r1));
  fe7_mgba_set_gpr(gCpu, kArmRegR2, static_cast<int32_t>(r2));
  fe7_mgba_swi16(gCpu, swi_num);
  if (out_r0 != nullptr) {
    *out_r0 = static_cast<uint32_t>(fe7_mgba_get_gpr(gCpu, kArmRegR0));
  }

  fe7_mgba_set_gprs(gCpu, saved_gprs.data());
}

// Writes host memory into GBA graphics RAM (PAL/VRAM/OAM) in 16-bit
// little-endian halfwords, because the GBA graphics bus ignores or duplicates
// 8-bit byte writes.
void WriteHalfwords(uint32_t dst_addr, const void* absl_nonnull src,
                    size_t size_bytes) {
  assert(gCpu != nullptr && "GbaHal not initialized");
  const auto* bytes = static_cast<const uint8_t*>(src);
  for (size_t i = 0; i + 1 < size_bytes; i += 2) {
    const uint16_t val = static_cast<uint16_t>(bytes[i]) |
                         static_cast<uint16_t>(bytes[i + 1] << 8);
    fe7_mgba_store16(gCpu, dst_addr + static_cast<uint32_t>(i), val);
  }
}

}  // namespace

bool Init(const RomImage& rom) {
  // Always clean up any previous core instance so re-initialization cannot leak
  // resources.
  Shutdown();
  if (rom.bytes.empty()) {
    return false;
  }

  // Own a copy of the ROM bytes so callers do not have to keep RomImage alive
  // after Init returns.
  *gRomBytes = rom.bytes;
  gCore = fe7_mgba_core_create(gRomBytes->data(), gRomBytes->size(),
                               gFramebuffer.data(), kViewportWidth);
  if (gCore == nullptr) {
    std::vector<uint8_t>().swap(*gRomBytes);
    return false;
  }

  fe7_mgba_get_gba_ptrs(gCore, &gMem.ewram, &gMem.iwram, &gMem.rom,
                        &gMem.rom_size, &gCpu);

  // Wire both directions of the dispatch table once gMem and gCpu are valid.
  fe7_mgba_dispatch_hook = &MgbaDispatchHook;
  SetFallbackExecutor(&MgbaFallbackExec);
  link::Init();
  return true;
}

void Shutdown() {
  // Detach hooks and clear gMem before destroying gCore so no callback can
  // touch freed memory.
  fe7_mgba_dispatch_hook = nullptr;
  SetFallbackExecutor(nullptr);
  gMem = GbaMemoryMap{};
  gCpu = nullptr;
  if (gCore != nullptr) {
    fe7_mgba_core_destroy(gCore);
    gCore = nullptr;
  }
  // Free gRomBytes storage only after gCore is destroyed, since mGBA holds a
  // non-owning pointer to it. Swap with an empty vector to release heap
  // capacity because absl::NoDestructor skips ~vector() at exit.
  std::vector<uint8_t>().swap(*gRomBytes);
}

void StepFrame(uint16_t key_input) {
  assert(gCore != nullptr && "GbaHal not initialized");
  fe7_mgba_core_step_frame(gCore, key_input);
}

const uint32_t* absl_nonnull GetFramebuffer() { return gFramebuffer.data(); }

uint8_t MmioRead8(uint32_t addr) {
  assert(gCpu != nullptr);
  return fe7_mgba_load8(gCpu, addr);
}

uint16_t MmioRead16(uint32_t addr) {
  assert(gCpu != nullptr);
  return fe7_mgba_load16(gCpu, addr);
}

uint32_t MmioRead32(uint32_t addr) {
  assert(gCpu != nullptr);
  return fe7_mgba_load32(gCpu, addr);
}

void MmioWrite8(uint32_t addr, uint8_t value) {
  assert(gCpu != nullptr);
  fe7_mgba_store8(gCpu, addr, value);
}

void MmioWrite16(uint32_t addr, uint16_t value) {
  assert(gCpu != nullptr);
  fe7_mgba_store16(gCpu, addr, value);
}

void MmioWrite32(uint32_t addr, uint32_t value) {
  assert(gCpu != nullptr);
  fe7_mgba_store32(gCpu, addr, value);
}

void PalWrite(uint32_t dst_addr, const void* absl_nonnull src,
              size_t size_bytes) {
  WriteHalfwords(dst_addr, src, size_bytes);
}

void VramWrite(uint32_t dst_addr, const void* absl_nonnull src,
               size_t size_bytes) {
  WriteHalfwords(dst_addr, src, size_bytes);
}

void OamWrite(uint32_t dst_addr, const void* absl_nonnull src,
              size_t size_bytes) {
  WriteHalfwords(dst_addr, src, size_bytes);
}

void DmaCopy(int channel, uint32_t src_addr, uint32_t dst_addr, uint32_t ctrl) {
  assert(channel >= 0 && channel < 4);
  // GBA DMA0..DMA3 register blocks start at 0x040000B0 and are spaced 12 bytes
  // apart (SAD at +0, DAD at +4, CNT_L/CNT_H at +8). Writing CNT last triggers
  // the transfer.
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
  InvokeSwi(0x06, static_cast<uint32_t>(numer), static_cast<uint32_t>(denom), 0,
            &out);
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
bool IsBonusDiskUnlocked() { return gBonusDiskUnlocked; }
void SetBonusDiskUnlocked(bool unlocked) { gBonusDiskUnlocked = unlocked; }
}  // namespace link

void VideoSubmitFrame() {}

void TextHookDrawGlyph(int, int, uint16_t, uint16_t) {}

}  // namespace hal
}  // namespace fe7
