// FE7 US (AE7E) pseudo-random number generators (primary LFSR + secondary LCG).
//
// FE7 uses two independent PRNGs backed by fixed IWRAM state:
//   1. Primary 48-bit shift-register RNG (`gRNSeeds[3]` at 0x03000000), used
//      for hit/crit rolls, level-up growths, and gameplay randomness.
//   2. Secondary 32-bit LCG (`gLCGRNValue` at 0x03000008), used for visual
//      effects and non-gameplay randomness.
//
// All 10 functions (ROM 0x08000BE0..0x08000EDC) operate directly on IWRAM via
// GbaPtr<T> so native C++20 and fallback ARM7TDMI code stay bit-identical.

#ifndef FE7_GAME_ENGINE_RNG_H_
#define FE7_GAME_ENGINE_RNG_H_

#include <cstdint>

#include "runtime/gba_ptr.h"

namespace fe7 {

// Fixed IWRAM addresses of the two FE7 US RNG state blocks.
inline constexpr uint32_t kRnSeedsAddr = 0x03000000u;     // uint16_t[3]
inline constexpr uint32_t kLcgRnValueAddr = 0x03000008u;  // uint32_t

// FE7 US ROM addresses for the 10 RNG functions.
inline constexpr uint32_t kAddrNextRn = 0x08000BE0u;
inline constexpr uint32_t kAddrRandInit = 0x08000CA8u;
inline constexpr uint32_t kAddrRandSetSt = 0x08000D74u;
inline constexpr uint32_t kAddrRandGetSt = 0x08000DD0u;
inline constexpr uint32_t kAddrRandNext100 = 0x08000E04u;
inline constexpr uint32_t kAddrRandNext = 0x08000E30u;
inline constexpr uint32_t kAddrRandRoll = 0x08000E60u;
inline constexpr uint32_t kAddrRandRoll2Rn = 0x08000E84u;
inline constexpr uint32_t kAddrRandInitB = 0x08000EC0u;
inline constexpr uint32_t kAddrRandNextB = 0x08000EDCu;

// Advances the primary 3x16-bit RNG state and returns the next 16-bit value in
// [0, 65535]. Matches FE7 US `NextRN` (0x08000BE0).
int NextRN();

// Seeds the primary RNG from `seed` using FE7's 8-entry seed table and warms up
// by `seed % 23` iterations. Matches FE7 US `RandInit` (0x08000CA8).
void RandInit(int seed);

// Overwrites the 3 primary RNG state halfwords from `src[0..2]`.
// Matches FE7 US `RandSetSt` (0x08000D74).
void RandSetSt(GbaPtr<const uint16_t> src);

// Copies the 3 primary RNG state halfwords into `dst[0..2]`.
// Matches FE7 US `RandGetSt` (0x08000DD0).
void RandGetSt(GbaPtr<uint16_t> dst);

// Returns a uniform integer in [0, 99] derived from NextRN().
// Matches FE7 US `RandNext_100` (0x08000E04).
int RandNext100();

// Returns an integer in [0, max - 1] (or signed truncated `(NextRN() * max) /
// 65536` for arbitrary 32-bit `max`). Matches FE7 US `RandNext` (0x08000E30).
int RandNext(int max);

// Single-RN roll: returns true iff `threshold > RandNext100()`.
// Matches FE7 US `RandRoll` (0x08000E60).
bool RandRoll(int threshold);

// True-Hit (2-RN) roll: returns true iff `threshold > (rn1 + rn2) / 2`.
// Matches FE7 US `RandRoll2Rn` (0x08000E84).
bool RandRoll2Rn(int threshold);

// Seeds the secondary LCG state (`gLCGRNValue` at 0x03000008).
// Matches FE7 US `RandInitB` (0x08000EC0).
void RandInitB(int seed);

// Advances and returns the next 30-bit value from the secondary LCG.
// Matches FE7 US `RandNextB` (0x08000EDC).
uint32_t RandNextB();

// Registers all 10 native RNG implementations in the global dispatch table.
void RegisterRngFunctions();

}  // namespace fe7

#endif  // FE7_GAME_ENGINE_RNG_H_
