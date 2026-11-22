#include "game/battle/battle.h"

#include <array>
#include <cstdint>

#include "game/battle.h"
#include "game/item.h"
#include "game/item/item.h"
#include "game/unit.h"
#include "gtest/gtest.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

alignas(16) std::array<uint8_t, 0x01000000u> gSyntheticRom{};

class BattleTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ewram_.fill(0);
    iwram_.fill(0);
    gSyntheticRom.fill(0);
    gMem.ewram = ewram_.data();
    gMem.iwram = iwram_.data();
    gMem.rom = gSyntheticRom.data();
    gMem.rom_size = gSyntheticRom.size();
    ResetDispatchTable();
    RegisterItemFunctions();
    RegisterBattleFunctions();
  }

  void TearDown() override {
    ResetDispatchTable();
    gMem = GbaMemoryMap{};
  }

  alignas(16) std::array<uint8_t, 0x40000> ewram_;
  alignas(16) std::array<uint8_t, 0x8000> iwram_;
};

TEST_F(BattleTest, WeaponTriangleAndReaverFlip) {
  GbaPtr<WeaponTriangleRule> rules(kWeaponTriangleTableAddr);
  rules[0] = {0, 2, 15, 1};   // Sword > Axe
  rules[1] = {-1, -1, 0, 0};  // Terminator

  GbaPtr<BattleUnit> atk(kBattleActorAddr);
  GbaPtr<BattleUnit> def(kBattleTargetAddr);
  atk->weapon_type = static_cast<uint8_t>(ItemKind::kSword);
  def->weapon_type = static_cast<uint8_t>(ItemKind::kAxe);

  BattleApplyWeaponTriangleEffect(atk, def);
  EXPECT_EQ(atk->wtriangle_hit_bonus, 15);
  EXPECT_EQ(atk->wtriangle_dmg_bonus, 1);
  EXPECT_EQ(def->wtriangle_hit_bonus, -15);
  EXPECT_EQ(def->wtriangle_dmg_bonus, -1);

  // Single Reaver doubles and reverses triangle bonuses.
  def->weapon_attributes = kItemAttrReverseTriangle;
  BattleApplyWeaponTriangleEffect(atk, def);
  EXPECT_EQ(atk->wtriangle_hit_bonus, -30);
  EXPECT_EQ(atk->wtriangle_dmg_bonus, -2);
  EXPECT_EQ(def->wtriangle_hit_bonus, 30);
  EXPECT_EQ(def->wtriangle_dmg_bonus, 2);
}

TEST_F(BattleTest, CoreCombatFormulasAndSilencer) {
  GbaPtr<IInfo> sword(kIInfoTableAddr + sizeof(IInfo));
  sword->kind = static_cast<uint8_t>(ItemKind::kSword);
  sword->attributes = kItemAttrWeapon;
  sword->might = 8;
  sword->hit = 85;
  sword->weight = 7;
  sword->crit = 10;

  GbaPtr<PInfo> p_atk(0x02001000u);
  GbaPtr<JInfo> j_atk(0x02001100u);
  GbaPtr<PInfo> p_def(0x02001200u);
  GbaPtr<JInfo> j_def(0x02001300u);
  j_atk->attributes = kUnitAttrCritBonus | kUnitAttrAssassin;
  j_def->attributes = kUnitAttrBoss;

  GbaPtr<BattleUnit> atk(kBattleActorAddr);
  GbaPtr<BattleUnit> def(kBattleTargetAddr);
  atk->unit.pinfo = GbaPtr<const PInfo>(p_atk.addr);
  atk->unit.jinfo = GbaPtr<const JInfo>(j_atk.addr);
  atk->unit.pow = 12;
  atk->unit.skl = 14;
  atk->unit.spd = 16;
  atk->unit.lck = 10;
  atk->unit.bonus_con = 5;  // AS penalty = 7 - 5 = 2 -> AS = 14
  atk->unit.wexp[0] = 251;  // S-rank (+5 hit, +5 crit)
  atk->weapon = (20 << 8) | 1;
  atk->weapon_before = atk->weapon;
  atk->wtriangle_hit_bonus = 15;
  atk->wtriangle_dmg_bonus = 1;

  def->unit.pinfo = GbaPtr<const PInfo>(p_def.addr);
  def->unit.jinfo = GbaPtr<const JInfo>(j_def.addr);
  def->unit.def = 9;
  def->terrain_defense = 2;
  def->battle_avoid_rate = 30;
  def->battle_dodge_rate = 10;

  ComputeBattleUnitDefense(def, GbaPtr<const BattleUnit>(atk.addr));
  ComputeBattleUnitAttack(atk, GbaPtr<const BattleUnit>(def.addr));
  ComputeBattleUnitSpeed(atk);
  ComputeBattleUnitHitRate(atk);
  ComputeBattleUnitCritRate(atk);
  ComputeBattleUnitWeaponRankBonuses(atk);
  ComputeBattleUnitEffectiveHitRate(atk, GbaPtr<const BattleUnit>(def.addr));
  ComputeBattleUnitEffectiveCritRate(atk, GbaPtr<const BattleUnit>(def.addr));
  ComputeBattleUnitSilencerRate(atk, GbaPtr<const BattleUnit>(def.addr));

  EXPECT_EQ(def->battle_defense, 11);
  EXPECT_EQ(atk->battle_attack, 21);  // (8 + 1) + 12
  EXPECT_EQ(atk->battle_speed, 14);
  EXPECT_EQ(atk->battle_hit_rate, 85 + 28 + 5 + 15 + 5);
  EXPECT_EQ(atk->battle_crit_rate, 10 + 7 + 15 + 5);
  EXPECT_EQ(atk->battle_effective_hit_rate, 100);
  EXPECT_EQ(atk->battle_effective_crit_rate, 27);
  EXPECT_EQ(atk->battle_silencer_rate, 25);  // Assassin vs Boss
}

}  // namespace
}  // namespace fe7
