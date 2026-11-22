#include "game/map/map.h"

#include <array>
#include <cstdint>

#include "gtest/gtest.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

class MapTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ewram_.fill(0);
    iwram_.fill(0);
    gMem.ewram = ewram_.data();
    gMem.iwram = iwram_.data();
    gMem.rom = nullptr;
    gMem.rom_size = 0;
    ResetDispatchTable();
    RegisterMapFunctions();

    *GbaPtr<MapSize>(kMapSizeAddr) = {8, 8};
    MapInit(GbaPtr<uint8_t>(0x02001000u),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr), 8, 8);
    MapInit(GbaPtr<uint8_t>(0x02001400u),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr), 8, 8);
    MapInit(GbaPtr<uint8_t>(0x02001800u),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr), 8, 8);
    MapInit(GbaPtr<uint8_t>(0x02001C00u),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapRangeAddr), 8, 8);
  }

  void TearDown() override {
    ResetDispatchTable();
    gMem = GbaMemoryMap{};
  }

  alignas(16) std::array<uint8_t, 0x40000> ewram_;
  alignas(16) std::array<uint8_t, 0x8000> iwram_;
};

TEST_F(MapTest, MapFillEdgesAndDiamondRangeRing) {
  const GbaPtr<GbaPtr<uint8_t>> range_map =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapRangeAddr);
  MapFill(range_map, 0);
  // Ring [2, 3] around (4, 4).
  MapSetInRange(4, 4, 2, 3);
  EXPECT_EQ(range_map[4][4], 0u);  // distance 0
  EXPECT_EQ(range_map[4][5], 0u);  // distance 1
  EXPECT_EQ(range_map[4][6], 1u);  // distance 2
  EXPECT_EQ(range_map[4][7], 1u);  // distance 3
  EXPECT_EQ(range_map[2][4], 1u);  // distance 2
  EXPECT_EQ(range_map[1][4], 1u);  // distance 3
  EXPECT_EQ(range_map[0][4], 0u);  // distance 4

  MapFillEdges(range_map, 0x99u);
  EXPECT_EQ(range_map[0][0], 0x99u);
  EXPECT_EQ(range_map[7][7], 0x99u);
  EXPECT_EQ(range_map[3][0], 0x99u);
  EXPECT_EQ(range_map[3][7], 0x99u);
}

TEST_F(MapTest, BfsFloodRespectsTerrainCostsAndEnemyBlocking) {
  const GbaPtr<GbaPtr<uint8_t>> terrain =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr);
  const GbaPtr<GbaPtr<uint8_t>> units =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr);
  const GbaPtr<GbaPtr<uint8_t>> mov_map =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr);

  // Fill terrain with plains (1), border with impassable (0), and forest (2) at
  // (3, 2).
  MapFill(terrain, 0);
  for (int y = 0; y < 8; ++y) {
    for (int x = 0; x < 8; ++x) {
      terrain[y][x] = 1u;
    }
  }
  terrain[2][3] = 2u;  // Forest (cost 2)
  MapFill(units, 0);
  units[2][1] = 0x81u;  // Enemy blocking (1, 2)

  GbaPtr<int8_t> costs(0x02002000u);
  for (int i = 0; i <= 0x40; ++i) {
    costs[i] = 127;  // Impassable by default
  }
  costs[1] = 1;  // Plains
  costs[2] = 2;  // Forest

  SetWorkingMoveTable(GbaPtr<const int8_t>(costs.addr));
  SetWorkingMap(mov_map);
  BeginMapFlood(2, 2, 3, /*unit_id=*/1);

  EXPECT_EQ(mov_map[2][2], 0u);     // Origin
  EXPECT_EQ(mov_map[2][3], 2u);     // Forest east
  EXPECT_EQ(mov_map[2][4], 3u);     // Plains past forest (2 + 1)
  EXPECT_EQ(mov_map[2][1], 0xFFu);  // Blocked by enemy 0x81
  EXPECT_EQ(mov_map[1][2], 1u);     // Plains north
  EXPECT_EQ(mov_map[0][2], 2u);     // 2 north
}

}  // namespace
}  // namespace fe7
