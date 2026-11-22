#include "game/engine/rng.h"

#include <array>
#include <cstdint>

#include "gtest/gtest.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

class RngTest : public ::testing::Test {
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

TEST_F(RngTest, RandInitZeroProducesCanonicalFeGbaSequence) {
  RandInit(0);
  // Seed 0 selects initTable[0..2] = {0xA36E, 0x924E, 0xB784} with 0 warmups.
  GbaPtr<uint16_t> out(0x02000100u);
  RandGetSt(out);
  EXPECT_EQ(out[0], 0xA36Eu);
  EXPECT_EQ(out[1], 0x924Eu);
  EXPECT_EQ(out[2], 0xB784u);

  // Verify the first five RNs (0..99) produced from seed 0:
  // NextRN() = 0x1A12 (10), 0x52B5 (32), 0xD4D4 (83), 0x5A06 (35), 0x5B2D (35).
  EXPECT_EQ(RandNext100(), 10);
  EXPECT_EQ(RandNext100(), 32);
  EXPECT_EQ(RandNext100(), 83);
  EXPECT_EQ(RandNext100(), 35);
  EXPECT_EQ(RandNext100(), 35);
}

TEST_F(RngTest, RandSetStAndRandGetStRoundTrip) {
  GbaPtr<uint16_t> src(0x02000200u);
  src[0] = 0x1111u;
  src[1] = 0x2222u;
  src[2] = 0x3333u;
  RandSetSt(GbaPtr<const uint16_t>(src.addr));

  GbaPtr<uint16_t> dst(0x02000210u);
  RandGetSt(dst);
  EXPECT_EQ(dst[0], 0x1111u);
  EXPECT_EQ(dst[1], 0x2222u);
  EXPECT_EQ(dst[2], 0x3333u);
}

TEST_F(RngTest, RandInitHandlesNegativeSeedsWithoutUb) {
  RandInit(-1);
  // -1 % 7 == -1; (-1 & 7) == 7, (0 & 7) == 0, (1 & 7) == 1.
  GbaPtr<uint16_t> out(0x02000300u);
  RandGetSt(out);
  EXPECT_EQ(out[0], 0xA794u);
  EXPECT_EQ(out[1], 0xA36Eu);
  EXPECT_EQ(out[2], 0x924Eu);
}

TEST_F(RngTest, RandNextAndRollFunctions) {
  RandInit(0);
  // First RN from seed 0 is 10 (in 0..99).
  EXPECT_FALSE(RandRoll(10));
  // Next RN is 32.
  EXPECT_TRUE(RandRoll(33));
  // Next two RNs are 83 and 35 -> avg = (83 + 35) / 2 = 59.
  EXPECT_TRUE(RandRoll2Rn(60));

  // RandNext(0) is always 0; RandNext(10) is in [0, 9].
  EXPECT_EQ(RandNext(0), 0);
  const int v = RandNext(10);
  EXPECT_GE(v, 0);
  EXPECT_LT(v, 10);
}

TEST_F(RngTest, SecondaryLcgInitAndAdvance) {
  RandInitB(0);
  // s=0 -> ((0*4+2)*(0*4+3)) >> 2 = 6 >> 2 = 1.
  EXPECT_EQ(RandNextB(), 1u);
  // s=1 -> (6 * 7) >> 2 = 42 >> 2 = 10.
  EXPECT_EQ(RandNextB(), 10u);
  // s=10 -> (42 * 43) >> 2 = 1806 >> 2 = 451.
  EXPECT_EQ(RandNextB(), 451u);
}

TEST_F(RngTest, RegisterRngFunctionsRoutesViaDispatchTable) {
  RegisterRngFunctions();
  EXPECT_NE(LookupNative(kAddrNextRn), nullptr);
  EXPECT_NE(LookupNative(kAddrRandInit), nullptr);
  EXPECT_NE(LookupNative(kAddrRandNextB), nullptr);

  GbaCall(kAddrRandInit, 0u);
  EXPECT_EQ(GbaCall(kAddrRandNext100), 10u);

  GbaCall(kAddrRandInitB, 0u);
  EXPECT_EQ(GbaCall(kAddrRandNextB), 1u);
}

}  // namespace
}  // namespace fe7
