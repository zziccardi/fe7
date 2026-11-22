// Unit tests for FE7 US post-battle EXP, level-up rolls, WEXP, and updates.

#include "game/battle/battle_exp.h"

#include <array>
#include <cstdint>
#include <cstring>

#include "game/battle.h"
#include "game/engine/rng.h"
#include "game/item.h"
#include "game/item/item.h"
#include "game/unit.h"
#include "game/unit/unit.h"
#include "gtest/gtest.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

alignas(16) std::array<uint8_t, 0x01000000u> gSyntheticRom{};

class BattleExpTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ewram_.fill(0);
    iwram_.fill(0);
    gSyntheticRom.fill(0);
    gMem = {};
    gMem.ewram = ewram_.data();
    gMem.iwram = iwram_.data();
    gMem.rom = gSyntheticRom.data();
    gMem.rom_size = gSyntheticRom.size();

    // Populate JInfo entries 1 (unpromoted) and 2 (promoted) in ROM table.
    JInfo base{};
    base.id = 1;
    base.jid_promote = 2;
    base.max_hp = 60;
    base.max_pow = 20;
    base.max_skl = 20;
    base.max_spd = 20;
    base.max_def = 20;
    base.max_res = 20;
    base.power_level = 3;
    base.wexp[0] = 1;
    SetJobEntry(1, base);

    JInfo promo{};
    promo.id = 2;
    promo.jid_promote = 1;
    promo.max_hp = 60;
    promo.max_pow = 25;
    promo.max_skl = 25;
    promo.max_spd = 25;
    promo.max_def = 25;
    promo.max_res = 25;
    promo.power_level = 3;
    promo.promotion_hp = 4;
    promo.promotion_pow = 2;
    promo.promotion_skl = 2;
    promo.promotion_spd = 2;
    promo.promotion_def = 2;
    promo.promotion_res = 2;
    promo.attributes = 0x100u;  // Promoted
    promo.wexp[0] = 31;
    SetJobEntry(2, promo);

    // Populate PInfo 1 with 100% growths for deterministic level-up testing.
    pinfo_addr_ = 0x08001000u;
    PInfo p{};
    p.id = 1;
    p.growth_hp = 100;
    p.growth_pow = 100;
    p.growth_skl = 100;
    p.growth_spd = 100;
    p.growth_def = 100;
    p.growth_res = 100;
    p.growth_lck = 100;
    std::memcpy(gSyntheticRom.data() + (pinfo_addr_ - 0x08000000u), &p,
                sizeof(p));

    // Populate gUnitLookup for unit ID 1 -> 0x0202BD50.
    const uint32_t unit_addr = 0x0202BD50u;
    const uint32_t lut_off =
        (kUnitLutAddr - 0x08000000u) + 1u * sizeof(uint32_t);
    std::memcpy(gSyntheticRom.data() + lut_off, &unit_addr, sizeof(unit_addr));

    // Populate Iron Sword (iid 1) with wexp=2, cost_per_use=20.
    IInfo sword{};
    sword.id = 1;
    sword.attributes = kItemAttrWeapon;
    sword.max_uses = 46;
    sword.cost_per_use = 20;
    sword.wexp_gain = 2;
    const uint32_t item_off =
        (kIInfoTableAddr - 0x08000000u) + 1u * sizeof(IInfo);
    std::memcpy(gSyntheticRom.data() + item_off, &sword, sizeof(sword));
  }

  void TearDown() override { gMem = {}; }

  void SetJobEntry(int jid, const JInfo& info) {
    const uint32_t off = (kJInfoTableAddr - 0x08000000u) +
                         static_cast<uint32_t>(jid) * sizeof(JInfo);
    std::memcpy(gSyntheticRom.data() + off, &info, sizeof(info));
  }

  std::array<uint8_t, 0x40000u> ewram_{};
  std::array<uint8_t, 0x8000u> iwram_{};
  uint32_t pinfo_addr_ = 0;
};

TEST_F(BattleExpTest, RoundAndKillExpFormulas) {
  GbaPtr<BattleUnit> actor(kBattleActorAddr);
  GbaPtr<BattleUnit> target(kBattleTargetAddr);
  actor->unit.pinfo = GbaPtr<const PInfo>(pinfo_addr_);
  actor->unit.jinfo = GetJobInfo(1);
  actor->unit.id = 1;
  actor->unit.level = 5;
  actor->unit.hp = 20;
  actor->non_zero_damage = 1;

  target->unit.pinfo = GbaPtr<const PInfo>(pinfo_addr_);
  target->unit.jinfo = GetJobInfo(1);
  target->unit.id = static_cast<int8_t>(0x81);
  target->unit.level = 5;
  target->unit.hp = 10;

  // Non-kill equal level: (31 - 0) / 3 = 10.
  EXPECT_EQ(GetBattleUnitExpGain(actor, target), 10);

  // Kill equal level: round(10) + kill(20 + 15 - 7 = 28) = 38.
  target->unit.hp = 0;
  EXPECT_EQ(GetBattleUnitExpGain(actor, target), 38);

  // Zero-damage survival yields 1 EXP.
  actor->non_zero_damage = 0;
  EXPECT_EQ(GetBattleUnitExpGain(actor, target), 1);
}

TEST_F(BattleExpTest, LevelUpRollsAndStatCaps) {
  RandInit(42);
  GbaPtr<Unit> base = GetUnit(1);
  base->pinfo = GbaPtr<const PInfo>(pinfo_addr_);
  base->jinfo = GetJobInfo(1);
  base->id = 1;
  base->max_hp = 59;
  base->pow = 20;  // Already capped at max_pow=20
  base->lck = 30;  // Already capped at 30

  GbaPtr<BattleUnit> bu(kBattleActorAddr);
  std::memcpy(&bu->unit, base.get(), sizeof(Unit));
  bu->unit.level = 19;
  bu->unit.exp = 115;
  bu->exp_gain = 30;

  CheckBattleUnitLevelUp(bu);
  EXPECT_EQ(bu->unit.level, 20);
  EXPECT_EQ(bu->unit.exp, 0xFFu);
  EXPECT_EQ(bu->exp_gain, 15);   // Trimmed by overflow 15
  EXPECT_EQ(bu->change_hp, 1);   // 59 + 1 <= 60
  EXPECT_EQ(bu->change_pow, 0);  // Clamped at max_pow=20
  EXPECT_EQ(bu->change_skl, 1);
  EXPECT_EQ(bu->change_lck, 0);  // Clamped at 30
}

TEST_F(BattleExpTest, UnitPromoteAppliesBonusesCapsAndResetsLevel) {
  GbaPtr<Unit> u = GetUnit(1);
  u->pinfo = GbaPtr<const PInfo>(pinfo_addr_);
  u->jinfo = GetJobInfo(1);
  u->level = 20;
  u->exp = 0xFFu;
  u->max_hp = 58;
  u->pow = 18;
  u->wexp[0] = 50;

  UnitPromote(u);
  EXPECT_EQ(u->jinfo.addr, GetJobInfo(2).addr);
  EXPECT_EQ(u->level, 1);
  EXPECT_EQ(u->exp, 0u);
  EXPECT_EQ(u->max_hp, 60);   // 58 + 4 clamped to 60
  EXPECT_EQ(u->pow, 20);      // 18 + 2
  EXPECT_EQ(u->wexp[0], 80);  // 50 - 1 + 31
}

TEST_F(BattleExpTest, UpdatedWeaponExpEnforcesCapsAndSingleSRank) {
  GbaPtr<BattleUnit> bu(kBattleActorAddr);
  bu->unit.pinfo = GbaPtr<const PInfo>(pinfo_addr_);
  bu->unit.jinfo = GetJobInfo(1);  // Unpromoted: cap 0xB5 (181)
  bu->unit.id = 1;
  bu->unit.hp = 20;
  bu->can_counter = 1;
  bu->weapon = (10u << 8) | 1u;
  bu->weapon_attributes = kItemAttrWeapon;
  bu->weapon_type = 0;
  bu->unit.wexp[0] = 180;
  bu->wexp_multiplier = 2;

  EXPECT_EQ(GetBattleUnitUpdatedWeaponExp(bu), 0xB5);

  // Promoted unit with another S-rank caps at 0xFA (250).
  bu->unit.jinfo = GetJobInfo(2);
  bu->unit.wexp[0] = 249;
  bu->unit.wexp[1] = 0xFB;
  EXPECT_EQ(GetBattleUnitUpdatedWeaponExp(bu), 0xFA);
}

}  // namespace
}  // namespace fe7
