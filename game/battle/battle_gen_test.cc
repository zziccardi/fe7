#include "game/battle/battle_gen.h"

#include <array>
#include <cstdint>

#include "absl/base/nullability.h"
#include "game/battle.h"
#include "game/battle/battle.h"
#include "game/battle/battle_sim.h"
#include "game/item.h"
#include "game/item/item.h"
#include "game/map/ballista.h"
#include "game/map/map.h"
#include "game/map/trap.h"
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

constexpr uint32_t kPInfoBase = 0x08020000u;
constexpr uint32_t kJInfoBase = 0x08021000u;
constexpr uint32_t kTerrainAvoBase = 0x08022000u;
constexpr uint32_t kTerrainDefBase = 0x08022100u;
constexpr uint32_t kTerrainResBase = 0x08022200u;
constexpr uint32_t kTerrainMapBuf = 0x02018000u;

uint32_t StubNoOp(uint32_t, uint32_t, uint32_t, uint32_t,
                  const uint32_t* absl_nullable) {
  return 0u;
}

class BattleGenTest : public ::testing::Test {
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
    RegisterUnitFunctions();
    RegisterItemFunctions();
    RegisterMapFunctions();
    RegisterTrapFunctions();
    RegisterBallistaFunctions();
    RegisterBattleFunctions();
    RegisterBattleSimFunctions();
    RegisterBattleGenFunctions();

    RegisterNative(kAddrComputeBattleObstacleStats, &StubNoOp);
    RegisterNative(kAddrBattleApplySupportBonusesAfter, &StubNoOp);
    RegisterNative(kAddrBattleApplyMiscUpdates, &StubNoOp);
    RegisterNative(kAddrRecordBattleStatsAfterCombat, &StubNoOp);
    RegisterNative(kAddrRecordUnitBattleStats, &StubNoOp);

    // Initialize an 8x8 terrain map (default terrain ID 1 = Plains).
    GbaPtr<MapSize>(kMapSizeAddr)->x = 8;
    GbaPtr<MapSize>(kMapSizeAddr)->y = 8;
    MapInit(GbaPtr<uint8_t>(kTerrainMapBuf),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr), 8, 8);
    MapFill(*GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr), 1);

    // Forest (terrain ID 12): +20 avo, +1 def, +2 res.
    *GbaPtr<int8_t>(kTerrainAvoBase + 12u) = 20;
    *GbaPtr<int8_t>(kTerrainDefBase + 12u) = 1;
    *GbaPtr<int8_t>(kTerrainResBase + 12u) = 2;

    // Terminate weapon triangle rule table at kWeaponTriangleTableAddr.
    *GbaPtr<int8_t>(kWeaponTriangleTableAddr) = -1;

    // Iron Sword (IID 0x01): might 5, hit 90, weight 5, range 1-1.
    IInfo sword{};
    sword.id = 0x01u;
    sword.kind = 0u;
    sword.attributes = 0x01u;  // Weapon
    sword.might = 5;
    sword.hit = 90;
    sword.weight = 5;
    sword.encoded_range = 0x11u;
    *GbaPtr<IInfo>(kIInfoTableAddr + 0x01u * sizeof(IInfo)) = sword;

    // Light Brand (IID 0x10): magic sword (attr 0x41), range 1-2.
    IInfo light_brand{};
    light_brand.id = 0x10u;
    light_brand.kind = 0u;
    light_brand.attributes = 0x41u;  // Weapon | MagicSword
    light_brand.might = 9;
    light_brand.hit = 70;
    light_brand.weight = 9;
    light_brand.encoded_range = 0x12u;
    *GbaPtr<IInfo>(kIInfoTableAddr + 0x10u * sizeof(IInfo)) = light_brand;

    // Ballista (IID 0x35): Bow (kind 3), range 3-10.
    IInfo ballista{};
    ballista.id = 0x35u;
    ballista.kind = 3u;
    ballista.attributes = 0x01u;
    ballista.might = 8;
    ballista.hit = 70;
    ballista.weight = 20;
    ballista.encoded_range = 0x3Au;
    *GbaPtr<IInfo>(kIInfoTableAddr + 0x35u * sizeof(IInfo)) = ballista;

    // Setup PInfo / JInfo for actor & target.
    PInfo p{};
    p.id = 1u;
    *GbaPtr<PInfo>(kPInfoBase) = p;
    JInfo j{};
    j.id = 1u;
    j.base_con = 6;
    j.avo_terrain_table = GbaPtr<const int8_t>(kTerrainAvoBase);
    j.def_terrain_table = GbaPtr<const int8_t>(kTerrainDefBase);
    j.res_terrain_table = GbaPtr<const int8_t>(kTerrainResBase);
    *GbaPtr<JInfo>(kJInfoBase) = j;
  }

  void TearDown() override { ResetDispatchTable(); }
};

TEST_F(BattleGenTest, GenerateSimulationSetsConfigAndRange) {
  GbaPtr<Unit> actor(0x02010000u);
  GbaPtr<Unit> target(0x02010100u);
  actor->pinfo = GbaPtr<const PInfo>(kPInfoBase);
  actor->jinfo = GbaPtr<const JInfo>(kJInfoBase);
  actor->id = 0x01;
  actor->x = 2;
  actor->y = 2;
  actor->max_hp = 20;
  actor->hp = 20;
  actor->pow = 8;
  actor->skl = 10;
  actor->spd = 10;
  actor->items[0] = 0x2E01u;
  actor->wexp[0] = 251u;

  target->pinfo = GbaPtr<const PInfo>(kPInfoBase);
  target->jinfo = GbaPtr<const JInfo>(kJInfoBase);
  target->id = static_cast<int8_t>(0x81);
  target->x = 4;
  target->y = 3;
  target->max_hp = 20;
  target->hp = 20;
  target->def = 3;
  target->items[0] = 0x2E01u;
  target->wexp[0] = 251u;

  BattleGenerateSimulation(actor, target, 3, 3, 0);
  GbaPtr<BattleStats> stats(kBattleStatsAddr);
  EXPECT_EQ(stats->config, kBattleConfigSimulate);
  EXPECT_EQ(stats->range, 1u);
  EXPECT_GT(GbaPtr<BattleUnit>(kBattleActorAddr)->battle_attack, 0);
}

TEST_F(BattleGenTest, GenerateDisplayStatsRestoresOriginalInventoryOrder) {
  GbaPtr<Unit> unit(0x02010000u);
  unit->pinfo = GbaPtr<const PInfo>(kPInfoBase);
  unit->jinfo = GbaPtr<const JInfo>(kJInfoBase);
  unit->id = 0x01;
  unit->max_hp = 20;
  unit->hp = 20;
  unit->pow = 8;
  unit->items[0] = 0x0000u;
  unit->items[1] = 0x2E01u;

  BattleGenerateDisplayStats(unit, 1);
  EXPECT_EQ(GbaPtr<BattleStats>(kBattleStatsAddr)->config, kBattleConfigBit2);
  EXPECT_EQ(unit->items[0], 0x0000u);
  EXPECT_EQ(unit->items[1], 0x2E01u);
}

TEST_F(BattleGenTest, SetBattleUnitTerrainBonusesAutoReadsMapTerrain) {
  GbaPtr<GbaPtr<uint8_t>> terrain =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr);
  terrain[4][3] = 12u;

  GbaPtr<BattleUnit> bu(kBattleActorAddr);
  bu->unit.jinfo = GbaPtr<const JInfo>(kJInfoBase);
  bu->unit.x = 3;
  bu->unit.y = 4;

  SetBattleUnitTerrainBonusesAuto(bu);
  EXPECT_EQ(bu->terrain_id, 12u);
  EXPECT_EQ(bu->terrain_avoid, 20);
  EXPECT_EQ(bu->terrain_defense, 1);
  EXPECT_EQ(bu->terrain_resistance, 2);
}

TEST_F(BattleGenTest, SetBattleUnitWeaponHandlesMagicSwordsRangeAndSleep) {
  GbaPtr<BattleUnit> bu(kBattleActorAddr);
  GbaPtr<BattleStats> stats(kBattleStatsAddr);
  bu->unit.pinfo = GbaPtr<const PInfo>(kPInfoBase);
  bu->unit.jinfo = GbaPtr<const JInfo>(kJInfoBase);
  bu->unit.items[0] = 0x1910u;  // Light Brand (range 1-2)
  bu->unit.wexp[0] = 251u;

  // At range 2, Light Brand switches weapon_type to Light (6) and keeps magic.
  stats->config = kBattleConfigSimulate;
  stats->range = 2u;
  SetBattleUnitWeapon(bu, 0);
  EXPECT_EQ(bu->weapon, 0x1910u);
  EXPECT_EQ(bu->weapon_type, 6u);
  EXPECT_NE(bu->weapon_attributes & 0x40u, 0u);
  EXPECT_EQ(bu->can_counter, 1);

  // At range 1, Light Brand strips magic-sword bit (0x40).
  stats->range = 1u;
  SetBattleUnitWeapon(bu, 0);
  EXPECT_EQ(bu->weapon, 0x1910u);
  EXPECT_EQ(bu->weapon_type, 0u);
  EXPECT_EQ(bu->weapon_attributes & 0x40u, 0u);

  // Out of range (range 3) clears weapon and can_counter.
  stats->range = 3u;
  SetBattleUnitWeapon(bu, 0);
  EXPECT_EQ(bu->weapon, 0u);
  EXPECT_EQ(bu->can_counter, 0);

  // Sleep status clears weapon even within range.
  stats->range = 1u;
  bu->unit.status = static_cast<uint8_t>(UnitStatus::kSleep);
  SetBattleUnitWeapon(bu, 0);
  EXPECT_EQ(bu->weapon, 0u);
  EXPECT_EQ(bu->can_counter, 0);
}

TEST_F(BattleGenTest, SetBattleUnitWeaponBallistaLoadsTrapWeapon) {
  InitTraps();
  GbaPtr<Trap> trap =
      AddTrap(3, 4, static_cast<int>(TrapType::kBallista), 0x35);
  trap->data[2] = 5u;

  GbaPtr<BattleUnit> bu(kBattleActorAddr);
  bu->unit.x = 3;
  bu->unit.y = 4;

  SetBattleUnitWeaponBallista(bu);
  EXPECT_EQ(bu->weapon, 0x0535u);
  EXPECT_EQ(bu->weapon_before, 0x0535u);
  EXPECT_EQ(bu->weapon_type, 3u);
  EXPECT_EQ(bu->can_counter, 0);
}

}  // namespace
}  // namespace fe7
