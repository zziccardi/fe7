#include "game/ai/ai_decide.h"

#include <array>
#include <cstdint>
#include <cstring>

#include "game/ai/ai.h"
#include "game/engine/rng.h"
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

alignas(16) std::array<uint8_t, 0x40000u> gTestEwram{};
alignas(16) std::array<uint8_t, 0x8000u> gTestIwram{};
alignas(16) std::array<uint8_t, 0x01000000u> gTestRom{};

constexpr uint32_t kTestPInfoBase = 0x08001000u;
constexpr uint32_t kTestJInfoBase = 0x08002000u;
constexpr uint32_t kTestMoveTableAddr = 0x08003000u;
constexpr uint32_t kTestEscapeListAddr = 0x08003200u;

constexpr uint32_t kMapBufTerrain = 0x02010000u;
constexpr uint32_t kMapBufUnit = 0x02012000u;
constexpr uint32_t kMapBufMovement = 0x02014000u;
constexpr uint32_t kMapBufRange = 0x02016000u;
constexpr uint32_t kMapBufOther = 0x02018000u;
constexpr uint32_t kScratchAddr = 0x02020000u;

class AiDecideTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ResetDispatchTable();
    RegisterUnitFunctions();
    RegisterItemFunctions();
    RegisterMapFunctions();
    gTestEwram.fill(0);
    gTestIwram.fill(0);
    gTestRom.fill(0);
    gMem.ewram = gTestEwram.data();
    gMem.iwram = gTestIwram.data();
    gMem.rom = gTestRom.data();
    gMem.rom_size = gTestRom.size();

    // Initialize 12x10 synthetic maps with standard row-pointer tables.
    auto size = GbaPtr<MapSize>(kMapSizeAddr);
    size->x = 12;
    size->y = 10;
    MapInit(GbaPtr<uint8_t>(kMapBufTerrain),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr), 12, 10);
    MapInit(GbaPtr<uint8_t>(kMapBufUnit),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr), 12, 10);
    MapInit(GbaPtr<uint8_t>(kMapBufMovement),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr), 12, 10);
    MapInit(GbaPtr<uint8_t>(kMapBufRange),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapRangeAddr), 12, 10);
    MapInit(GbaPtr<uint8_t>(kMapBufOther),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kAiMapOtherAddr), 12, 10);

    // Terrain 1 everywhere (cost 1), impassable (-1) outside.
    MapFill(*GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr), 1);
    MapFillEdges(*GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr), 0);

    // Movement cost table: terrain 0 impassable (-1), terrain 1 cost 1.
    auto mov_table = GbaPtr<int8_t>(kTestMoveTableAddr);
    for (int i = 0; i < 65; ++i) {
      mov_table[i] = 1;
    }
    mov_table[0] = -1;

    // Faction unit counts table at 0x081D3B68: {62, 20, 50}.
    auto counts = GbaPtr<int32_t>(kAiFactionUnitCountsAddr);
    counts[0] = 62;
    counts[1] = 20;
    counts[2] = 50;

    // Recovery threshold table at 0x08B9727C: entry 0 = {exit=100, enter=50}.
    auto rec = GbaPtr<uint8_t>(kAiRecoveryThresholdTableAddr);
    rec[0] = 100;
    rec[1] = 50;
    rec[4] = 80;
    rec[5] = 30;

    // Nearby offset table at 0x08B97030 (pre-entry) and 0x08B97034 (entries).
    auto offsets = GbaPtr<AiPos>(kAiNearbyOffsetTableAddr - 4u);
    offsets[0] = {0, 0};
    offsets[1] = {1, 0};
    offsets[2] = {-1, 0};
    offsets[3] = {0, 1};
    offsets[4] = {0, -1};
    offsets[5] = {0x270F, 0};

    // Populate synthetic gUnitLookup entries for Blue, Green, and Red test
    // slots.
    auto lut = GbaPtr<uint32_t>(kUnitLutAddr);
    for (int i = 0; i < 10; ++i) {
      lut[1 + i] = 0x0202BD50u + static_cast<uint32_t>(i) * sizeof(Unit);
      lut[0x41 + i] = 0x0202DDD0u + static_cast<uint32_t>(i) * sizeof(Unit);
      lut[0x81 + i] = 0x0202CEB8u + static_cast<uint32_t>(i) * sizeof(Unit);
    }

    // Seed synthetic item table entries:
    // ID 1 = Iron Sword (melee 1-1, might 5, uses 46)
    // ID 2 = Steel Sword (melee 1-1, might 8, uses 30)
    // ID 3 = Javelin (ranged 1-2 -> min 1, max 2, might 6, uses 20)
    // ID 4 = Iron Bow (ranged 2-2 -> min 2, max 2, might 6, uses 45)
    // ID 0x4A = Heal staff (staff, might 0, uses 30)
    // ID 0x56 = Torch/Unlock staff (staff, might 0, uses 10)
    SetupItem(1, kItemAttrWeapon, 0, 5, 46, 0x11);
    SetupItem(2, kItemAttrWeapon, 0, 8, 30, 0x11);
    SetupItem(3, kItemAttrWeapon, 0, 6, 20, 0x12);
    SetupItem(4, kItemAttrWeapon, 0, 6, 45, 0x22);
    SetupItem(0x4A, kItemAttrStaff, 4, 0, 30, 0x11);
    SetupItem(0x56, kItemAttrStaff, 4, 0, 10, 0x11);
  }

  void SetupItem(int id, uint32_t attrs, uint8_t wtype, uint8_t might,
                 uint8_t max_uses, uint8_t encoded_range) {
    GbaPtr<IInfo> info(kIInfoTableAddr +
                       static_cast<uint32_t>(id) * sizeof(IInfo));
    info->id = static_cast<uint8_t>(id);
    info->kind = wtype;
    info->attributes = attrs;
    info->max_uses = max_uses;
    info->might = might;
    info->encoded_range = encoded_range;
    info->required_wexp = 1;
  }

  GbaPtr<Unit> MakeUnit(int uid, uint8_t jid, int8_t x, int8_t y, int8_t mov,
                        uint32_t j_attrs = 0u) {
    GbaPtr<PInfo> p(kTestPInfoBase +
                    static_cast<uint32_t>(uid) * sizeof(PInfo));
    p->id = static_cast<uint8_t>(uid);
    GbaPtr<JInfo> j(kTestJInfoBase +
                    static_cast<uint32_t>(uid) * sizeof(JInfo));
    j->id = jid;
    j->base_mov = mov;
    j->attributes = j_attrs;
    j->mov_table[0] = GbaPtr<const int8_t>(kTestMoveTableAddr);
    j->mov_table[1] = GbaPtr<const int8_t>(kTestMoveTableAddr);
    j->mov_table[2] = GbaPtr<const int8_t>(kTestMoveTableAddr);

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
    u->wexp[4] = 100;
    (*GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr))[y][x] =
        static_cast<uint8_t>(uid);
    return u;
  }

  void SetActiveUnit(GbaPtr<Unit> u) {
    *GbaPtr<GbaPtr<Unit>>(kAiActiveUnitAddr) = u;
    *GbaPtr<uint8_t>(kAiActiveUnitIdAddr) = static_cast<uint8_t>(u->id);
  }
};

TEST_F(AiDecideTest, AiCompareOperators) {
  *GbaPtr<uint8_t>(kScratchAddr) = 10;
  const GbaPtr<const uint8_t> val_ptr(kScratchAddr);
  EXPECT_TRUE(AiCompare(val_ptr, 0, 9));
  EXPECT_FALSE(AiCompare(val_ptr, 0, 10));
  EXPECT_TRUE(AiCompare(val_ptr, 1, 10));
  EXPECT_TRUE(AiCompare(val_ptr, 2, 10));
  EXPECT_FALSE(AiCompare(val_ptr, 2, 11));
  EXPECT_TRUE(AiCompare(val_ptr, 3, 10));
  EXPECT_TRUE(AiCompare(val_ptr, 4, 11));
  EXPECT_FALSE(AiCompare(val_ptr, 4, 10));
  EXPECT_TRUE(AiCompare(val_ptr, 5, 11));
  EXPECT_FALSE(AiCompare(val_ptr, 5, 10));
  EXPECT_FALSE(AiCompare(val_ptr, 6, 10));
}

TEST_F(AiDecideTest, RangeAndNearbyUnitCounts) {
  auto active = MakeUnit(0x81, 1, 5, 5, 5);
  SetActiveUnit(active);
  MakeUnit(0x82, 1, 6, 5, 5);  // enemy ally
  MakeUnit(0x01, 1, 4, 5, 5);  // player (non-allied)
  MakeUnit(0x41, 1, 5, 6, 5);  // NPC (non-allied to 0x81)

  auto range = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapRangeAddr);
  MapFill(range, 0);
  range[5][6] = 1;
  range[5][4] = 1;
  range[6][5] = 1;

  EXPECT_EQ(AiCountUnitsInRange(), 3);
  EXPECT_EQ(AiCountAlliesInRange(), 1);
  EXPECT_EQ(AiCountEnemiesInRange(), 2);

  EXPECT_EQ(AiCountUnitsNearby(5, 5), 3);
  EXPECT_EQ(AiCountAlliesNearby(5, 5), 1);
  EXPECT_EQ(AiCountEnemiesNearby(5, 5), 2);

  // AiGetMoveCostAt allows own tile or empty tile, rejects other unit's tile.
  range[5][5] = 2;
  range[5][7] = 3;
  EXPECT_EQ(AiGetMoveCostAt(5, 5), 2u);
  EXPECT_EQ(AiGetMoveCostAt(7, 5), 3u);
  EXPECT_EQ(AiGetMoveCostAt(6, 5), 0xFFu);
}

TEST_F(AiDecideTest, RecoveryModeAndFactionUpdate) {
  auto u1 = MakeUnit(0x81, 1, 3, 3, 5);
  auto u2 = MakeUnit(0x82, 1, 4, 3, 5);
  u1->hp = 8;   // 40% < 50% -> enters recovery
  u2->hp = 18;  // 90% >= 50% -> stays normal
  *GbaPtr<uint8_t>(kAiPlayStatusAddr + 0x0Fu) = 0x80u;

  AiUpdateFactionRecovery();
  EXPECT_EQ(u1->ai_flags & 1u, 1u);
  EXPECT_EQ(u2->ai_flags & 1u, 0u);

  // Healing to 100% exits recovery mode.
  u1->hp = 20;
  EXPECT_FALSE(AiUpdateUnitRecoveryMode(u1));
  EXPECT_EQ(u1->ai_flags & 1u, 0u);
}

TEST_F(AiDecideTest, EscapePointAndDangerThreshold) {
  *GbaPtr<int8_t>(kAiPlayStatusAddr + 0x0Eu) = 2;
  *GbaPtr<uint8_t>(kAiPlayStatusAddr + 0x0Fu) = 0x80u;
  auto esc_list = GbaPtr<uint8_t>(kTestEscapeListAddr);
  esc_list[0] = 2;
  esc_list[1] = 2;
  esc_list[4] = 8;
  esc_list[5] = 8;
  esc_list[8] = 0xFF;
  auto esc_tbl = GbaPtr<uint32_t>(kAiEnemyEscapeTableAddr);
  esc_tbl[2] = kTestEscapeListAddr;

  auto mov = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr);
  MapFill(mov, 0xFF);
  mov[2][2] = 6;
  mov[8][8] = 3;

  EXPECT_EQ(AiGetClosestEscapePoint().addr, kTestEscapeListAddr + 4u);

  auto other = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kAiMapOtherAddr);
  other[3][4] = 15;
  EXPECT_TRUE(AiIsWithinDangerThreshold(4, 3, 15));
  EXPECT_FALSE(AiIsWithinDangerThreshold(4, 3, 14));
}

TEST_F(AiDecideTest, EquipBestItemAndThreatEvaluation) {
  auto enemy = MakeUnit(0x81, 1, 5, 5, 5);
  enemy->items[0] =
      static_cast<uint16_t>((46u << 8) | 1u);  // Iron Sword (Mt 5)
  enemy->items[1] =
      static_cast<uint16_t>((30u << 8) | 2u);  // Steel Sword (Mt 8)
  enemy->items[2] = static_cast<uint16_t>((45u << 8) | 4u);  // Iron Bow (Mt 6)
  enemy->items[3] = static_cast<uint16_t>((30u << 8) | 0x4Au);  // Heal staff
  SetActiveUnit(enemy);

  // Player melee unit within movement reach of (5, 5).
  auto player = MakeUnit(0x01, 1, 3, 5, 5);
  player->items[0] = static_cast<uint16_t>((46u << 8) | 1u);  // Iron Sword

  GbaPtr<AiDecision> dec(kAiDecisionAddr);
  dec->x_move = 4;
  dec->y_move = 5;
  dec->action_id = 0;

  EXPECT_TRUE(AiCanEquipBestItem());
  AiEquipBestItem();
  // Melee threat > ranged threat -> Steel Sword (ID 2, Mt 8) moved to slot 0.
  EXPECT_EQ(GetItemIid(enemy->items[0]), 2);
  // Heal staff triggered special staff flag bit 0x04.
  EXPECT_NE(enemy->ai_flags & 4u, 0u);
}

TEST_F(AiDecideTest, NearestClassRandomMoveAndDance) {
  // Test AiTryFindNearestUnitOfClass.
  auto active = MakeUnit(0x81, 1, 2, 2, 5);
  SetActiveUnit(active);
  MakeUnit(0x01, 9, 9, 2, 5);
  MakeUnit(0x02, 9, 4, 2, 5);

  GbaPtr<AiPos> out_pos(kScratchAddr);
  EXPECT_TRUE(AiTryFindNearestUnitOfClass(9, out_pos));
  EXPECT_EQ(out_pos->x, 4);
  EXPECT_EQ(out_pos->y, 2);

  // Test AiTryMoveRandomly sets a valid decision.
  RandInit(12345);
  AiClearDecision();
  AiTryMoveRandomly();
  EXPECT_EQ(GbaPtr<const AiDecision>(kAiDecisionAddr)->action_performed, 1);

  // Test AiTryDanceOrPlay selects highest-level adjacent ally.
  auto dancer = MakeUnit(0x83, 2, 6, 6, 5, 0x10u);
  SetActiveUnit(dancer);
  auto ally_lo = MakeUnit(0x84, 1, 7, 6, 5);
  ally_lo->level = 4;
  auto ally_hi = MakeUnit(0x85, 1, 6, 7, 5);
  ally_hi->level = 12;

  AiClearDecision();
  EXPECT_TRUE(AiTryDanceOrPlay(6, 6));
  GbaPtr<const AiDecision> dec(kAiDecisionAddr);
  EXPECT_EQ(dec->action_id, 7u);
  EXPECT_EQ(dec->unit_target_id, 0x85u);
}

}  // namespace
}  // namespace fe7
