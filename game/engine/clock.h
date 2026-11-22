// FE7 US (AE7E) frame-based game clock and play-time formatting.
//
// FE7 tracks elapsed play time as a 32-bit 60 Hz frame counter (`gGameTime` at
// IWRAM 0x03000010), wrapping from 1000 hours back to 990 hours so the status
// screen play-time display never exceeds 999:59:59.

#ifndef FE7_GAME_ENGINE_CLOCK_H_
#define FE7_GAME_ENGINE_CLOCK_H_

#include <cstdint>

#include "runtime/gba_ptr.h"

namespace fe7 {

// Fixed IWRAM address of FE7 US `gGameTime` (uint32_t frame counter).
inline constexpr uint32_t kGameTimeAddr = 0x03000010u;

// FE7 US ROM addresses for the 4 game-clock functions.
inline constexpr uint32_t kAddrGetGameTime = 0x08000F14u;
inline constexpr uint32_t kAddrSetGameTime = 0x08000F2Cu;
inline constexpr uint32_t kAddrIncGameTime = 0x08000F48u;
inline constexpr uint32_t kAddrFormatTime = 0x08000F80u;

// Maximum frame count before IncGameTime() wraps (1000 hours - 1 frame at
// 60 fps = 216,000,000 - 1).
inline constexpr uint32_t kMaxGameTimeFrames = 0x0CDFE5FFu;

// Frame count restored on wrap (990 hours at 60 fps = 213,840,000).
inline constexpr uint32_t kWrapGameTimeFrames = 0x0CBEF080u;

// Returns the current 60 Hz frame counter (`gGameTime`).
// Matches FE7 US `GetGameTime` (0x08000F14).
uint32_t GetGameTime();

// Overwrites `gGameTime` with `time`.
// Matches FE7 US `SetGameTime` (0x08000F2C).
void SetGameTime(uint32_t time);

// Increments `gGameTime` by 1 frame, wrapping past 999:59:59 to 990:00:00.
// Matches FE7 US `IncGameTime` (0x08000F48).
void IncGameTime();

// Splits `frame_total` (60 Hz frames) into hours, minutes, and seconds, and
// returns a 0/1 half-second blink phase (`(frame_total / 30) & 1`).
// Matches FE7 US `FormatTime` (0x08000F80).
int8_t FormatTime(uint32_t frame_total, GbaPtr<uint16_t> hours,
                  GbaPtr<uint16_t> minutes, GbaPtr<uint16_t> seconds);

// Registers all 4 native clock functions in the global dispatch table.
void RegisterClockFunctions();

}  // namespace fe7

#endif  // FE7_GAME_ENGINE_CLOCK_H_
