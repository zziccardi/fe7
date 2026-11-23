// Pure-native FE7 E2E combat, forecasting, support, weapon-break, special
// combat mechanics (Reaver, Luna, Silencer, Triangle Attack), and promotion
// integration tests.

#include <cstdint>

#include "game/ai/ai.h"
#include "game/battle.h"
#include "game/battle/battle.h"
#include "game/battle/battle_exp.h"
#include "game/battle/battle_sim.h"
#include "game/engine/rng.h"
#include "game/item.h"
#include "game/item/item.h"
#include "game/map/map.h"
#include "game/phase/phase.h"
#include "game/support/support.h"
#include "game/unit.h"
#include "game/unit/unit.h"
#include "gtest/gtest.h"
#include "runtime/gba_ptr.h"
#include "testing/e2e/skirmish_fixture.h"

namespace fe7::e2e {
namespace {

TEST_F(SkirmishIntegrationTest, FullMultiTurnCombatLevel) {
  RandInit(kRngSeedMultiTurn);

  GbaPtr<Unit> lyn = Lyn();
  GbaPtr<Unit> kent = Kent();
  GbaPtr<Unit> brigand_a = BrigandA();
  GbaPtr<Unit> brigand_b = BrigandB();

  // --- Turn 1, Player Phase (Blue) ---
  EXPECT_EQ(GetCurrentPhase(), kFactionBlue);
  EXPECT_EQ(CountUnitsByFaction(kFactionBlue), 2);
  EXPECT_EQ(CountUnitsByFaction(kFactionRed), 2);

  // 1. BFS movement flood for Lyn (5 mov): wall at (3,2) is unreachable,
  //    plains tile (2,3) next to Kent & Brigand A is reachable at cost 1.
  //
  //      x: 0 1 2 3 4 5
  //    y=2: . . . # . .   <- Wall (3,2) blocks direct north route
  //    y=3: . . . L F .   <- Lyn moves (1,3) -> (3,3) (dist 2 from Kent)
  //    y=4: . . K A . B   <- Kent (2,4) gives C-support aura; A at (3,4)
  MapFloodUnit(AsConst(lyn));
  GbaPtr<GbaPtr<uint8_t>> mov_map = MovementMap();
  EXPECT_EQ(mov_map[2][3], kMoveCostUnreachable);  // Wall tile blocked.
  EXPECT_LE(mov_map[3][2], 5u);                    // (2,3) reachable.
  EXPECT_EQ(mov_map[3][2], 1u);

  ASSERT_LE(mov_map[3][3], 5u);
  PlaceUnit(lyn, 3, 3);

  // 2. Verify live Wind+Fire C-support aura from Kent (distance = 2 <= 3).
  const SupportBonuses lyn_bonuses = ComputeSupportBonuses(AsConst(lyn));
  EXPECT_GT(lyn_bonuses.bonus_attack, 0);
  EXPECT_GT(lyn_bonuses.bonus_hit, 0);

  // 3. Lyn attacks Brigand A (Sword vs. Axe WTA, speed double, guaranteed KO
  //    and level-up from 90 EXP with 100% growths).
  const int sword_uses_before = GetItemUses(lyn->items[0]);
  RunCombatExchange(lyn, brigand_a, kBattleConfigReal);
  lyn->flags |= kUnitFlagTurnEnded;

  GbaPtr<const BattleUnit> actor(kBattleActorAddr);
  EXPECT_EQ(actor->wtriangle_hit_bonus, 15);
  EXPECT_EQ(actor->wtriangle_dmg_bonus, 1);
  EXPECT_EQ(brigand_a->hp, 0);
  EXPECT_NE(brigand_a->flags & kUnitFlagDead, 0u);
  EXPECT_LT(GetItemUses(lyn->items[0]), sword_uses_before);
  EXPECT_EQ(lyn->level, 2);    // Leveled up!
  EXPECT_GT(lyn->max_hp, 18);  // 100% growth rolled +1.
  EXPECT_GT(lyn->pow, 8);
  EXPECT_GT(lyn->wexp[static_cast<int>(ItemKind::kSword)], 1u);
  EXPECT_EQ(CountUnitsByFaction(kFactionRed), 1);

  // Kent moves to (4, 4) to screen Brigand B, then ends turn.
  PlaceUnit(kent, 4, 4, /*end_turn=*/true);

  // 4. Phase switch: Blue -> Red. Blue units are refreshed.
  EXPECT_EQ(BmMainChangePhase(), 1);
  EXPECT_EQ(GetCurrentPhase(), kFactionRed);
  EXPECT_EQ(lyn->flags & kUnitFlagTurnEnded, 0u);
  EXPECT_EQ(kent->flags & kUnitFlagTurnEnded, 0u);

  // --- Turn 1, Enemy Phase (Red) ---
  // 5. Brigand B refreshes the AI danger map natively, runs movement flood,
  //    evaluates position & combat score against Kent, and attacks Kent.
  SetActiveAiUnit(brigand_b);
  SetDangerMapFilled(false);
  AiRefreshDangerMap();
  EXPECT_TRUE(IsDangerMapFilled());
  EXPECT_EQ(BmMainCheckVictory(), 1);

  MapFloodUnit(AsConst(brigand_b));
  GbaPtr<GbaPtr<uint8_t>> rng_map = RangeMap();
  MapFill(rng_map, 0);
  // Tiles adjacent to Kent (4, 4): (5, 4) is forest (bonus def/avo).
  rng_map[4][5] = 1;
  rng_map[3][4] = 1;

  AiPos best_pos{};
  ASSERT_TRUE(FindBestAiPosition(&best_pos));
  EXPECT_EQ(best_pos.x, 5);
  EXPECT_EQ(best_pos.y, 4);  // Forest tile (5, 4) preferred.

  // Score Brigand B vs. Kent using simulated battle stats.
  RunCombatExchange(brigand_b, kent, kBattleConfigSimulate);
  const uint32_t score =
      ScoreAiCombat(static_cast<uint8_t>(best_pos.x),
                    static_cast<uint8_t>(best_pos.y), AsConst(kent));
  EXPECT_GT(score, 0u);

  RecordAiDecision({
      .x_move = best_pos.x,
      .y_move = best_pos.y,
      .action_id = kAiActionAttack,
      .target = kent,
  });
  ExpectAiDecision({.action_id = kAiActionAttack, .target = kent});

  // Execute Brigand B's attack on Kent.
  RunCombatExchange(brigand_b, kent, kBattleConfigReal);
  brigand_b->flags |= kUnitFlagTurnEnded;

  // 6. Advance Red -> Green -> Blue (Turn 1 -> Turn 2).
  //    Position Lyn adjacent to Kent before turn wrap to verify
  //    ProcessSupportGains() runs on Turn 2 start.
  PlaceUnit(lyn, 4, 3);
  const uint8_t sup_before = lyn->supports[0];

  EXPECT_EQ(BmMainChangePhase(), 1);
  EXPECT_EQ(GetPhaseFaction(), kFactionGreen);
  EXPECT_EQ(BmMainChangePhase(), 1);
  EXPECT_EQ(GetPhaseFaction(), kFactionBlue);
  EXPECT_EQ(GetCurrentPhase(), kFactionBlue);
  EXPECT_EQ(GetTurnNumber(), 2u);
  EXPECT_EQ(lyn->supports[0], sup_before + 5u);

  // --- Turn 2, Player Phase: Rout remaining Brigand B ---
  if ((brigand_b->flags & kUnitFlagDead) == 0u) {
    PlaceUnit(lyn, 5, 3);
    RunCombatExchange(lyn, brigand_b, kBattleConfigReal);
  }
  EXPECT_EQ(CountUnitsByFaction(kFactionRed), 0);
  EXPECT_GT(CountUnitsByFaction(kFactionBlue), 0);
}

TEST_F(SkirmishIntegrationTest, BattleForecastDoesNotMutateUnits) {
  GbaPtr<Unit> lyn = Lyn();
  GbaPtr<Unit> brigand_a = BrigandA();
  PlaceUnit(lyn, 3, 3);

  const int8_t lyn_hp_before = lyn->hp;
  const int8_t enemy_hp_before = brigand_a->hp;
  const uint16_t sword_before = lyn->items[0];

  RunCombatExchange(lyn, brigand_a, kBattleConfigSimulate);

  GbaPtr<const BattleUnit> actor(kBattleActorAddr);
  GbaPtr<const BattleUnit> target(kBattleTargetAddr);
  EXPECT_GT(actor->battle_attack, target->battle_defense);
  EXPECT_EQ(lyn->hp, lyn_hp_before);
  EXPECT_EQ(brigand_a->hp, enemy_hp_before);
  EXPECT_EQ(lyn->items[0], sword_before);
}

TEST_F(SkirmishIntegrationTest, WeaponBreakAndSupportRankUpAcrossTurns) {
  RandInit(kRngSeedWeaponBreak);
  GbaPtr<Unit> lyn = Lyn();
  GbaPtr<Unit> kent = Kent();
  GbaPtr<Unit> brigand_a = BrigandA();

  // Give Lyn a 1-use Iron Sword so it breaks on her first strike.
  lyn->items[0] = MakeItem(kIidIronSword, 1);
  PlaceUnit(lyn, 3, 3);

  RunCombatExchange(lyn, brigand_a, kBattleConfigReal);
  EXPECT_EQ(lyn->items[0], 0u);

  // Advance to Turn 2 (FE7 skips support EXP on Turn 1) and place Lyn & Kent
  // adjacent at 155 support EXP (5 shy of B-support threshold 160).
  SetTurnNumber(2);
  lyn->supports[0] = 155;
  kent->supports[0] = 155;
  PlaceUnit(lyn, 2, 3);
  PlaceUnit(kent, 2, 4);

  EXPECT_FALSE(CanUnitSupportNow(AsConst(lyn), 0));
  ProcessSupportGains();
  EXPECT_EQ(lyn->supports[0], 160u);
  EXPECT_TRUE(CanUnitSupportNow(AsConst(lyn), 0));

  UnitGainSupportLevel(lyn, 0);
  EXPECT_EQ(GetSupportLevelBySupportIndex(AsConst(lyn), 0), kSupportRankB);
}

TEST_F(SkirmishIntegrationTest,
       SpecialCombatMechanicsReaverLunaSilencerAndPromotion) {
  RandInit(kRngSeedSpecialCombat);

  // Register Lancereaver (Sword with Reaver reversal), Luna (Dark tome
  // negating Resistance), promoted Blade Lord, Assassin, and Pegasus Knight.
  RegisterItem({.id = kIidLancereaver,
                .kind = ItemKind::kSword,
                .attributes = kItemAttrWeapon | kItemAttrReverseTriangle,
                .max_uses = 15,
                .might = 9,
                .hit = 75,
                .weight = 9,
                .encoded_range = kEncodedRange1To1,
                .required_wexp = kWexpERank,
                .wexp_gain = 2});
  RegisterItem(
      {.id = kIidLuna,
       .kind = ItemKind::kDark,
       .attributes = kItemAttrWeapon | kItemAttrMagic | kItemAttrNegateDefense,
       .max_uses = 30,
       .might = 0,
       .hit = 95,
       .weight = 12,
       .crit = 20,
       .encoded_range = kEncodedRange1To2,
       .required_wexp = kWexpERank,
       .wexp_gain = 1});
  RegisterJob({.id = kJidLord,
               .promote_jid = kJidBladeLord,
               .mov = 5,
               .con = 5,
               .power_level = 3});
  RegisterJob({.id = kJidBladeLord,
               .mov = 6,
               .con = 6,
               .power_level = 4,
               .promotion_hp = 3,
               .promotion_pow = 2,
               .promotion_skl = 2,
               .promotion_spd = 1,
               .promotion_def = 3,
               .promotion_res = 5,
               .attributes = kUnitAttrPromoted,
               .base_wexp_sword = 31});
  RegisterJob({.id = kJidAssassin,
               .mov = 6,
               .con = 8,
               .power_level = 4,
               .attributes = kUnitAttrPromoted | kUnitAttrAssassin});
  RegisterJob({.id = kJidPegasusKnight,
               .mov = 7,
               .con = 5,
               .power_level = 3,
               .attributes = kUnitAttrPegasus | kUnitAttrTriangleAttackPegasi});
  RegisterChar({.id = kPidFlorina, .default_jid = kJidPegasusKnight});
  RegisterChar({.id = kPidFiora, .default_jid = kJidPegasusKnight});

  GbaPtr<Unit> lyn = Lyn();
  GbaPtr<Unit> brigand_a = BrigandA();

  // 1. Reaver reversal: Sword normally beats Axe (+15 Hit, +1 Dmg), so a
  //    Lancereaver Sword vs. Iron Axe reverses and doubles to (-30 Hit, -2
  //    Dmg).
  lyn->items[0] = MakeItem(kIidLancereaver, 15);
  PlaceUnit(lyn, 3, 3);
  RunCombatExchange(lyn, brigand_a, kBattleConfigSimulate);
  GbaPtr<const BattleUnit> actor(kBattleActorAddr);
  GbaPtr<const BattleUnit> target(kBattleTargetAddr);
  EXPECT_EQ(actor->wtriangle_hit_bonus, -30);
  EXPECT_EQ(actor->wtriangle_dmg_bonus, -2);
  EXPECT_EQ(target->wtriangle_hit_bonus, 30);
  EXPECT_EQ(target->wtriangle_dmg_bonus, 2);

  // 2. Luna negates defender Resistance completely during battle calculation.
  brigand_a->res = 15;
  lyn->items[0] = MakeItem(kIidLuna, 30);
  SetWeaponExp(lyn, ItemKind::kDark, kWexpERank);
  RunCombatExchange(lyn, brigand_a, kBattleConfigSimulate);
  EXPECT_EQ(target->battle_defense, 0);

  // 3. Assassin Silencer rates: 50% on normal enemies, 25% on bosses.
  lyn->jinfo = GetJobInfo(kJidAssassin);
  lyn->items[0] = MakeItem(kIidIronSword, 46);
  RunCombatExchange(lyn, brigand_a, kBattleConfigSimulate);
  EXPECT_EQ(actor->battle_silencer_rate, 50);

  RegisterChar({.id = kPidBrigandA,
                .default_jid = kJidBrigand,
                .attributes = kUnitAttrBoss});
  RunCombatExchange(lyn, brigand_a, kBattleConfigSimulate);
  EXPECT_EQ(actor->battle_silencer_rate, 25);

  // 4. Triangle Attack detection: 3 Pegasus Knights surrounding Brigand A at
  //    (3, 4) from (3, 3), (2, 4), and (4, 4).
  lyn->jinfo = GetJobInfo(kJidPegasusKnight);
  ConfigureUnit(kUnitIdExtraBlue1, {.pid = kPidFlorina,
                                    .jid = kJidPegasusKnight,
                                    .x = 2,
                                    .y = 4,
                                    .weapon_kind = ItemKind::kLance,
                                    .weapon_iid = kIidIronLance});
  ConfigureUnit(kUnitIdExtraBlue2, {.pid = kPidFiora,
                                    .jid = kJidPegasusKnight,
                                    .x = 4,
                                    .y = 4,
                                    .weapon_kind = ItemKind::kLance,
                                    .weapon_iid = kIidIronLance});
  SyncUnitMap();

  GbaPtr<BattleUnit> mut_actor(kBattleActorAddr);
  GbaPtr<BattleUnit> mut_target(kBattleTargetAddr);
  InitBattleUnit(mut_actor, AsConst(lyn));
  InitBattleUnit(mut_target, AsConst(brigand_a));
  EXPECT_TRUE(BattleCheckTriangleAttackOrder(mut_actor, mut_target));
  GbaPtr<const BattleStats> bstats(kBattleStatsAddr);
  EXPECT_TRUE(static_cast<bool>(bstats->ta_unit_a));
  EXPECT_TRUE(static_cast<bool>(bstats->ta_unit_b));

  // 5. Promotion (`UnitPromote`): Lord -> Blade Lord applies promotion gains,
  //    updates class pointer, and resets level/EXP to 1/0.
  lyn->jinfo = GetJobInfo(kJidLord);
  lyn->level = 10;
  lyn->exp = 55;
  const int8_t hp_before = lyn->max_hp;
  const int8_t pow_before = lyn->pow;
  UnitPromote(lyn);
  EXPECT_EQ(lyn->jinfo->id, kJidBladeLord);
  EXPECT_EQ(lyn->level, 1);
  EXPECT_EQ(lyn->exp, 0u);
  EXPECT_EQ(lyn->max_hp, hp_before + 3);
  EXPECT_EQ(lyn->pow, pow_before + 2);
}

}  // namespace
}  // namespace fe7::e2e
