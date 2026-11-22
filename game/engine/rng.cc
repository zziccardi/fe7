#include "game/engine/rng.h"

#include <cstdint>

#include "absl/base/nullability.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

// 8-entry seed table from FE7 US ROM 0x080C5820 used by RandInit().
constexpr uint16_t kRngInitTable[8] = {
    0xA36Eu, 0x924Eu, 0xB784u, 0x4F67u, 0x8092u, 0x592Du, 0x8E70u, 0xA794u,
};

uint32_t ThunkNextRn(uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/,
                     uint32_t /*r3*/, const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(NextRN());
}

uint32_t ThunkRandInit(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                       uint32_t /*r3*/,
                       const uint32_t* absl_nullable /*stack*/) {
  RandInit(static_cast<int>(r0));
  return 0u;
}

uint32_t ThunkRandSetSt(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                        uint32_t /*r3*/,
                        const uint32_t* absl_nullable /*stack*/) {
  RandSetSt(GbaPtr<const uint16_t>(r0));
  return 0u;
}

uint32_t ThunkRandGetSt(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                        uint32_t /*r3*/,
                        const uint32_t* absl_nullable /*stack*/) {
  RandGetSt(GbaPtr<uint16_t>(r0));
  return 0u;
}

uint32_t ThunkRandNext100(uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/,
                          uint32_t /*r3*/,
                          const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(RandNext100());
}

uint32_t ThunkRandNext(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                       uint32_t /*r3*/,
                       const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(RandNext(static_cast<int>(r0)));
}

uint32_t ThunkRandRoll(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                       uint32_t /*r3*/,
                       const uint32_t* absl_nullable /*stack*/) {
  return RandRoll(static_cast<int>(r0)) ? 1u : 0u;
}

uint32_t ThunkRandRoll2Rn(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                          uint32_t /*r3*/,
                          const uint32_t* absl_nullable /*stack*/) {
  return RandRoll2Rn(static_cast<int>(r0)) ? 1u : 0u;
}

uint32_t ThunkRandInitB(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                        uint32_t /*r3*/,
                        const uint32_t* absl_nullable /*stack*/) {
  RandInitB(static_cast<int>(r0));
  return 0u;
}

uint32_t ThunkRandNextB(uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/,
                        uint32_t /*r3*/,
                        const uint32_t* absl_nullable /*stack*/) {
  return RandNextB();
}

}  // namespace

int NextRN() {
  GbaPtr<uint16_t> seeds(kRnSeedsAddr);
  // Combine high 5 bits of seeds[1] with low 11 bits of seeds[0], then mix
  // with a 1-bit rotated seeds[2] carrying the MSB of seeds[1].
  uint16_t rn = static_cast<uint16_t>((seeds[1] << 11) + (seeds[0] >> 5));
  seeds[2] = static_cast<uint16_t>(seeds[2] << 1);
  if ((seeds[1] & 0x8000u) != 0u) {
    seeds[2] = static_cast<uint16_t>(seeds[2] + 1u);
  }
  rn ^= seeds[2];

  seeds[2] = seeds[1];
  seeds[1] = seeds[0];
  seeds[0] = rn;
  return static_cast<int>(rn);
}

void RandInit(int seed) {
  GbaPtr<uint16_t> seeds(kRnSeedsAddr);
  // Masking the signed remainder as uint32_t (& 7u) matches Thumb `ands r2, #7`
  // even for negative seeds without relying on negative array indexing.
  int mod = seed % 7;
  seeds[0] = kRngInitTable[static_cast<uint32_t>(mod++) & 7u];
  seeds[1] = kRngInitTable[static_cast<uint32_t>(mod++) & 7u];
  seeds[2] = kRngInitTable[static_cast<uint32_t>(mod) & 7u];

  const int warmup = seed % 23;
  for (int i = 0; i < warmup; ++i) {
    NextRN();
  }
}

void RandSetSt(GbaPtr<const uint16_t> src) {
  GbaPtr<uint16_t> seeds(kRnSeedsAddr);
  seeds[0] = src[0];
  seeds[1] = src[1];
  seeds[2] = src[2];
}

void RandGetSt(GbaPtr<uint16_t> dst) {
  GbaPtr<const uint16_t> seeds(kRnSeedsAddr);
  dst[0] = seeds[0];
  dst[1] = seeds[1];
  dst[2] = seeds[2];
}

int RandNext100() { return (NextRN() * 100) / 0x10000; }

int RandNext(int max) {
  // Perform 32-bit multiplication unsigned to match ARM7TDMI `muls` wraparound
  // without C++ signed-overflow UB, then divide signed toward zero by 2^16.
  const uint32_t raw_prod =
      static_cast<uint32_t>(NextRN()) * static_cast<uint32_t>(max);
  const int32_t prod = static_cast<int32_t>(raw_prod);
  return prod / 0x10000;
}

bool RandRoll(int threshold) { return threshold > RandNext100(); }

bool RandRoll2Rn(int threshold) {
  const int avg = (RandNext100() + RandNext100()) / 2;
  return threshold > avg;
}

void RandInitB(int seed) { *GbaPtr<int32_t>(kLcgRnValueAddr) = seed; }

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

void RegisterRngFunctions() {
  RegisterNative(kAddrNextRn, &ThunkNextRn);
  RegisterNative(kAddrRandInit, &ThunkRandInit);
  RegisterNative(kAddrRandSetSt, &ThunkRandSetSt);
  RegisterNative(kAddrRandGetSt, &ThunkRandGetSt);
  RegisterNative(kAddrRandNext100, &ThunkRandNext100);
  RegisterNative(kAddrRandNext, &ThunkRandNext);
  RegisterNative(kAddrRandRoll, &ThunkRandRoll);
  RegisterNative(kAddrRandRoll2Rn, &ThunkRandRoll2Rn);
  RegisterNative(kAddrRandInitB, &ThunkRandInitB);
  RegisterNative(kAddrRandNextB, &ThunkRandNextB);
}

}  // namespace fe7
