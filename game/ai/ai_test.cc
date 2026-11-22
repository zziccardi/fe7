// Unit tests for FE7 US Enemy AI decision state, reachability, and scoring.

#include "game/ai/ai.h"

#include <array>
#include <cstdint>
#include <cstring>

#include "game/battle.h"
#include "game/item.h"
#include "game/item/item.h"
#include "game/map/map.h"
#include "game/unit.h"
#include "gtest/gtest.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

alignas(16) std::array<uint8_t, 0x01000000u> gSyntheticRom{};

class AiTest : public ::testing::Test {
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

    // Populate a Bow (iid 1, range 2..2) in ROM gItemData.
    IInfo bow{};
    bow.id = 1;
    bow.attributes = kItemAttrWeapon;
    bow.encoded_range = 0x22u;
    std::memcpy(gSyntheticRom.data() + (kIInfoTableAddr - 0x08000000u) +
                    1u * sizeof(IInfo),
                &bow, sizeof(bow));

    // Populate AI combat score coefficient table entry 0 at 0x081D36F4.
    AiCombatScoreCoefficients coeffs{};
    coeffs.coeff_damage_dealt = 2;
    coeffs.coeff_low_hp_opponent = 1;
    coeffs.coeff_friend_zone = 1;
    coeffs.coeff_class_rank_bonus = 1;
    coeffs.coeff_turn_number = 1;
    coeffs.coeff_damage_taken = 1;
    coeffs.coeff_danger = 1;
    coeffs.coeff_low_hp_self = 1;
    coeffs.class_rank_bonuses[0] = 5;
    std::memcpy(
        gSyntheticRom.data() + (kAiCombatScoreCoeffTableAddr - 0x08000000u),
        &coeffs, sizeof(coeffs));
    *GbaPtr<uint32_t>(kAiScoreCoeffPtrAddr) = kAiCombatScoreCoeffTableAddr;
  }

  void TearDown() override { gMem = {}; }

  std::array<uint8_t, 0x40000u> ewram_{};
  std::array<uint8_t, 0x8000u> iwram_{};
};

TEST_F(AiTest, DecisionSetUpdateAndClear) {
  *GbaPtr<uint8_t>(kAiActiveUnitIdAddr) = 0x81u;
  AiSetDecision(4, 5, 1, 0x01, 0, 4, 6);
  GbaPtr<AiDecision> dec(kAiDecisionAddr);
  EXPECT_EQ(dec->unit_id, 0x81u);
  EXPECT_EQ(dec->x_move, 4u);
  EXPECT_EQ(dec->y_move, 5u);
  EXPECT_EQ(dec->action_id, 1u);
  EXPECT_EQ(dec->action_performed, 1);

  AiUpdateDecision(0xFF, 0x02, 0xFF, 7, 0xFF);
  EXPECT_EQ(dec->action_id, 1u);
  EXPECT_EQ(dec->unit_target_id, 0x02u);
  EXPECT_EQ(dec->x_target, 7u);
  EXPECT_EQ(dec->y_target, 6u);

  AiClearDecision();
  EXPECT_EQ(dec->action_performed, 0);
  EXPECT_EQ(dec->unit_id, 0u);
}

TEST_F(AiTest, BirdsEyeReachabilityAndListLookups) {
  const uint32_t j_addr = 0x08001100u;
  JInfo j{};
  j.base_mov = 5;
  std::memcpy(gSyntheticRom.data() + (j_addr - 0x08000000u), &j, sizeof(j));

  GbaPtr<Unit> a(0x02010000u);
  GbaPtr<Unit> b(0x02010100u);
  a->jinfo = GbaPtr<const JInfo>(j_addr);
  b->jinfo = GbaPtr<const JInfo>(j_addr);
  a->x = 2;
  a->y = 2;
  b->x = 9;
  b->y = 2;  // dist = 7 == mov(5) + range(2)
  EXPECT_TRUE(AiIsWithinReachByBirdsEyeDistance(
      GbaPtr<const Unit>(a.addr), GbaPtr<const Unit>(b.addr), 1u));
  b->x = 10;  // dist = 8 > 7, but <= mov(5) + mov(5) + range(2) = 12
  EXPECT_FALSE(AiIsWithinReachByBirdsEyeDistance(
      GbaPtr<const Unit>(a.addr), GbaPtr<const Unit>(b.addr), 1u));
  EXPECT_TRUE(AiCouldReachByBirdsEyeDistance(GbaPtr<const Unit>(a.addr),
                                             GbaPtr<const Unit>(b.addr), 1u));

  GbaPtr<uint16_t> shorts(0x02010200u);
  shorts[0] = 0x1234u;
  shorts[1] = 0x5678u;
  shorts[2] = 0u;
  EXPECT_TRUE(AiIsInShortList(GbaPtr<const uint16_t>(shorts.addr), 0x5678u));
  EXPECT_FALSE(AiIsInShortList(GbaPtr<const uint16_t>(shorts.addr), 0x9999u));
}

TEST_F(AiTest, DamageDealtAndTakenScoreComponents) {
  GbaPtr<BattleUnit> actor(kBattleActorAddr);
  GbaPtr<BattleUnit> target(kBattleTargetAddr);
  actor->unit.hp = 15;
  actor->battle_attack = 20;
  actor->battle_defense = 5;
  actor->battle_effective_hit_rate = 100;

  target->unit.hp = 10;
  target->weapon = 1u;
  target->battle_attack = 15;
  target->battle_defense = 10;
  target->battle_effective_hit_rate = 80;

  // Expected damage dealt = (20-10)*100/100 = 10, * coeff(2) = 20.
  EXPECT_EQ(AiGetDamageDealtCombatScoreComponent(), 20);
  // Opponent low HP = (20 - 10) * 1 = 10.
  EXPECT_EQ(AiGetOpponentLowHpScoreComponent(), 10);
  // Expected damage taken = (15-5)*80/100 = 8, * coeff(1) = 8.
  EXPECT_EQ(AiGetDamageTakenScoreComponent(), 8);
  // Unarmed target returns -10 damage taken penalty (bonus to attack!).
  target->weapon = 0u;
  EXPECT_EQ(AiGetDamageTakenScoreComponent(), -10);
  // Lethal hit returns 50.
  target->unit.hp = 0;
  EXPECT_EQ(AiGetDamageDealtCombatScoreComponent(), 50);
}

}  // namespace
}  // namespace fe7
