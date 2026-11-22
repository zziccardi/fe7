#include "game/battle/battle_gen.h"

#include <array>
#include <cstdint>

#include "absl/base/nullability.h"
#include "game/battle.h"
#include "game/battle/battle.h"
#include "game/battle/battle_sim.h"
#include "game/item.h"
#include "game/item/item.h"
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

uint32_t StubNoOp(uint32_t, uint32_t, uint32_t, uint32_t,
                  const uint32_t* absl_nullable) {
  return 0u;
}

uint32_t StubSetWeaponFromSlot(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                               const uint32_t* absl_nullable) {
  GbaPtr<BattleUnit> bu(r0);
  const int slot = static_cast<int32_t>(r1);
  const uint16_t item =
      (slot >= 0 && slot < 5) ? bu->unit.items[slot] : bu->unit.items[0];
  bu->weapon = item;
  bu->weapon_before = item;
  bu->weapon_attributes = GetItemAttributes(item);
  bu->weapon_type = static_cast<uint8_t>(GetItemKind(item));
  bu->weapon_slot_index =
      (slot >= 0 && slot < 5) ? static_cast<uint8_t>(slot) : 0u;
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
    RegisterBattleFunctions();
    RegisterBattleSimFunctions();
    RegisterTrapFunctions();
    RegisterBattleGenFunctions();

    RegisterNative(kAddrSetBattleUnitTerrainBonusesAuto, &StubNoOp);
    RegisterNative(kAddrSetBattleUnitWeapon, &StubSetWeaponFromSlot);
    RegisterNative(kAddrSetBattleUnitWeaponBallista, &StubNoOp);
    RegisterNative(kAddrDisableAllLightRunes, &StubNoOp);
    RegisterNative(kAddrApplyLightRuneTerrainAlt, &StubNoOp);
    RegisterNative(kAddrComputeBattleObstacleStats, &StubNoOp);
    RegisterNative(kAddrBattleApplySupportBonusesAfter, &StubNoOp);
    RegisterNative(kAddrBattleApplyMiscUpdates, &StubNoOp);
    RegisterNative(kAddrRecordBattleStatsAfterCombat, &StubNoOp);
    RegisterNative(kAddrRecordUnitBattleStats, &StubNoOp);

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

    // Setup PInfo / JInfo for actor & target.
    PInfo p{};
    p.id = 1u;
    *GbaPtr<PInfo>(kPInfoBase) = p;
    JInfo j{};
    j.id = 1u;
    j.base_con = 6;
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

}  // namespace
}  // namespace fe7
