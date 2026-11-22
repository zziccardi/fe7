// Hardware Abstraction Layer (HAL) for Game Boy Advance hardware subsystems.
//
// Decouples decompiled C++20 game code from the underlying GBA emulation
// backend (currently libmgba). All hardware-facing operations—frame stepping,
// MMIO register access, graphics memory writes, DMA transfers, BIOS SWIs, and
// Link/SIO stubs—route through this interface so the fallback backend can
// eventually be swapped or retired without modifying game code.

#ifndef FE7_RUNTIME_GBA_HAL_H_
#define FE7_RUNTIME_GBA_HAL_H_

#include <cstddef>
#include <cstdint>

#include "absl/base/nullability.h"

namespace fe7 {

struct RomImage;

// Native GBA LCD viewport dimensions in pixels.
inline constexpr int kViewportWidth = 240;
inline constexpr int kViewportHeight = 160;

// Output PCM audio sample rate in Hz (stereo signed 16-bit interleaved).
inline constexpr int kAudioSampleRate = 48000;

// Active-high GBA keypad bitmask constants matching GBA KEYINPUT bit positions.
enum GbaKey : uint16_t {
  kKeyA = 1u << 0,
  kKeyB = 1u << 1,
  kKeySelect = 1u << 2,
  kKeyStart = 1u << 3,
  kKeyRight = 1u << 4,
  kKeyLeft = 1u << 5,
  kKeyUp = 1u << 6,
  kKeyDown = 1u << 7,
  kKeyR = 1u << 8,
  kKeyL = 1u << 9,
};

namespace hal {

// --- Lifecycle & frame execution ---

// Initializes the GBA hardware backend with a copy of `rom`, binds gMem backing
// pointers, and wires up the two-way dispatch hooks. Re-calling Init() shuts
// down any previous instance first. Returns false if `rom` is empty or backend
// initialization fails.
bool Init(const RomImage& rom);

// Tears down the active GBA hardware backend, unregisters dispatch hooks, and
// clears gMem. Safe to call even if Init() was not called or already shut down.
void Shutdown();

// Advances the GBA hardware by one video frame with the given active-high key
// input bitmask (bitwise OR of GbaKey values).
void StepFrame(uint16_t key_input);

// Returns a pointer to the latest rendered 32-bit XBGR8888 framebuffer
// (kViewportWidth * kViewportHeight pixels). Valid while the HAL is
// initialized.
const uint32_t* absl_nonnull GetFramebuffer();

// Drains up to `max_frames` stereo signed 16-bit PCM frames (2 int16_t samples
// per frame, interleaved L/R at kAudioSampleRate) into `out_stereo`. Returns
// the number of stereo frames actually written.
size_t ReadAudioSamples(int16_t* absl_nonnull out_stereo, size_t max_frames);

// --- Cartridge save RAM (.sav) persistence ---

// Restores `size` bytes of cartridge save RAM (SRAM/Flash/EEPROM) into the
// active backend. Returns true on success.
bool LoadSaveData(const uint8_t* absl_nonnull data, size_t size);

// Copies the active cartridge save RAM into `out_buf` (up to `max_bytes`) and
// returns the total save RAM size in bytes. Pass `nullptr, 0` to query the size
// without copying.
size_t CopySaveData(uint8_t* absl_nullable out_buf, size_t max_bytes);

// --- Explicit MMIO access (0x04000000..0x040003FF) ---

// Reads an 8-bit, 16-bit, or 32-bit value from a GBA memory-mapped I/O
// register.
uint8_t MmioRead8(uint32_t addr);
uint16_t MmioRead16(uint32_t addr);
uint32_t MmioRead32(uint32_t addr);

// Writes an 8-bit, 16-bit, or 32-bit value to a GBA memory-mapped I/O register.
void MmioWrite8(uint32_t addr, uint8_t value);
void MmioWrite16(uint32_t addr, uint16_t value);
void MmioWrite32(uint32_t addr, uint32_t value);

// --- Explicit graphics memory writes (PAL: 0x05000000, VRAM: 0x06000000, OAM:
// 0x07000000) ---

// Copies `size_bytes` from host memory `src` into GBA Palette RAM, VRAM, or OAM
// using 16-bit halfword writes to respect GBA graphics bus constraints.
void PalWrite(uint32_t dst_addr, const void* absl_nonnull src,
              size_t size_bytes);
void VramWrite(uint32_t dst_addr, const void* absl_nonnull src,
               size_t size_bytes);
void OamWrite(uint32_t dst_addr, const void* absl_nonnull src,
              size_t size_bytes);

// --- Explicit DMA transfer ---

// Programs GBA DMA channel `channel` (0..3) with source address, destination
// address, and control word (length + flags) via its MMIO registers, triggering
// the transfer immediately if enabled.
void DmaCopy(int channel, uint32_t src_addr, uint32_t dst_addr, uint32_t ctrl);

// --- GBA BIOS Software Interrupts (SWIs) ---

// SWI 0x11: Decompresses LZ77 data from `src_addr` to `dst_addr` (8-bit writes,
// suitable for WRAM).
void SwiLz77UncompWram(uint32_t src_addr, uint32_t dst_addr);

// SWI 0x12: Decompresses LZ77 data from `src_addr` to `dst_addr` (16-bit
// writes, safe for VRAM).
void SwiLz77UncompVram(uint32_t src_addr, uint32_t dst_addr);

// SWI 0x13: Decompresses Huffman-encoded data from `src_addr` to `dst_addr`.
void SwiHuffUncomp(uint32_t src_addr, uint32_t dst_addr);

// SWI 0x0C: Fast 32-byte-aligned memory copy/fill controlled by `mode` (word
// count + fill bit).
void SwiCpuFastSet(uint32_t src_addr, uint32_t dst_addr, uint32_t mode);

// SWI 0x06: Signed 32-bit integer division (`numer / denom`), returning the
// quotient.
int32_t SwiDiv(int32_t numer, int32_t denom);

// SWI 0x08: Integer square root of `value`, truncated toward zero.
uint32_t SwiSqrt(uint32_t value);

// --- GBA SIO / Link Arena / Bonus Disk stub interface ---

namespace link {
// Initializes serial I/O / link cable state (no-op stub until multiplayer/link
// support is added).
void Init();
// Polls link cable state once per frame (no-op stub).
void Poll();
// Returns whether Mario Kart: Double Dash!! Bonus Disk content is unlocked.
bool IsBonusDiskUnlocked();
// Sets whether Bonus Disk content is treated as unlocked.
void SetBonusDiskUnlocked(bool unlocked);
}  // namespace link

// --- Phase 5 native enhancement seams (no-ops during Phases 1-4 lockstep) ---

// Hook invoked after each frame is stepped so a native renderer can present or
// augment the frame.
void VideoSubmitFrame();

// Hook invoked when the game draws a text glyph, enabling future HD font
// rendering overlays.
void TextHookDrawGlyph(int x, int y, uint16_t glyph_id, uint16_t color);

}  // namespace hal
}  // namespace fe7

#endif  // FE7_RUNTIME_GBA_HAL_H_
