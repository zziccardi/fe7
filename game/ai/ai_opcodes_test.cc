#include "game/ai/ai_opcodes.h"

#include <array>
#include <cstdint>
#include <cstring>

#include "absl/base/nullability.h"
#include "game/ai/ai.h"
#include "game/ai/ai_decide.h"
#include "game/ai/ai_script.h"
#include "game/engine/rng.h"
#include "game/item.h"
#include "game/item/item.h"
#include "game/map/map.h"
#include "game/map/trap.h"
#include "game/phase/phase.h"
#include "game/unit.h"
#include "game/unit/unit.h"
#include "gtest/gtest.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

alignas(16) std::array<uint8_t, 0x40000u> gTestEwram{};
alignas(16) std::array<uint8_t, 0x8000u> gTestIwram{};
alignas(16) std::array<uint8_t, 0x01000000u> gTestRom{};

constexpr uint32_t kTestPInfoBase = 0x08001000u;
constexpr uint32_t kTestJInfoBase = 0x08002000u;
constexpr uint32_t kTestMoveTableAddr = 0x08003000u;
constexpr uint32_t kTestTerrainAvoAddr = 0x08003100u;
constexpr uint32_t kTestTerrainDefAddr = 0x08003180u;
constexpr uint32_t kTestTerrainResAddr = 0x08003200u;
constexpr uint32_t kTestTerrainListAddr = 0x08003300u;
constexpr uint32_t kTestJobRank0Addr = 0x08003340u;

constexpr uint32_t kMapBufTerrain = 0x02010000u;
constexpr uint32_t kMapBufUnit = 0x02011000u;
constexpr uint32_t kMapBufMovement = 0x02012000u;
constexpr uint32_t kMapBufRange = 0x02013000u;
constexpr uint32_t kMapBufOther = 0x02014000u;
constexpr uint32_t kScratchAddr = 0x02020000u;
constexpr uint32_t kCmdAddr = 0x02020100u;

uint32_t StubReturnZero(uint32_t, uint32_t, uint32_t, uint32_t,
                        const uint32_t* absl_nullable) {
  return 0u;
}

class AiOpcodesTest : public ::testing::Test {
 protected:
  void SetUp() override {
    gTestEwram.fill(0);
    gTestIwram.fill(0);
    gTestRom.fill(0);
    gMem.ewram = gTestEwram.data();
    gMem.iwram = gTestIwram.data();
    gMem.rom = gTestRom.data();
    gMem.rom_size = gTestRom.size();

    ResetDispatchTable();
    RegisterRngFunctions();
    RegisterUnitFunctions();
    RegisterItemFunctions();
    RegisterMapFunctions();
    RegisterTrapFunctions();
    RegisterPhaseFunctions();
    RegisterAiFunctions();
    RegisterAiDecideFunctions();
    RegisterAiScriptFunctions();
    RegisterAiOpcodeFunctions();

    // Stub un-decompiled external hooks for pure unit testing.
    RegisterNative(kAddrMapFloodPostProcess, &StubReturnZero);
    RegisterNative(kAddrAiPerformAttack, &StubReturnZero);
    RegisterNative(kAddrAiPerformEscape, &StubReturnZero);
    RegisterNative(kAddrAiTrySpecialAction, &StubReturnZero);
    RegisterNative(kAddrAiTryDoorOrChestAction, &StubReturnZero);
    RegisterNative(kAddrAiMapFloodNoDoors, &StubReturnZero);
    RegisterNative(kAddrAiMapFloodNoDoorsUnit, &StubReturnZero);
    RegisterNative(kAddrAiMapFloodObstacles, &StubReturnZero);
    RegisterNative(kAddrAiMapFloodRangeWithUnit, &StubReturnZero);

    auto size = GbaPtr<MapSize>(kMapSizeAddr);
    size->x = 10;
    size->y = 10;
    MapInit(GbaPtr<uint8_t>(kMapBufTerrain),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr), 10, 10);
    // 4-tile padded gMapUnit for friend-zone / nearby scans.
    constexpr int kPad = 4;
    constexpr uint32_t kRows = 10 + 2 * kPad;
    constexpr uint32_t kStride = 10 + 2 * kPad;
    const uint32_t tiles = kMapBufUnit + kRows * 4u;
    auto row_ptrs = GbaPtr<uint32_t>(kMapBufUnit);
    for (uint32_t r = 0; r < kRows; ++r) {
      row_ptrs[r] = tiles + r * kStride + kPad;
    }
    *GbaPtr<uint32_t>(kMapUnitAddr) = kMapBufUnit + kPad * 4u;

    MapInit(GbaPtr<uint8_t>(kMapBufMovement),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr), 10, 10);
    MapInit(GbaPtr<uint8_t>(kMapBufRange),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapRangeAddr), 10, 10);
    MapInit(GbaPtr<uint8_t>(kMapBufOther),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kAiMapOtherAddr), 10, 10);

    MapFill(*GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr), 1);
    MapFillEdges(*GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr), 0);

    auto mov_table = GbaPtr<int8_t>(kTestMoveTableAddr);
    for (int i = 0; i < 65; ++i) {
      mov_table[i] = 1;
    }
    mov_table[0] = -1;

    auto lut = GbaPtr<uint32_t>(kUnitLutAddr);
    for (int i = 0; i < 10; ++i) {
      lut[1 + i] = 0x0202BD50u + static_cast<uint32_t>(i) * sizeof(Unit);
      lut[0x81 + i] = 0x0202CEB8u + static_cast<uint32_t>(i) * sizeof(Unit);
    }

    // Cardinal offsets at 0x081D3670: (1,0), (-1,0), (0,1), (0,-1).
    const int8_t card[8] = {1, 0, -1, 0, 0, 1, 0, -1};
    std::memcpy(gTestRom.data() + (kAiCardinalOffsetsAddr - 0x08000000u), card,
                sizeof(card));

    // Friend-zone position offsets terminator.
    auto fz = GbaPtr<AiPos>(kAiFriendZonePosTableAddr);
    fz[0] = {9999, 9999};

    // Nearby offsets terminator.
    auto nb = GbaPtr<AiPos>(kAiNearbyOffsetTableAddr - 4u);
    nb[0] = {0, 0};
    nb[1] = {0x270F, 0};

    // Loot & obstacle terrain lists.
    auto no_key = GbaPtr<uint8_t>(kAiLootTerrainsNoKeyAddr);
    no_key[0] = 0x03u;
    no_key[1] = 0x00u;
    auto with_key = GbaPtr<uint8_t>(kAiLootTerrainsWithKeyAddr);
    with_key[0] = 0x03u;
    with_key[1] = 0x21u;
    with_key[2] = 0x00u;

    // Job rank table terminator.
    auto jr_entry = GbaPtr<uint8_t>(kTestJobRank0Addr);
    jr_entry[0] = 1u;
    jr_entry[1] = 0u;
    auto jr_tbl = GbaPtr<uint32_t>(kAiJobRankTableAddr);
    jr_tbl[0] = kTestJobRank0Addr;
    jr_tbl[1] = 0u;

    // Seed Iron Sword (ID 1, Mt 5, range 1-1).
    GbaPtr<IInfo> sw(kIInfoTableAddr + sizeof(IInfo));
    sw->id = 1;
    sw->kind = 0;
    sw->attributes = kItemAttrWeapon;
    sw->max_uses = 46;
    sw->might = 5;
    sw->encoded_range = 0x11u;
    sw->required_wexp = 1;
  }

  GbaPtr<Unit> MakeUnit(int uid, uint8_t pid, uint8_t jid, int8_t x, int8_t y,
                        int8_t mov) {
    GbaPtr<PInfo> p(kTestPInfoBase +
                    static_cast<uint32_t>(uid) * sizeof(PInfo));
    p->id = pid;
    GbaPtr<JInfo> j(kTestJInfoBase +
                    static_cast<uint32_t>(uid) * sizeof(JInfo));
    j->id = jid;
    j->base_mov = mov;
    j->mov_table[0] = GbaPtr<const int8_t>(kTestMoveTableAddr);
    j->mov_table[1] = GbaPtr<const int8_t>(kTestMoveTableAddr);
    j->mov_table[2] = GbaPtr<const int8_t>(kTestMoveTableAddr);
    j->avo_terrain_table = GbaPtr<const int8_t>(kTestTerrainAvoAddr);
    j->def_terrain_table = GbaPtr<const int8_t>(kTestTerrainDefAddr);
    j->res_terrain_table = GbaPtr<const int8_t>(kTestTerrainResAddr);

    GbaPtr<Unit> u = GetUnit(uid);
    std::memset(u.get(), 0, sizeof(Unit));
    u->pinfo = GbaPtr<const PInfo>(p.addr);
    u->jinfo = GbaPtr<const JInfo>(j.addr);
    u->id = static_cast<int8_t>(uid);
    u->level = 5;
    u->x = x;
    u->y = y;
    u->max_hp = 20;
    u->hp = 20;
    u->pow = 7;
    u->wexp[0] = 100;
    (*GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr))[y][x] =
        static_cast<uint8_t>(uid);
    return u;
  }

  void SetActiveUnit(GbaPtr<Unit> u) {
    *GbaPtr<GbaPtr<Unit>>(kAiActiveUnitAddr) = u;
    *GbaPtr<uint8_t>(kAiActiveUnitIdAddr) = static_cast<uint8_t>(u->id);
  }
};

TEST_F(AiOpcodesTest, MoveTowardsAndFindBestAdjacent) {
  auto u = MakeUnit(0x81, 10, 1, 2, 2, 4);
  SetActiveUnit(u);

  AiClearDecision();
  AiMoveTowards(7, 2, 0u, 0xFFu, 1u);
  GbaPtr<const AiDecision> dec(kAiDecisionAddr);
  EXPECT_EQ(dec->action_performed, 1);
  EXPECT_EQ(dec->x_move, 6);
  EXPECT_EQ(dec->y_move, 2);

  // FindBestAdjacentPosition picks lowest gMapRange neighbor.
  auto range = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapRangeAddr);
  MapFill(range, 10);
  range[4][5] = 2;
  GbaPtr<AiPos> adj(kScratchAddr);
  EXPECT_TRUE(AiFindBestAdjacentPosition(4, 4, kAddrAiGetMoveCostAt | 1u, adj));
  EXPECT_EQ(adj->x, 5);
  EXPECT_EQ(adj->y, 4);
}

TEST_F(AiOpcodesTest, TerrainSearchAndDoorChest) {
  auto u = MakeUnit(0x81, 10, 1, 2, 2, 5);
  SetActiveUnit(u);
  auto terrain = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr);
  terrain[2][5] = 0x03u;  // Village
  auto range = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapRangeAddr);
  MapFill(range, 3);

  auto tlist = GbaPtr<uint8_t>(kTestTerrainListAddr);
  tlist[0] = 0x03u;
  tlist[1] = 0x00u;

  GbaPtr<AiPos> out_pos(kScratchAddr);
  EXPECT_TRUE(AiFindNearestTerrainInList(
      GbaPtr<const uint8_t>(kTestTerrainListAddr), 0u, out_pos));
  EXPECT_EQ(out_pos->x, 5);
  EXPECT_EQ(out_pos->y, 2);

  // Chest (0x21) found within movement range by AiFindNearestDoorOrChest.
  terrain[2][4] = 0x21u;
  auto mov = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr);
  MapFill(mov, 2);
  EXPECT_TRUE(AiFindNearestDoorOrChest(0u, out_pos));
  EXPECT_EQ(out_pos->x, 4);
  EXPECT_EQ(out_pos->y, 2);
}

TEST_F(AiOpcodesTest, ScriptOpcodesAndObstacleAttackPos) {
  RandInit(42);
  auto u = MakeUnit(0x81, 10, 1, 3, 3, 4);
  u->items[0] = static_cast<uint16_t>((46u << 8) | 1u);
  SetActiveUnit(u);

  // AiFindBestObstacleAttackPos finds tile adjacent to (5, 3) with Iron Sword.
  GbaPtr<AiPos> atk_pos(kScratchAddr);
  GbaPtr<uint8_t> atk_slot(kScratchAddr + 8u);
  EXPECT_TRUE(AiFindBestObstacleAttackPos(5, 3, atk_pos, atk_slot));
  EXPECT_EQ(*atk_slot, 0u);
  EXPECT_EQ(atk_pos->x, 4);
  EXPECT_EQ(atk_pos->y, 3);

  // Opcode 0x0C (MoveToCoord) advances pc when unit reaches destination.
  GbaPtr<AiScriptCmd> cmd(kCmdAddr);
  *cmd = {0x0C, 5, 0xFF, 3, 0u, 0u, 0u};
  *GbaPtr<GbaPtr<const AiScriptCmd>>(kAiScriptCurAddr) =
      GbaPtr<const AiScriptCmd>(kCmdAddr);
  GbaPtr<uint8_t> pc(kScratchAddr + 16u);
  *pc = 0u;
  AiClearDecision();
  AiScriptCmdMoveToCoord(pc);
  EXPECT_EQ(*pc, 1u);

  // Label (0x1B) advances pc and clears gAiScriptDone.
  *GbaPtr<int8_t>(kAiScriptDoneAddr) = 1;
  AiScriptCmdLabel(pc);
  EXPECT_EQ(*pc, 2u);
  EXPECT_EQ(*GbaPtr<int8_t>(kAiScriptDoneAddr), 0);

  // ReturnTrue / CheckJobRank helpers.
  EXPECT_TRUE(AiReturnTrue());
  *GbaPtr<uint8_t>(kScratchAddr + 20u) = 1u;
  EXPECT_TRUE(
      AiCheckJobRankAndReturnTrue(GbaPtr<const uint8_t>(kScratchAddr + 20u)));
}

}  // namespace
}  // namespace fe7
