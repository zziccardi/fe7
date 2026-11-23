// Pure-native FE7 E2E tactical map, Fog of War, traps, unit actions, status
// conditions, and auto-end-turn integration tests.

#include <cstdint>

#include "game/action/action.h"
#include "game/ai/ai.h"
#include "game/ai/ai_combat.h"
#include "game/ai/ai_cp.h"
#include "game/ai/ai_decide.h"
#include "game/battle.h"
#include "game/battle/battle_exp.h"
#include "game/engine/proc.h"
#include "game/engine/rng.h"
#include "game/item.h"
#include "game/item/item.h"
#include "game/map/ballista.h"
#include "game/map/map.h"
#include "game/map/trap.h"
#include "game/phase/phase.h"
#include "game/proc.h"
#include "game/unit.h"
#include "game/unit/unit.h"
#include "gtest/gtest.h"
#include "runtime/gba_ptr.h"
#include "testing/e2e/skirmish_fixture.h"

namespace fe7::e2e {
namespace {

TEST_F(SkirmishIntegrationTest, TacticalFogTrapsActionsAndTopLevelAi) {
  RandInit(kRngSeedTacticalFog);
  GbaPtr<Unit> lyn = Lyn();
  GbaPtr<Unit> kent = Kent();
  GbaPtr<Unit> brigand_a = BrigandA();
  GbaPtr<Unit> brigand_b = BrigandB();

  // --- 1. Traps, Light Rune & Fog of War (game/map/trap) ---
  SetFogVision(3);
  SetPhaseFaction(kFactionBlue);

  GbaPtr<Trap> rune = AddLightRune(2, 2);
  ASSERT_TRUE(static_cast<bool>(rune));
  ApplyLightRuneTerrain();
  EXPECT_EQ(TerrainMap()[2][2], kTerrainWall);

  GbaPtr<Trap> fire = AddFireTrap(4, 3, /*turn_delay=*/1, /*interval=*/2);
  ASSERT_TRUE(static_cast<bool>(fire));
  EXPECT_EQ(GetTrapAt(4, 3).addr, fire.addr);

  RefreshEntityMaps();
  GbaPtr<GbaPtr<uint8_t>> fog = FogMap();
  EXPECT_GT(fog[lyn->y][lyn->x], 0u);  // Blue unit illuminates its own tile
  EXPECT_EQ(fog[7][7], 0u);            // Far corner remains shrouded

  // Tick and reset damaging traps, then clean up Light Rune.
  TickDamagingTraps();
  ResetExpiredDamagingTraps();
  RemoveTrap(rune);
  EXPECT_FALSE(static_cast<bool>(GetTrapAt(2, 2)));
  TerrainMap()[2][2] = kTerrainPlains;

  // Disable fog for the action/AI portion.
  SetFogVision(0);
  RefreshEntityMaps();

  // --- 2. Unit Actions, Stat Boosters, Autolevel & Rescue/Drop ---
  const int8_t lyn_max_hp_before = lyn->max_hp;
  lyn->items[1] = MakeItem(kIidAngelicRobe, 1);
  EXPECT_EQ(FindUnitItemSlot(AsConst(lyn), kIidAngelicRobe), 1);
  ApplyItemStatBoost(lyn, 1);
  EXPECT_EQ(lyn->max_hp, lyn_max_hp_before + 7);
  EXPECT_EQ(lyn->items[1], 0u);  // 1-use stat booster consumed

  const int8_t b_hp_before = brigand_b->max_hp;
  UnitApplyBonusLevels(brigand_b, 3);
  EXPECT_GE(brigand_b->max_hp, b_hp_before);

  ASSERT_TRUE(CanUnitCarry(AsConst(kent), AsConst(lyn)));
  UnitRescue(kent, lyn);
  EXPECT_EQ(kent->rescue, static_cast<uint8_t>(lyn->id));
  EXPECT_NE(lyn->flags & kUnitFlagRescued, 0u);

  UnitDropRescue(kent, 1, 4);
  EXPECT_EQ(kent->rescue, 0u);
  EXPECT_EQ(lyn->flags & kUnitFlagRescued, 0u);
  EXPECT_EQ(lyn->x, 1);
  EXPECT_EQ(lyn->y, 4);
  SyncUnitMap();

  // --- 3. Top-Level Enemy AI (Recovery, Threat Auto-Equip, Nearby, Move) ---
  SetPhaseFaction(kFactionRed);
  brigand_a->hp = 5;
  brigand_b->hp = brigand_b->max_hp;
  AiUpdateFactionRecovery();
  EXPECT_NE(brigand_a->ai_flags & kAiFlagRecovery, 0);
  EXPECT_EQ(brigand_b->ai_flags & kAiFlagRecovery, 0);

  // Equip Brigand B with Iron Axe (Mt 8, slot 0) and Steel Axe (Mt 11, slot 1)
  // within melee threat of Kent; AiEquipBestItem should swap Steel Axe to 0.
  brigand_b->items[0] = MakeItem(kIidIronAxe, 45);
  brigand_b->items[1] = MakeItem(kIidSteelAxe, 30);
  PlaceUnit(brigand_b, 3, 4);
  SetActiveAiUnit(brigand_b);

  GbaPtr<AiDecision> dec(kAiDecisionAddr);
  AiClearDecision();
  dec->x_move = 2;
  dec->y_move = 4;
  dec->action_id = kAiActionWait;

  EXPECT_TRUE(AiCanEquipBestItem());
  AiEquipBestItem();
  EXPECT_EQ(GetItemIid(brigand_b->items[0]), kIidSteelAxe);
  EXPECT_GE(AiCountEnemiesNearby(2, 4), 1);

  AiClearDecision();
  AiTryMoveRandomly();
  EXPECT_EQ(dec->action_performed, 1);

  const int red_before = CountUnitsByFaction(kFactionRed);
  KillUnit(brigand_a);
  EXPECT_EQ(CountUnitsByFaction(kFactionRed), red_before - 1);
}

TEST_F(SkirmishIntegrationTest, StatusEffectsAndAutoEndTurnFlow) {
  RandInit(kRngSeedStatusFlow);
  GbaPtr<Unit> lyn = Lyn();
  GbaPtr<Unit> kent = Kent();

  // Register a magic tome (Luna) and Heal staff to verify Silence blocks both
  // magic and staff usage while physical weapons remain usable.
  RegisterItem({.id = kIidLuna,
                .kind = ItemKind::kDark,
                .attributes = kItemAttrWeapon | kItemAttrMagic,
                .max_uses = 30,
                .might = 0,
                .hit = 95,
                .weight = 12,
                .encoded_range = kEncodedRange1To2,
                .required_wexp = kWexpERank,
                .wexp_gain = 1});
  RegisterItem({.id = kIidHealStaff,
                .kind = ItemKind::kStaff,
                .attributes = kItemAttrStaff,
                .max_uses = 30,
                .encoded_range = kEncodedRange1To1,
                .required_wexp = kWexpERank});
  SetWeaponExp(lyn, ItemKind::kDark, kWexpERank);
  SetWeaponExp(lyn, ItemKind::kStaff, kWexpERank);
  const uint16_t luna = MakeItem(kIidLuna, 30);
  const uint16_t heal = MakeItem(kIidHealStaff, 30);

  // 1. Both Blue units start able to act; Lyn can use Dark magic and staves.
  EXPECT_EQ(GetPhaseAbleUnitCount(kFactionBlue), 2);
  EXPECT_TRUE(CanUnitUseWeapon(AsConst(lyn), luna));
  EXPECT_TRUE(CanUnitUseStaff(AsConst(lyn), heal));

  // 2. Silencing Lyn blocks magic & staff usage, while she remains able to
  //    move/act with physical weapons.
  SetUnitStatus(lyn, static_cast<int>(UnitStatus::kSilenced));
  EXPECT_FALSE(CanUnitUseWeapon(AsConst(lyn), luna));
  EXPECT_FALSE(CanUnitUseStaff(AsConst(lyn), heal));
  EXPECT_TRUE(CanUnitUseWeapon(AsConst(lyn), lyn->items[0]));
  EXPECT_EQ(GetPhaseAbleUnitCount(kFactionBlue), 2);

  // 3. Putting Lyn to Sleep and Berserking Kent leaves 0 able Blue units.
  SetUnitStatus(lyn, static_cast<int>(UnitStatus::kSleep));
  EXPECT_EQ(GetPhaseAbleUnitCount(kFactionBlue), 1);
  SetUnitStatus(kent, static_cast<int>(UnitStatus::kBerserk));
  EXPECT_EQ(GetPhaseAbleUnitCount(kFactionBlue), 0);

  // 4. Build a minimal ROM ProcScr containing label 3 so
  //    `PlayerPhaseHandleAutoEnd` jumps `proc_script_pc` to label 3 when
  //    auto-end-turn is enabled and no active faction units can act.
  InitProcs();
  const ProcScr script[3] = {
      {.cmd = static_cast<int16_t>(ProcCmd::kBlock), .imm = 0, .ptr = {}},
      {.cmd = static_cast<int16_t>(ProcCmd::kLabel), .imm = 3, .ptr = {}},
      {.cmd = static_cast<int16_t>(ProcCmd::kEnd), .imm = 0, .ptr = {}},
  };
  WriteRom(kSyntheticProcScrAddr, script);
  GbaPtr<Proc> phase_proc =
      SpawnProc(GbaPtr<const ProcScr>(kSyntheticProcScrAddr), kProcTree3);
  ASSERT_TRUE(static_cast<bool>(phase_proc));
  const uint32_t pc_before = phase_proc->proc_script_pc.addr;

  // With auto-end disabled, PlayerPhaseHandleAutoEnd is a no-op.
  SetAutoEndTurn(false);
  PlayerPhaseHandleAutoEnd(phase_proc);
  EXPECT_EQ(phase_proc->proc_script_pc.addr, pc_before);

  // With auto-end enabled and 0 able Blue units, it jumps past label 3.
  SetAutoEndTurn(true);
  PlayerPhaseHandleAutoEnd(phase_proc);
  EXPECT_NE(phase_proc->proc_script_pc.addr, pc_before);
}

TEST_F(SkirmishIntegrationTest, BallistaMountCombatAmmoAndDismount) {
  RandInit(kRngSeedBallista);

  RegisterItem({.id = kIidBallista,
                .kind = ItemKind::kBallista,
                .attributes = kItemAttrWeapon | kItemAttrUncounterable,
                .max_uses = 2,
                .might = 10,
                .hit = 85,
                .weight = 20,
                .encoded_range = kEncodedRange3To10,
                .required_wexp = kWexpERank,
                .wexp_gain = 1});

  GbaPtr<Unit> lyn = Lyn();
  GbaPtr<Unit> brigand_b = BrigandB();

  // 1. Spawn a 2-ammo ballista at Lyn's tile (1, 3).
  GbaPtr<Trap> ballista = AddBallista(1, 3, kIidBallista);
  ASSERT_TRUE(static_cast<bool>(ballista));
  EXPECT_TRUE(IsBallistaTrap(AsConst(ballista)));
  EXPECT_EQ(GetBallistaAt(1, 3).addr, ballista.addr);
  EXPECT_EQ(GetItemUses(GetBallistaItemAtPos(1, 3)), 2);

  // 2. Lyn mounts the ballista; `kUnitFlagInBallista` is set and she fires at
  //    Brigand B (distance 5, within 3-10 range).
  UnitRideBallista(lyn);
  EXPECT_NE(lyn->flags & kUnitFlagInBallista, 0u);
  EXPECT_TRUE(CanItemReachDistance(GetBallistaItemFromTrap(AsConst(ballista)),
                                   /*distance=*/5));

  // 3. Fire shot #1 with `kBattleConfigBallista` and sync ammo via
  //    `BattleApplyBallistaUpdates()` -> 1 use remaining.
  lyn->items[0] = static_cast<uint16_t>(GetBallistaItemAtPos(1, 3));
  RunCombatExchange(lyn, brigand_b, kBattleConfigReal | kBattleConfigBallista);
  BattleApplyBallistaUpdates();
  EXPECT_EQ(GetItemUses(GetBallistaItemAtPos(1, 3)), 1);

  // 4. Deplete the final shot -> `GetBallistaAt` returns null (out of ammo),
  //    while `GetBallistaItemAtIgnoreUses` still identifies the empty trap.
  brigand_b->hp = brigand_b->max_hp;
  brigand_b->flags &= ~kUnitFlagDead;
  lyn->items[0] = static_cast<uint16_t>(GetBallistaItemAtPos(1, 3));
  RunCombatExchange(lyn, brigand_b, kBattleConfigReal | kBattleConfigBallista);
  BattleApplyBallistaUpdates();
  EXPECT_FALSE(static_cast<bool>(GetBallistaAt(1, 3)));
  EXPECT_EQ(GetItemIid(GetBallistaItemAtIgnoreUses(1, 3)), kIidBallista);

  // 5. Dismounting clears `kUnitFlagInBallista` and keeps trap coordinates
  //    synced with Lyn's tile.
  TryRemoveUnitFromBallista(lyn);
  EXPECT_EQ(lyn->flags & kUnitFlagInBallista, 0u);
  EXPECT_EQ(ballista->x, lyn->x);
  EXPECT_EQ(ballista->y, lyn->y);
}

TEST_F(SkirmishIntegrationTest, ChokepointPathfindingAndZoneOfControlBlocking) {
  RandInit(kRngSeedChokepoint);
  InitEnemyAiTurnRomTables();

  // Vertical wall at x=3 with a single 1-tile chokepoint at (3, 3):
  //   y=3: . L . K . B . .   (Kent holds choke; 1-HP Lyn behind him; B right)
  LoadTerrainFromAscii({
      "...#....",  // y=0
      "...#....",  // y=1
      "...#....",  // y=2
      "........",  // y=3: 1-tile chokepoint at (3,3)
      "...#....",  // y=4
      "...#....",  // y=5
      "...#....",  // y=6
      "...#....",  // y=7
  });

  GbaPtr<Unit> lyn = Lyn();
  GbaPtr<Unit> kent = Kent();
  GbaPtr<Unit> brigand_b = BrigandB();

  lyn->hp = 1;  // Tempting 1-HP target behind the chokepoint
  PlaceUnit(lyn, 1, 3);
  PlaceUnit(kent, 3, 3);
  PlaceUnit(brigand_b, 5, 3);
  PlaceUnit(BrigandA(), 7, 7);

  // 1. Friendly pass-through: Blue Lyn can flood THROUGH Blue Kent at (3, 3)
  //    to reach (4, 3) on the far side of the wall.
  MapFloodUnit(AsConst(lyn));
  EXPECT_LE(MovementMap()[3][4], 5u);

  // 2. Zone-of-control blocking: Red Brigand B cannot pass through Blue Kent
  //    at (3, 3), leaving (2, 3) and Lyn's tile (1, 3) completely unreachable.
  MapFloodUnit(AsConst(brigand_b));
  EXPECT_LE(MovementMap()[3][4], 5u);
  EXPECT_EQ(MovementMap()[3][2], kMoveCostUnreachable);
  EXPECT_EQ(MovementMap()[3][1], kMoveCostUnreachable);

  // 3. Enemy AI is forced to attack Kent at the chokepoint rather than
  //    walking through him to finish off 1-HP Lyn.
  SetPhaseFaction(kFactionRed);
  brigand_b->ai_a = 0u;
  brigand_b->ai_a_pc = 0u;
  PrepareAiTurn(brigand_b);
  CpDecideMain();
  ExpectAiDecision({.action_id = kAiActionAttack, .target = kent});
}

}  // namespace
}  // namespace fe7::e2e
