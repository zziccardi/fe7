// Unit tests for FE7 US battle simulation, hit generation, and unwinding.

#include "game/battle/battle_sim.h"

#include <array>
#include <cstdint>
#include <cstring>

#include "game/battle.h"
#include "game/engine/rng.h"
#include "game/item.h"
#include "game/item/item.h"
#include "game/unit.h"
#include "gtest/gtest.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

alignas(16) std::array<uint8_t, 0x01000000u> gSyntheticRom{};

class BattleSimTest : public ::testing::Test {
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

    // Populate three synthetic items in ROM gItemData:
    // iid 1: Iron Sword, iid 2: Brave Sword, iid 3: Nosferatu (HP steal).
    SetItemEntry(1, /*max_uses=*/46, /*attrs=*/kItemAttrWeapon, /*effect=*/0);
    SetItemEntry(2, /*max_uses=*/30,
                 /*attrs=*/kItemAttrWeapon | kItemAttrBrave, /*effect=*/0);
    SetItemEntry(3, /*max_uses=*/20, /*attrs=*/kItemAttrWeapon, /*effect=*/2);

    // Minimal PInfo/JInfo in synthetic ROM at 0x08001000 / 0x08001100.
    pinfo_addr_ = 0x08001000u;
    jinfo_addr_ = 0x08001100u;
    PInfo p{};
    p.bonus_con = 5;
    JInfo j{};
    j.base_con = 3;
    j.base_mov = 6;
    std::memcpy(gSyntheticRom.data() + (pinfo_addr_ - 0x08000000u), &p,
                sizeof(p));
    std::memcpy(gSyntheticRom.data() + (jinfo_addr_ - 0x08000000u), &j,
                sizeof(j));
  }

  void TearDown() override { gMem = {}; }

  void SetItemEntry(int iid, uint8_t max_uses, uint32_t attrs,
                    uint8_t weapon_effect) {
    IInfo info{};
    info.id = static_cast<uint8_t>(iid);
    info.attributes = attrs;
    info.max_uses = max_uses;
    info.weapon_effect = weapon_effect;
    const uint32_t off = (kIInfoTableAddr - 0x08000000u) +
                         static_cast<uint32_t>(iid) * sizeof(IInfo);
    std::memcpy(gSyntheticRom.data() + off, &info, sizeof(info));
  }

  void SetupUnit(GbaPtr<BattleUnit> bu, int8_t hp, int8_t max_hp,
                 uint16_t weapon, uint32_t attrs, int16_t atk, int16_t def,
                 int16_t spd, int16_t hit, int16_t crit) {
    std::memset(bu.get(), 0, sizeof(BattleUnit));
    bu->unit.pinfo = GbaPtr<const PInfo>(pinfo_addr_);
    bu->unit.jinfo = GbaPtr<const JInfo>(jinfo_addr_);
    bu->unit.hp = hp;
    bu->unit.max_hp = max_hp;
    bu->weapon = weapon;
    bu->weapon_before = weapon;
    bu->weapon_attributes = attrs;
    bu->can_counter = 1;
    bu->battle_attack = atk;
    bu->battle_defense = def;
    bu->battle_speed = spd;
    bu->battle_effective_hit_rate = hit;
    bu->battle_effective_crit_rate = crit;
  }

  std::array<uint8_t, 0x40000u> ewram_{};
  std::array<uint8_t, 0x8000u> iwram_{};
  uint32_t pinfo_addr_ = 0;
  uint32_t jinfo_addr_ = 0;
};

TEST_F(BattleSimTest, BattleRollRespectsSimulateConfig) {
  GbaPtr<BattleStats> stats(kBattleStatsAddr);
  stats->config = kBattleConfigSimulate;
  EXPECT_EQ(BattleRoll1RN(0, true), 1);
  EXPECT_EQ(BattleRoll1RN(100, false), 0);
  EXPECT_EQ(BattleRoll2RN(0, true), 1);
  EXPECT_EQ(BattleRoll2RN(100, false), 0);

  stats->config = kBattleConfigReal;
  RandInit(12345);
  EXPECT_EQ(BattleRoll1RN(100, false), 1);
  EXPECT_EQ(BattleRoll1RN(0, true), 0);
  EXPECT_EQ(BattleRoll2RN(100, false), 1);
  EXPECT_EQ(BattleRoll2RN(0, true), 0);
}

TEST_F(BattleSimTest, BattleUnwindSimulatesInitialRetaliationAndFollowUp) {
  GbaPtr<BattleStats> stats(kBattleStatsAddr);
  stats->config = kBattleConfigSimulate;
  stats->range = 1;

  GbaPtr<BattleUnit> actor(kBattleActorAddr);
  GbaPtr<BattleUnit> target(kBattleTargetAddr);
  SetupUnit(actor, 30, 30, (10u << 8) | 1u, kItemAttrWeapon, 15, 5, 14, 90, 0);
  SetupUnit(target, 25, 25, (10u << 8) | 1u, kItemAttrWeapon, 12, 6, 8, 80, 0);

  BattleUnwind();

  GbaPtr<BattleHit> hits(kBattleHitsAddr);
  // Hit 0: Actor deals 15 - 6 = 9 dmg (target 25 -> 16).
  EXPECT_NE(hits[0].info & kBattleHitInfoBegin, 0);
  EXPECT_EQ(hits[0].hp_change, 9);
  // Hit 1: Target retaliates for 12 - 5 = 7 dmg (actor 30 -> 23).
  EXPECT_NE(hits[1].attributes & kBattleHitAttrRetaliation, 0);
  EXPECT_NE(hits[1].info & kBattleHitInfoRetaliation, 0);
  EXPECT_EQ(hits[1].hp_change, 7);
  // Hit 2: Actor follows up (14 - 8 >= 4) for 9 dmg (target 16 -> 7).
  EXPECT_NE(hits[2].attributes & kBattleHitAttrFollowUp, 0);
  EXPECT_EQ(hits[2].hp_change, 9);
  // Hit 3: Terminator.
  EXPECT_NE(hits[3].info & kBattleHitInfoEnd, 0);
  EXPECT_EQ(actor->unit.hp, 23);
  EXPECT_EQ(target->unit.hp, 7);
}

TEST_F(BattleSimTest, BattleUnwindStopsEarlyOnKillAndHandlesBraveWeapon) {
  GbaPtr<BattleStats> stats(kBattleStatsAddr);
  stats->config = kBattleConfigSimulate;
  stats->range = 1;

  GbaPtr<BattleUnit> actor(kBattleActorAddr);
  GbaPtr<BattleUnit> target(kBattleTargetAddr);
  SetupUnit(actor, 30, 30, (10u << 8) | 2u, kItemAttrWeapon | kItemAttrBrave,
            16, 5, 10, 100, 0);
  SetupUnit(target, 15, 25, (10u << 8) | 1u, kItemAttrWeapon, 12, 6, 10, 80, 0);

  BattleUnwind();

  GbaPtr<BattleHit> hits(kBattleHitsAddr);
  // Brave hit 1: 10 dmg (target 15 -> 5).
  EXPECT_NE(hits[0].attributes & kBattleHitAttrBrave, 0);
  EXPECT_EQ(hits[0].hp_change, 10);
  // Brave hit 2: kills target (clamped to remaining 5 HP), combat ends.
  // FE7 captures round attributes before BattleCheckBrave, so only hit 0 has
  // kBattleHitAttrBrave set.
  EXPECT_EQ(hits[1].attributes & kBattleHitAttrBrave, 0);
  EXPECT_NE(hits[1].info & kBattleHitInfoKills, 0);
  EXPECT_NE(hits[1].info & kBattleHitInfoKillsTarget, 0);
  EXPECT_EQ(hits[1].hp_change, 5);
  EXPECT_NE(hits[2].info & kBattleHitInfoEnd, 0);
  EXPECT_EQ(target->unit.hp, 0);
}

TEST_F(BattleSimTest, HpStealHealsAttackerUpToMaxHp) {
  GbaPtr<BattleStats> stats(kBattleStatsAddr);
  stats->config = kBattleConfigSimulate;
  stats->damage = 10;

  ClearBattleHits();
  GbaPtr<BattleUnit> actor(kBattleActorAddr);
  GbaPtr<BattleUnit> target(kBattleTargetAddr);
  SetupUnit(actor, 24, 30, (10u << 8) | 3u, kItemAttrWeapon, 16, 5, 10, 100, 0);
  SetupUnit(target, 20, 20, (10u << 8) | 1u, kItemAttrWeapon, 12, 6, 10, 80, 0);

  BattleGenerateHitEffects(actor, target);
  EXPECT_EQ(target->unit.hp, 10);
  EXPECT_EQ(actor->unit.hp, 30);
  EXPECT_NE((*GbaPtr<GbaPtr<BattleHit>>(kBattleHitItAddr))->attributes &
                kBattleHitAttrHpSteal,
            0);
}

}  // namespace
}  // namespace fe7
