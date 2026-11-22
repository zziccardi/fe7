#include "game/engine/clock.h"

#include <cstdint>

#include "absl/base/nullability.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

uint32_t ThunkGetGameTime(uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/,
                          uint32_t /*r3*/,
                          const uint32_t* absl_nullable /*stack*/) {
  return GetGameTime();
}

uint32_t ThunkSetGameTime(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                          uint32_t /*r3*/,
                          const uint32_t* absl_nullable /*stack*/) {
  SetGameTime(r0);
  return 0u;
}

uint32_t ThunkIncGameTime(uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/,
                          uint32_t /*r3*/,
                          const uint32_t* absl_nullable /*stack*/) {
  IncGameTime();
  return 0u;
}

uint32_t ThunkFormatTime(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                         const uint32_t* absl_nullable /*stack*/) {
  const int8_t blink = FormatTime(r0, GbaPtr<uint16_t>(r1),
                                  GbaPtr<uint16_t>(r2), GbaPtr<uint16_t>(r3));
  return static_cast<uint32_t>(static_cast<int32_t>(blink));
}

}  // namespace

uint32_t GetGameTime() { return *GbaPtr<uint32_t>(kGameTimeAddr); }

void SetGameTime(uint32_t time) { *GbaPtr<uint32_t>(kGameTimeAddr) = time; }

void IncGameTime() {
  GbaPtr<uint32_t> clock(kGameTimeAddr);
  *clock += 1u;
  if (*clock > kMaxGameTimeFrames) {
    *clock = kWrapGameTimeFrames;
  }
}

int8_t FormatTime(uint32_t frame_total, GbaPtr<uint16_t> hours,
                  GbaPtr<uint16_t> minutes, GbaPtr<uint16_t> seconds) {
  // Store seconds, then minutes, then hours to match the exact write order of
  // FE7 US 0x08000F80 in case callers pass aliased pointers.
  *seconds = static_cast<uint16_t>((frame_total / 60u) % 60u);
  *minutes = static_cast<uint16_t>((frame_total / 3600u) % 60u);
  *hours = static_cast<uint16_t>(frame_total / 216000u);
  return static_cast<int8_t>((frame_total / 30u) & 1u);
}

void RegisterClockFunctions() {
  RegisterNative(kAddrGetGameTime, &ThunkGetGameTime);
  RegisterNative(kAddrSetGameTime, &ThunkSetGameTime);
  RegisterNative(kAddrIncGameTime, &ThunkIncGameTime);
  RegisterNative(kAddrFormatTime, &ThunkFormatTime);
}

}  // namespace fe7
