#include "game/map/map_range.h"

#include <array>
#include <cstdint>

#include "game/engine/clock.h"
#include "game/engine/rng.h"
#include "game/map/map.h"
#include "game/unit.h"
#include "gtest/gtest.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

alignas(16) std::array<uint8_t, 0x40000> gTestEwram{};
alignas(16) std::array<uint8_t, 0x8000> gTestIwram{};
alignas(16) std::array<uint8_t, 0x01000000> gTestRom{};

constexpr int kW = 8;
constexpr int kH = 8;

void InitMap(uint32_t buf_addr, uint32_t ptr_addr, uint8_t fill) {
  const uint32_t rows_addr = buf_addr;
  const uint32_t data_addr = buf_addr + 0x100u;
  *GbaPtr<uint32_t>(ptr_addr) = rows_addr;
  GbaPtr<uint32_t> rows_ptr(rows_addr);
  for (int y = 0; y < kH; ++y) {
    const uint32_t row_addr = data_addr + static_cast<uint32_t>(y * kW);
    rows_ptr[y] = row_addr;
    GbaPtr<uint8_t> row_ptr(row_addr);
    for (int x = 0; x < kW; ++x) {
      row_ptr[x] = fill;
    }
  }
}

class MapRangeTest : public ::testing::Test {
 protected:
  void SetUp() override {
    gTestEwram.fill(0);
    gTestIwram.fill(0);
    gTestRom.fill(0);
    gMem.ewram = gTestEwram.data();
    gMem.iwram = gTestIwram.data();
    gMem.rom = gTestRom.data();
    gMem.rom_size = static_cast<uint32_t>(gTestRom.size());

    ResetDispatchTable();
    RegisterRngFunctions();
    RegisterClockFunctions();
    RegisterMapFunctions();
    RegisterMapRangeFunctions();

    *GbaPtr<MapSize>(kMapSizeAddr) = MapSize{kW, kH};
    InitMap(0x02010000u, kMapMovementAddr, 0xFFu);
    InitMap(0x02010400u, kMapRangeAddr, 0x00u);
    InitMap(0x02010800u, kMapUnitAddr, 0x00u);
    InitMap(0x02010C00u, kMapOtherAddr, 0x00u);
    InitMap(0x02011000u, kMapHiddenAddr, 0x00u);
    SetWorkingMap(*GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapRangeAddr));
  }

  void TearDown() override { ResetDispatchTable(); }
};

TEST_F(MapRangeTest, MapFillInRangeFillsDiamond) {
  MapFillInRange(3, 3, 2, 7);
  auto rows = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapRangeAddr);
  EXPECT_EQ(rows[3][3], 7u);
  EXPECT_EQ(rows[1][3], 7u);
  EXPECT_EQ(rows[3][5], 7u);
  EXPECT_EQ(rows[2][4], 7u);
  EXPECT_EQ(rows[1][4], 0u);
}

TEST_F(MapRangeTest, ReverseAndBuildBestMoveScript) {
  auto rows = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr);
  rows[2][2] = 0u;
  rows[2][3] = 1u;
  rows[2][4] = 2u;
  SetWorkingMap(rows);

  GbaPtr<uint8_t> script(kWorkingMoveScriptAddr);
  BuildBestMoveScript(4, 2, script);
  // Path from (2,2) to (4,2) backtracked left (1) twice -> reversed: 1, 1, 4.
  EXPECT_EQ(script[0], 1u);
  EXPECT_EQ(script[1], 1u);
  EXPECT_EQ(script[2], 4u);
}

TEST_F(MapRangeTest, MapFloodRangeExtendExpandsOneTile) {
  auto rows = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr);
  rows[3][3] = 0u;
  GbaPtr<MapFloodState>(kMapFloodStAddr)->unk_0b = 0x78u;

  MapFloodRangeExtend();
  EXPECT_EQ(rows[3][2], 0x78u);
  EXPECT_EQ(rows[3][4], 0x78u);
  EXPECT_EQ(rows[2][3], 0x78u);
  EXPECT_EQ(rows[4][3], 0x78u);
  EXPECT_EQ(GbaPtr<MapFloodState>(kMapFloodStAddr)->unk_0b, 0x79u);
}

TEST_F(MapRangeTest, BlinkColorsAlternateOpposite) {
  SetGameTime(0u);  // 0 seconds (even)
  EXPECT_EQ(GetMapRangeBlinkColorA(), 2);
  EXPECT_EQ(GetMapRangeBlinkColorB(), 3);
  SetGameTime(60u);  // 1 second (odd)
  EXPECT_EQ(GetMapRangeBlinkColorA(), 3);
  EXPECT_EQ(GetMapRangeBlinkColorB(), 2);
}

}  // namespace
}  // namespace fe7
