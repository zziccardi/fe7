// Pure-native FE7 E2E Enemy AI turn decision, staff targeting, action dispatch,
// and 6C Proc scheduler loop integration tests.

#include <cstdint>

#include "game/ai/ai.h"
#include "game/ai/ai_actions.h"
#include "game/ai/ai_combat.h"
#include "game/ai/ai_cp.h"
#include "game/ai/ai_decide.h"
#include "game/ai/ai_script.h"
#include "game/ai/ai_staff_loot.h"
#include "game/engine/proc.h"
#include "game/engine/rng.h"
#include "game/item.h"
#include "game/phase/phase.h"
#include "game/proc.h"
#include "game/unit.h"
#include "game/unit/unit.h"
#include "gtest/gtest.h"
#include "runtime/gba_ptr.h"
#include "testing/e2e/skirmish_fixture.h"

namespace fe7::e2e {
namespace {

TEST_F(SkirmishIntegrationTest, FullEnemyAiTurnDecisionAndExecution) {
  RandInit(kRngSeedEnemyAiTurn);
  RegisterEnemyAiTurnShims();
  InitEnemyAiTurnRomTables();

  GbaPtr<Unit> lyn = Lyn();
  GbaPtr<Unit> brigand_a = BrigandA();
  GbaPtr<Unit> brigand_b = BrigandB();
  GbaPtr<AiDecision> dec(kAiDecisionAddr);
  GbaPtr<Proc> proc(kProcsAddr);

  // Switch to Red (Enemy) phase.
  SetPhaseFaction(kFactionRed);

  // --- Phase A: Wounded enemy recovers with Vulnerary via CpDecideMain ---
  brigand_a->hp = 4;  // Below 50% HP -> recovery mode
  brigand_a->items[1] = MakeItem(kIidVulnerary, 3);
  PrepareAiTurn(brigand_a, {.recovery_mode = kAiRecoveryModeStationary});

  CpDecideMain();
  ExpectAiDecision({.action_id = kAiActionUseItem, .item_slot = 1});

  CpPerformDispatchAction(proc);
  EXPECT_EQ(GetProcDispatchedCallback(proc), ThumbAddr(kAddrCpPerformUseItem));

  // --- Phase B: Healthy enemy executes Script A attack + combat sim ---
  //   Row y=3 layout:  . . L . B . . .   (Lyn at (2,3), Brigand B at (4,3))
  brigand_b->hp = brigand_b->max_hp;
  brigand_b->ai_a = 0u;
  brigand_b->ai_a_pc = 0u;
  PlaceUnit(lyn, 2, 3);
  PlaceUnit(brigand_b, 4, 3);
  PrepareAiTurn(brigand_b);

  CpDecideMain();
  // With real native BattleGenerateSimulation + BattleUnwind, axe-wielding
  // Brigand B scores higher attacking lance-wielding Kent (WTA advantage) than
  // sword-wielding Lyn (WTA disadvantage).
  ExpectAiDecision({.action_id = kAiActionAttack, .target = Kent()});

  // --- Phase C: Staff healer dispatches Heal staff via AiTrySpecialAction ---
  //   Row y=3 layout:  . . L . B A . .   (wounded Brigand A adjacent at (5,3))
  MarkUnitForRecovery(brigand_a);
  SetWeaponExp(brigand_b, ItemKind::kStaff, kWexpSRank);
  brigand_b->items[1] = MakeItem(kIidHealStaff, 30);
  PlaceUnit(brigand_a, 5, 3);

  SetAiHealThreshold(80);
  AiClearDecision();
  EXPECT_TRUE(AiTrySpecialAction(ThumbAddr(kAddrAiIsUnitEnemy)));
  ExpectAiDecision({.action_id = kAiActionStaff, .target = brigand_a});

  CpPerformDispatchAction(proc);
  EXPECT_EQ(GetProcDispatchedCallback(proc), ThumbAddr(kAddrCpPerformStaff));

  // --- Phase D: CpPerformActionLoop syncs unit coords and breaks proc ---
  dec->action_id = kAiActionWait;
  dec->x_move = 3u;
  dec->y_move = 4u;
  CpPerformDispatchAction(proc);
  EXPECT_EQ(GetProcDispatchedCallback(proc), ThumbAddr(kAddrCpPerformWaitDone));
  CpPerformActionLoop(proc);
  EXPECT_EQ(brigand_b->x, 3);
  EXPECT_EQ(brigand_b->y, 4);
}

TEST_F(SkirmishIntegrationTest, ProcLoopDrivenPhaseAndAiExecution) {
  RandInit(kRngSeedProcLoop);
  InitProcs();

  GbaPtr<Unit> brigand_b = BrigandB();
  GbaPtr<AiDecision> dec(kAiDecisionAddr);

  SetPhaseFaction(kFactionRed);
  PrepareAiTurn(brigand_b);
  dec->action_id = kAiActionWait;
  dec->x_move = 4u;
  dec->y_move = 5u;

  // Build a real 6C Proc bytecode script:
  //   1. kCall(CpPerformDispatchAction) -> stores WaitDone callback in payload
  //   2. kRepeat(CpPerformActionLoop)   -> moves unit to (4, 5) & breaks repeat
  //   3. kEnd                           -> deallocates proc cleanly
  const ProcScr ai_perform_scr[3] = {
      {.cmd = static_cast<int16_t>(ProcCmd::kCall),
       .imm = 0,
       .ptr = GbaPtr<const void>(ThumbAddr(kAddrCpPerformDispatchAction))},
      {.cmd = static_cast<int16_t>(ProcCmd::kRepeat),
       .imm = 0,
       .ptr = GbaPtr<const void>(ThumbAddr(kAddrCpPerformActionLoop))},
      {.cmd = static_cast<int16_t>(ProcCmd::kEnd), .imm = 0, .ptr = {}},
  };
  WriteRom(kSyntheticProcScrAddr, ai_perform_scr);

  GbaPtr<Proc> proc =
      SpawnProc(GbaPtr<const ProcScr>(kSyntheticProcScrAddr), kProcTree3);
  ASSERT_TRUE(static_cast<bool>(proc));
  EXPECT_EQ(GetProcDispatchedCallback(proc), ThumbAddr(kAddrCpPerformWaitDone));

  // Frame 1: `CpPerformActionLoop` executes via `ProcRun`, moves Brigand B to
  // (4, 5), and calls `ProcBreak` to clear `proc_repeat_func`.
  ProcRun(proc);
  EXPECT_EQ(brigand_b->x, 4);
  EXPECT_EQ(brigand_b->y, 5);
  EXPECT_EQ(proc->proc_repeat_func, 0u);

  // Frame 2: `ProcRun` resumes the bytecode script at `kEnd` and deallocates
  // the proc slot.
  ProcRun(proc);
  EXPECT_FALSE(static_cast<bool>(
      ProcFind(GbaPtr<const ProcScr>(kSyntheticProcScrAddr))));
}

TEST_F(SkirmishIntegrationTest, ThreeFactionBlueGreenRedSkirmish) {
  RandInit(kRngSeedThreeFaction);
  RegisterEnemyAiTurnShims();
  InitEnemyAiTurnRomTables();
  RegisterChar({.id = kPidGreenAlly, .default_jid = kJidCavalier});

  // Place Blue Lyn (3,3), Green Ally (4,4), and Red Brigand A (5,4).
  //   Row y=3: . . . L F . . .
  //   Row y=4: . . . . G A . .   (G = Green Ally, A = Red Brigand A)
  GbaPtr<Unit> lyn = Lyn();
  GbaPtr<Unit> brigand_a = BrigandA();
  PlaceUnit(lyn, 3, 3);
  PlaceUnit(Kent(), 0, 0);
  PlaceUnit(BrigandB(), 7, 7);
  PlaceUnit(brigand_a, 5, 4);
  ConfigureUnit(kUnitIdGreenAlly, {
                                      .pid = kPidGreenAlly,
                                      .jid = kJidCavalier,
                                      .level = 3,
                                      .x = 4,
                                      .y = 4,
                                      .hp = 24,
                                      .pow = 10,
                                      .skl = 9,
                                      .spd = 10,
                                      .def = 8,
                                      .weapon_kind = ItemKind::kLance,
                                      .weapon_iid = kIidIronLance,
                                  });
  SyncUnitMap();
  GbaPtr<Unit> ally = GreenAlly();

  // 1. Verify 3-faction allegiance & unit counts.
  EXPECT_EQ(CountUnitsByFaction(kFactionGreen), 1);
  EXPECT_TRUE(AreUnitIdsAllied(lyn->id, ally->id));
  EXPECT_FALSE(AreUnitIdsAllied(lyn->id, brigand_a->id));
  EXPECT_FALSE(AreUnitIdsAllied(ally->id, brigand_a->id));
  EXPECT_FALSE(IsSameAllegiance(lyn->id, ally->id));

  // 2. Rotate Blue -> Red -> Green via `BmMainChangePhase`.
  EXPECT_EQ(BmMainChangePhase(), 1);
  EXPECT_EQ(GetPhaseFaction(), kFactionRed);
  EXPECT_EQ(BmMainChangePhase(), 1);
  EXPECT_EQ(GetPhaseFaction(), kFactionGreen);

  // 3. During Green Phase, Green AI executes Script A and targets hostile Red
  //    Brigand A (wounded to 6 HP) while never targeting allied Blue Lyn.
  brigand_a->hp = 6;
  ally->ai_a = 0u;
  ally->ai_a_pc = 0u;
  PrepareAiTurn(ally);
  CpDecideMain();
  ExpectAiDecision({.action_id = kAiActionAttack, .target = brigand_a});

  // 4. Wrapping Green -> Blue advances the turn counter to Turn 2.
  EXPECT_EQ(BmMainChangePhase(), 1);
  EXPECT_EQ(GetPhaseFaction(), kFactionBlue);
  EXPECT_EQ(GetTurnNumber(), 2u);
}

TEST_F(SkirmishIntegrationTest, ThiefAiStealChestLootAndAntitoxin) {
  RandInit(kRngSeedThiefLoot);
  RegisterEnemyAiTurnShims();
  InitEnemyAiTurnRomTables();
  InitThiefLootRomTables();

  // Treasure-room 8x8 map with a Chest ('C') at (6, 2) behind a wall corridor.
  LoadTerrainFromAscii({
      "........",  // y=0
      "........",  // y=1
      "....#.C.",  // y=2: Chest at (6,2)
      "....#...",  // y=3
      "........",  // y=4
      "........",  // y=5
      "........",  // y=6
      "........",  // y=7
  });

  // Fast Red Thief (spd 14) with Iron Sword, Lockpick (slot 1), Antitoxin (2).
  GbaPtr<Unit> thief = BrigandB();
  thief->jinfo = GetJobInfo(kJidThief);
  thief->spd = 14;
  thief->items[0] = MakeItem(kIidIronSword, 46);
  thief->items[1] = MakeItem(kIidLockpick, 15);
  thief->items[2] = MakeItem(kIidAntitoxin, 3);
  PlaceUnit(thief, 3, 4);

  // Slower Blue Kent (spd 9) carrying a stealable Vulnerary in slot 1.
  GbaPtr<Unit> kent = Kent();
  kent->items[1] = MakeItem(kIidVulnerary, 3);
  PlaceUnit(kent, 2, 4);
  PlaceUnit(Lyn(), 0, 0);
  PlaceUnit(BrigandA(), 7, 7);

  SetPhaseFaction(kFactionRed);

  // 1. Thief floods movement & spots Kent's Vulnerary via `AiTryStealAction`.
  EXPECT_EQ(AiGetBestStealableItemSlot(AsConst(kent)), 1);
  PrepareAiTurn(thief);
  AiMapFloodRangeUnitMov(thief);
  EXPECT_TRUE(AiTryStealAction());
  ExpectAiDecision(
      {.action_id = kAiActionSteal, .target = kent, .item_slot = 1});

  // 2. Poisoning the Thief triggers `AiTryUseAntitoxin` for slot 2.
  SetUnitStatus(thief, static_cast<int>(UnitStatus::kPoison));
  SetAiSpecialItemFlags(kAiSpecialItemAntitoxinBit);
  PrepareAiTurn(thief);
  AiTryUseAntitoxin(/*slot=*/2);
  ExpectAiDecision({.action_id = kAiActionUseItem, .item_slot = 2});

  // 3. Clearing poison and placing the Thief near the chest triggers
  //    `AiTryUseChestKeyOrLockpick` to move onto (6, 2) and loot with slot 1.
  SetUnitStatus(thief, static_cast<int>(UnitStatus::kNone));
  PlaceUnit(thief, 5, 3);
  SetAiSpecialItemFlags(kAiSpecialItemLootBit);
  PrepareAiTurn(thief);
  AiTryUseChestKeyOrLockpick(/*slot=*/1);
  ExpectAiDecision({.action_id = kAiActionUseItem, .item_slot = 1});
  GbaPtr<const AiDecision> dec(kAiDecisionAddr);
  EXPECT_EQ(dec->x_move, 6u);
  EXPECT_EQ(dec->y_move, 2u);
}

}  // namespace
}  // namespace fe7::e2e
