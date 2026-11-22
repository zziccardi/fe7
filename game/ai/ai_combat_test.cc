#include "game/ai/ai_combat.h"

#include <array>
#include <cstdint>

#include "game/ai/ai.h"
#include "game/ai/ai_actions.h"
#include "game/ai/ai_opcodes.h"
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

constexpr uint32_t kPInfoBase = 0x08020000u;
constexpr uint32_t kJInfoBase = 0x08021000u;
constexpr uint32_t kScratchAddr = 0x02012000u;

class AiCombatTest : public ::testing::Test {
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
    RegisterAiCombatFunctions();

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

    *GbaPtr<int32_t>(kAiFriendZonePosTableAddr) = 9999;

    // Populate unit LUT slots 0x01 and 0x81 in synthetic ROM.
    *GbaPtr<uint32_t>(kUnitLutAddr + 0x01u * 4u) = 0x02013000u;
    *GbaPtr<uint32_t>(kUnitLutAddr + 0x81u * 4u) = 0x02013100u;

    GbaPtr<Unit> active = GetUnit(0x81);
    active->id = static_cast<int8_t>(0x81);
    active->x = 3;
    active->y = 3;
    active->pinfo = GbaPtr<const PInfo>(kPInfoBase);
    active->jinfo = GbaPtr<const JInfo>(kJInfoBase);
    *GbaPtr<uint32_t>(kJInfoBase + offsetof(JInfo, avo_terrain_table)) =
        0x08022000u;
    *GbaPtr<uint32_t>(kJInfoBase + offsetof(JInfo, def_terrain_table)) =
        0x08022000u;
    *GbaPtr<uint32_t>(kJInfoBase + offsetof(JInfo, res_terrain_table)) =
        0x08022000u;
    *GbaPtr<uint32_t>(kAiActiveUnitAddr) = active.addr;
    *GbaPtr<uint8_t>(kAiActiveUnitIdAddr) = 0x81u;

    GbaPtr<Unit> target = GetUnit(0x01);
    target->id = 0x01;
    target->x = 5;
    target->y = 3;
    target->pinfo = GbaPtr<const PInfo>(kPInfoBase + 0x40u);
    target->jinfo = GbaPtr<const JInfo>(kJInfoBase);
  }

  void TearDown() override { ResetDispatchTable(); }
};

TEST_F(AiCombatTest, StealPositionScoreRejectsOccupiedOrUnreachableTiles) {
  auto move_map = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr);
  auto unit_map = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr);
  move_map[3][4] = 3u;
  unit_map[3][4] = 0u;
  EXPECT_EQ(AiGetStealPositionScore(4, 3), 3u);

  unit_map[3][4] = 0x81u;  // Active unit itself is allowed.
  EXPECT_EQ(AiGetStealPositionScore(4, 3), 3u);

  unit_map[3][4] = 0x01u;  // Other unit blocks tile.
  EXPECT_EQ(AiGetStealPositionScore(4, 3), 0xFFu);

  unit_map[3][4] = 0u;
  move_map[3][4] = 0xFFu;  // Unreachable.
  EXPECT_EQ(AiGetStealPositionScore(4, 3), 0xFFu);
}

TEST_F(AiCombatTest, CombatSimInvalidAlwaysReturnsFalse) {
  GbaPtr<AiCombatSimulationSt> st(kScratchAddr);
  EXPECT_FALSE(
      AiIsCombatSimInvalid(GbaPtr<const AiCombatSimulationSt>(st.addr)));
}

TEST_F(AiCombatTest, TryUseHealingItemChoosesCurrentPosWhenStationary) {
  GbaPtr<Unit> active = GetUnit(0x81);
  active->items[0] = 0x036Bu;                   // Vulnerary (IID 0x6B, 3 uses)
  *GbaPtr<uint8_t>(kAiStateAddr + 0x7Bu) = 2u;  // Stationary flag

  EXPECT_TRUE(AiTryUseHealingItem());
  GbaPtr<AiDecision> dec(kAiDecisionAddr);
  EXPECT_EQ(dec->action_id, 6u);
  EXPECT_EQ(dec->x_move, 3u);
  EXPECT_EQ(dec->y_move, 3u);
  EXPECT_EQ(dec->item_slot, 0u);
  EXPECT_EQ(dec->action_performed, 1);
}

TEST_F(AiCombatTest, TryStealFromPosReturnsFalseForNonThief) {
  EXPECT_FALSE(AiTryStealFromPos(3, 3));
}

}  // namespace
}  // namespace fe7
