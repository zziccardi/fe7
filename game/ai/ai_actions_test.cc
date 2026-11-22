#include "game/ai/ai_actions.h"

#include <array>
#include <cstdint>
#include <cstring>

#include "game/ai/ai.h"
#include "game/ai/ai_opcodes.h"
#include "game/item.h"
#include "game/item/item.h"
#include "game/map/map.h"
#include "game/unit.h"
#include "game/unit/unit.h"
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
constexpr uint32_t kRowPtrsMove = 0x02010000u;
constexpr uint32_t kRowPtrsRange = 0x02010040u;
constexpr uint32_t kRowPtrsUnit = 0x02010080u;
constexpr uint32_t kRowPtrsOther = 0x020100C0u;
constexpr uint32_t kRowPtrsTerrain = 0x02010100u;

constexpr uint32_t kGridMove = 0x02011000u;
constexpr uint32_t kGridRange = 0x02011100u;
constexpr uint32_t kGridUnit = 0x02011200u;
constexpr uint32_t kGridOther = 0x02011300u;
constexpr uint32_t kGridTerrain = 0x02011400u;

constexpr uint32_t kScratchParams = 0x02012000u;
constexpr uint32_t kPInfoBase = 0x08020000u;
constexpr uint32_t kJInfoBase = 0x08021000u;

class AiActionsTest : public ::testing::Test {
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
    RegisterAiFunctions();
    RegisterAiActionFunctions();

    *GbaPtr<MapSize>(kMapSizeAddr) = MapSize{kW, kH};

    auto init_map = [](uint32_t ptr_addr, uint32_t rows_addr,
                       uint32_t grid_addr) {
      *GbaPtr<uint32_t>(ptr_addr) = rows_addr;
      for (int y = 0; y < kH; ++y) {
        *GbaPtr<uint32_t>(rows_addr + static_cast<uint32_t>(y) * 4u) =
            grid_addr + static_cast<uint32_t>(y * kW);
      }
    };
    init_map(kMapMovementAddr, kRowPtrsMove, kGridMove);
    init_map(kMapRangeAddr, kRowPtrsRange, kGridRange);
    init_map(kMapUnitAddr, kRowPtrsUnit, kGridUnit);
    init_map(kAiMapOtherAddr, kRowPtrsOther, kGridOther);
    init_map(kMapTerrainAddr, kRowPtrsTerrain, kGridTerrain);

    // Populate cardinal adjacent offsets at kAiAdjacentOffsetsAddr.
    constexpr int32_t kOffsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int i = 0; i < 4; ++i) {
      *GbaPtr<int32_t>(kAiAdjacentOffsetsAddr + static_cast<uint32_t>(i) * 8u) =
          kOffsets[i][0];
      *GbaPtr<int32_t>(kAiAdjacentOffsetsAddr + static_cast<uint32_t>(i) * 8u +
                       4u) = kOffsets[i][1];
    }
    *GbaPtr<int32_t>(kAiFriendZonePosTableAddr) = 9999;

    // Populate unit LUT slots 0x01 and 0x81 in synthetic ROM.
    *GbaPtr<uint32_t>(kUnitLutAddr + 0x01u * 4u) = 0x02013000u;
    *GbaPtr<uint32_t>(kUnitLutAddr + 0x81u * 4u) = 0x02013100u;

    // Active enemy unit at slot 0x81 (position 3, 3).
    GbaPtr<Unit> active = GetUnit(0x81);
    active->id = static_cast<int8_t>(0x81);
    active->x = 3;
    active->y = 3;
    active->max_hp = 20;
    active->hp = 20;
    active->unk_46 = 2;
    active->pinfo = GbaPtr<const PInfo>(kPInfoBase);
    active->jinfo = GbaPtr<const JInfo>(kJInfoBase);
    *GbaPtr<uint8_t>(kPInfoBase + offsetof(PInfo, id)) = 0x55;
    *GbaPtr<uint32_t>(kAiActiveUnitAddr) = active.addr;
    *GbaPtr<uint8_t>(kAiActiveUnitIdAddr) = 0x81u;
  }

  void TearDown() override { ResetDispatchTable(); }
};

TEST_F(AiActionsTest, IsTileOccupiedByEnemyChecksFactionBit) {
  auto unit_map = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr);
  unit_map[2][2] = 0x01u;  // Player unit (opposite faction to 0x81)
  unit_map[2][3] = 0x82u;  // Enemy ally (same faction)
  unit_map[2][4] = 0x00u;  // Empty

  EXPECT_TRUE(AiIsTileOccupiedByEnemy(2, 2));
  EXPECT_FALSE(AiIsTileOccupiedByEnemy(3, 2));
  EXPECT_FALSE(AiIsTileOccupiedByEnemy(4, 2));
}

TEST_F(AiActionsTest, LoadUnitCounterAndWaitUpdateAiState) {
  EXPECT_FALSE(AiScriptActionLoadUnitCounter(GbaPtr<const uint8_t>(0)));
  EXPECT_EQ(*GbaPtr<uint8_t>(kAiStateAddr + 0x86u), 2u);

  EXPECT_TRUE(AiScriptActionWait(GbaPtr<const uint8_t>(0)));
  EXPECT_EQ(*GbaPtr<uint8_t>(kAiStateAddr + 0x79u), 4u);
}

TEST_F(AiActionsTest, IsActiveUnitInRectWritesResultByte) {
  GbaPtr<uint8_t> params(kScratchParams);
  params[0] = 2;
  params[1] = 2;
  params[2] = 4;
  params[3] = 4;
  EXPECT_FALSE(
      AiScriptActionIsActiveUnitInRect(GbaPtr<const uint8_t>(params.addr)));
  EXPECT_EQ(*GbaPtr<uint8_t>(kAiStateAddr + 0x86u), 1u);

  params[0] = 5;
  params[2] = 7;
  EXPECT_FALSE(
      AiScriptActionIsActiveUnitInRect(GbaPtr<const uint8_t>(params.addr)));
  EXPECT_EQ(*GbaPtr<uint8_t>(kAiStateAddr + 0x86u), 0u);
}

TEST_F(AiActionsTest, TargetPredicatesMatchPidAndCachedUid) {
  GbaPtr<Unit> player = GetUnit(0x01);
  player->id = 0x01;
  player->x = 5;
  player->y = 4;
  player->pinfo = GbaPtr<const PInfo>(kPInfoBase + 0x40u);
  *GbaPtr<uint8_t>(kPInfoBase + 0x40u + offsetof(PInfo, id)) = 0x10u;

  *GbaPtr<uint8_t>(kAiTargetPidScratchAddr) = 0x10u;
  EXPECT_TRUE(AiIsUnitMatchingTargetPid(GbaPtr<const Unit>(player.addr)));
  *GbaPtr<uint8_t>(kAiTargetPidScratchAddr) = 0x11u;
  EXPECT_FALSE(AiIsUnitMatchingTargetPid(GbaPtr<const Unit>(player.addr)));

  *GbaPtr<uint8_t>(kAiStateAddr + 0x86u) = 0x01u;
  EXPECT_TRUE(AiIsUnitMatchingCachedUid(GbaPtr<const Unit>(player.addr)));
  EXPECT_TRUE(
      AiIsUnitInDirectionOfCachedTarget(GbaPtr<const Unit>(player.addr)));
}

TEST_F(AiActionsTest, GetNextPatrolWaypointAdvancesWhenReachable) {
  constexpr uint32_t kTableAddr = 0x08030000u;
  constexpr uint32_t kRouteAddr = 0x08030010u;
  *GbaPtr<uint32_t>(kAiPatrolWaypointsPtrAddr) = kTableAddr;
  *GbaPtr<uint32_t>(kTableAddr) = kRouteAddr;
  *GbaPtr<int16_t>(kRouteAddr + 0u) = 4;
  *GbaPtr<int16_t>(kRouteAddr + 2u) = 5;
  *GbaPtr<int16_t>(kRouteAddr + 4u) = -1;

  GbaPtr<Unit> active = GetUnit(0x81);
  active->ai_config = 0;
  active->unk_46 = 0;

  auto move_map = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr);
  move_map[5][4] = 2u;  // Reachable

  GbaPtr<AiPos> out(kScratchParams);
  EXPECT_TRUE(AiGetNextPatrolWaypoint(out));
  EXPECT_EQ(out->x, 4);
  EXPECT_EQ(out->y, 5);
  EXPECT_EQ(active->unk_46, 1u);

  // Next call hits sentinel -1 and wraps to step 0.
  EXPECT_TRUE(AiGetNextPatrolWaypoint(out));
  EXPECT_EQ(out->x, 4);
  EXPECT_EQ(out->y, 5);
  EXPECT_EQ(active->unk_46, 1u);
}

TEST_F(AiActionsTest, FindBestAdjacentStaffPosPrefersLowDangerReachableTile) {
  auto move_map = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr);
  auto other_map = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kAiMapOtherAddr);
  for (int y = 0; y < kH; ++y) {
    for (int x = 0; x < kW; ++x) {
      move_map[y][x] = 0xFFu;
    }
  }
  move_map[3][5] = 1u;  // (5, 3) adjacent to (4, 3), high danger
  other_map[3][5] = 0x80u;
  move_map[3][3] = 1u;  // (3, 3) adjacent to (4, 3), low danger
  other_map[3][3] = 0x00u;

  // Point JInfo terrain tables at zeroed ROM so terrain bonus is 0.
  *GbaPtr<uint32_t>(kJInfoBase + offsetof(JInfo, avo_terrain_table)) =
      0x08022000u;
  *GbaPtr<uint32_t>(kJInfoBase + offsetof(JInfo, def_terrain_table)) =
      0x08022000u;
  *GbaPtr<uint32_t>(kJInfoBase + offsetof(JInfo, res_terrain_table)) =
      0x08022000u;

  GbaPtr<AiPos> out(kScratchParams);
  EXPECT_TRUE(AiFindBestAdjacentStaffPos(4, 3, out));
  EXPECT_EQ(out->x, 3);
  EXPECT_EQ(out->y, 3);
}

}  // namespace
}  // namespace fe7
