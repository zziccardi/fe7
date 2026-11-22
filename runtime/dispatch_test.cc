#include "runtime/dispatch.h"

#include <cstdint>

#include "gtest/gtest.h"

namespace fe7 {
namespace {

uint32_t MockNativeAdd(uint32_t r0, uint32_t r1, uint32_t, uint32_t, const uint32_t*) {
  return r0 + r1;
}

uint32_t MockNativeFiveArgs(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                            const uint32_t* stack_args) {
  return r0 + r1 + r2 + r3 + (stack_args != nullptr ? stack_args[0] : 0u);
}

uint32_t MockFallbackExec(uint32_t gba_addr, uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                          const uint32_t*, size_t) {
  if (gba_addr == 0x08002000u) {
    // Simulate Fallback -> Native re-entrant call.
    uint32_t out = 0;
    EXPECT_TRUE(TryDispatchFromCpu(0x08001001u, r0, r1, 0, 0, nullptr, &out));
    return out * 2u;
  }
  return gba_addr ^ r0;
}

class DispatchTest : public ::testing::Test {
 protected:
  void SetUp() override { ResetDispatchTable(); }
  void TearDown() override { ResetDispatchTable(); }
};

TEST_F(DispatchTest, RegistersAndNormalizesThumbBit) {
  RegisterNative(0x08001001u, &MockNativeAdd);
  EXPECT_EQ(LookupNative(0x08001000u), &MockNativeAdd);
  EXPECT_EQ(LookupNative(0x08001001u), &MockNativeAdd);
  EXPECT_EQ(LookupNative(0x08001002u), nullptr);

  EXPECT_EQ(GbaCall(0x08001001u, 17, 25), 42u);
  EXPECT_TRUE(GetFallbackReport().empty());
}

TEST_F(DispatchTest, PassesStackArgsAndTracksFallbacks) {
  RegisterNative(0x08001200u, &MockNativeFiveArgs);
  const uint32_t fifth = 5u;
  EXPECT_EQ(GbaCall(0x08001200u, 1, 2, 3, 4, &fifth, 1), 15u);

  SetFallbackExecutor(&MockFallbackExec);
  EXPECT_EQ(GbaCall(0x08003000u, 0x10u), 0x08003010u);
  EXPECT_EQ(GbaCall(0x08003000u, 0x20u), 0x08003020u);

  // Re-entrant Native -> Fallback -> Native.
  RegisterNative(0x08001001u, &MockNativeAdd);
  EXPECT_EQ(GbaCall(0x08002000u, 3, 4), 14u);

  const auto report = GetFallbackReport();
  ASSERT_EQ(report.size(), 2u);
  EXPECT_EQ(report[0].first, 0x08003000u);
  EXPECT_EQ(report[0].second, 2u);
  EXPECT_EQ(report[1].first, 0x08002000u);
  EXPECT_EQ(report[1].second, 1u);
}

}  // namespace
}  // namespace fe7
