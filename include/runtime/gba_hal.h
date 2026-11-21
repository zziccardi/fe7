#ifndef FE7_RUNTIME_GBA_HAL_H_
#define FE7_RUNTIME_GBA_HAL_H_

#include <cstddef>
#include <cstdint>

namespace fe7 {

struct RomImage;

inline constexpr int kViewportWidth = 240;
inline constexpr int kViewportHeight = 160;

namespace hal {

// Lifecycle & frame execution
bool Init(const RomImage& rom);
void Shutdown();
void StepFrame(uint16_t key_input);
const uint32_t* GetFramebuffer();

// Explicit MMIO access (0x04000000..0x040003FF)
uint8_t MmioRead8(uint32_t addr);
uint16_t MmioRead16(uint32_t addr);
uint32_t MmioRead32(uint32_t addr);
void MmioWrite8(uint32_t addr, uint8_t value);
void MmioWrite16(uint32_t addr, uint16_t value);
void MmioWrite32(uint32_t addr, uint32_t value);

// Explicit graphics memory writes (PAL: 0x05000000, VRAM: 0x06000000, OAM: 0x07000000)
void PalWrite(uint32_t dst_addr, const void* src, size_t size_bytes);
void VramWrite(uint32_t dst_addr, const void* src, size_t size_bytes);
void OamWrite(uint32_t dst_addr, const void* src, size_t size_bytes);

// Explicit DMA transfer
void DmaCopy(int channel, uint32_t src_addr, uint32_t dst_addr, uint32_t ctrl);

// GBA BIOS Software Interrupts (SWIs)
void SwiLz77UncompWram(uint32_t src_addr, uint32_t dst_addr);
void SwiLz77UncompVram(uint32_t src_addr, uint32_t dst_addr);
void SwiHuffUncomp(uint32_t src_addr, uint32_t dst_addr);
void SwiCpuFastSet(uint32_t src_addr, uint32_t dst_addr, uint32_t mode);
int32_t SwiDiv(int32_t numer, int32_t denom);
uint32_t SwiSqrt(uint32_t value);

// GBA SIO / Link Arena / Bonus Disk stub interface
namespace link {
void Init();
void Poll();
bool IsBonusDiskUnlocked();
void SetBonusDiskUnlocked(bool unlocked);
}  // namespace link

// Phase 5 native enhancement seams (no-ops during Phases 1-4 lockstep)
void VideoSubmitFrame();
void TextHookDrawGlyph(int x, int y, uint16_t glyph_id, uint16_t color);

}  // namespace hal
}  // namespace fe7

#endif  // FE7_RUNTIME_GBA_HAL_H_
