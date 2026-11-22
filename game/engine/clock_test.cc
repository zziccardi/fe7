#include "game/engine/clock.h"

#include <array>
#include <cstdint>

#include "gtest/gtest.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

class ClockTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ewram_.fill(0);
    iwram_.fill(0);
    gMem.ewram = ewram_.data();
    gMem.iwram = iwram_.data();
    gMem.rom = nullptr;
    gMem.rom_size = 0;
    ResetDispatchTable();
  }

  void TearDown() override {
    ResetDispatchTable();
    gMem = GbaMemoryMap{};
  }

  alignas(16) std::array<uint8_t, 0x40000> ewram_;
  alignas(16) std::array<uint8_t, 0x8000> iwram_;
};

TEST_F(ClockTest, GetSetAndIncGameTimeWrapAt1000Hours) {
  SetGameTime(123u);
  EXPECT_EQ(GetGameTime(), 123u);
  IncGameTime();
  EXPECT_EQ(GetGameTime(), 124u);

  SetGameTime(kMaxGameTimeFrames);
  IncGameTime();
  EXPECT_EQ(GetGameTime(), kWrapGameTimeFrames);
}

TEST_F(ClockTest, FormatTimeSplitsHoursMinutesSecondsAndBlink) {
  GbaPtr<uint16_t> hours(0x02000100u);
  GbaPtr<uint16_t> minutes(0x02000102u);
  GbaPtr<uint16_t> seconds(0x02000104u);

  // 2 hours, 34 minutes, 56 seconds, + 30 frames (half-second blink = 1).
  const uint32_t frames = 2u * 216000u + 34u * 3600u + 56u * 60u + 30u;
  EXPECT_EQ(FormatTime(frames, hours, minutes, seconds), 1);
  EXPECT_EQ(*hours, 2u);
  EXPECT_EQ(*minutes, 34u);
  EXPECT_EQ(*seconds, 56u);

  EXPECT_EQ(FormatTime(0u, hours, minutes, seconds), 0);
  EXPECT_EQ(*hours, 0u);
  EXPECT_EQ(*minutes, 0u);
  EXPECT_EQ(*seconds, 0u);
}

TEST_F(ClockTest, RegisterClockFunctionsRoutesViaDispatch) {
  RegisterClockFunctions();
  GbaCall(kAddrSetGameTime, 500u);
  GbaCall(kAddrIncGameTime);
  EXPECT_EQ(GbaCall(kAddrGetGameTime), 501u);
}

}  // namespace
}  // namespace fe7
