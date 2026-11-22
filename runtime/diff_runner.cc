// Live retail-ROM differential verification CLI (`fe7_diff`).
//
// Loads a user-supplied retail FE7 US ROM, optionally loads recorded `.fe7t`
// traces, and runs DiffFunction() against every decompiled native C++20
// function to verify bit-exact r0 and EWRAM/IWRAM parity with the original
// ARM7TDMI Thumb machine code.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "absl/flags/flag.h"
#include "absl/flags/parse.h"
#include "absl/flags/usage.h"
#include "game/action/action.h"
#include "game/ai/ai.h"
#include "game/ai/ai_decide.h"
#include "game/battle.h"
#include "game/battle/battle.h"
#include "game/battle/battle_exp.h"
#include "game/battle/battle_sim.h"
#include "game/engine/clock.h"
#include "game/engine/proc.h"
#include "game/engine/rng.h"
#include "game/item/item.h"
#include "game/map/map.h"
#include "game/map/trap.h"
#include "game/phase/phase.h"
#include "game/register_all.h"
#include "game/support/support.h"
#include "game/unit.h"
#include "game/unit/unit.h"
#include "runtime/diff_harness.h"
#include "runtime/dispatch.h"
#include "runtime/gba_hal.h"
#include "runtime/gba_ptr.h"
#include "runtime/rom_loader.h"
#include "runtime/trace_recorder.h"

ABSL_FLAG(std::string, rom, "", "Path to a retail FE7 US (AE7E) GBA ROM.");
ABSL_FLAG(std::string, traces, "", "Optional path to a .fe7t trace file.");
ABSL_FLAG(int, fuzz_iters, 128,
          "Number of deterministic fuzzed inputs per scalar function.");

namespace {

enum class FuzzMode {
  kScalarRegs,
  kPtrU16x3InR0,
  kFormatTimePtrs,
  kUnitPtrInR0,
  kUnitCopyPtrs,
  kUnitAndItem,
  kUnitAndSlot,
  kItemAndUnit,
  kValidItemInR0,
  kBattlePair,
  kBattleSim,
  kBattleExp,
  kMapState,
  kPhaseState,
  kSupportState,
  kAiState,
  kTrapState,
  kActionState,
  kAiDecideState,
  kTracesOnly,
};

struct VerifiedEntry {
  const char* name;
  uint32_t gba_addr;
  bool check_return_value;
  FuzzMode fuzz_mode;
};

constexpr VerifiedEntry kVerifiedFunctions[] = {
    // Wave 1: RNG (0x08000BE0..0x08000EDC)
    {"NextRN", fe7::kAddrNextRn, true, FuzzMode::kScalarRegs},
    {"RandInit", fe7::kAddrRandInit, false, FuzzMode::kScalarRegs},
    {"RandSetSt", fe7::kAddrRandSetSt, false, FuzzMode::kPtrU16x3InR0},
    {"RandGetSt", fe7::kAddrRandGetSt, false, FuzzMode::kPtrU16x3InR0},
    {"RandNext100", fe7::kAddrRandNext100, true, FuzzMode::kScalarRegs},
    {"RandNext", fe7::kAddrRandNext, true, FuzzMode::kScalarRegs},
    {"RandRoll", fe7::kAddrRandRoll, true, FuzzMode::kScalarRegs},
    {"RandRoll2Rn", fe7::kAddrRandRoll2Rn, true, FuzzMode::kScalarRegs},
    {"RandInitB", fe7::kAddrRandInitB, false, FuzzMode::kScalarRegs},
    {"RandNextB", fe7::kAddrRandNextB, true, FuzzMode::kScalarRegs},
    // Wave 1: Clock (0x08000F14..0x08000F80)
    {"GetGameTime", fe7::kAddrGetGameTime, true, FuzzMode::kScalarRegs},
    {"SetGameTime", fe7::kAddrSetGameTime, false, FuzzMode::kScalarRegs},
    {"IncGameTime", fe7::kAddrIncGameTime, false, FuzzMode::kScalarRegs},
    {"FormatTime", fe7::kAddrFormatTime, true, FuzzMode::kFormatTimePtrs},
    // Wave 1: Proc (0x08004420..0x08004B84)
    {"InitProcs", fe7::kAddrInitProcs, false, FuzzMode::kScalarRegs},
    {"SpawnProc", fe7::kAddrSpawnProc, true, FuzzMode::kTracesOnly},
    {"SpawnProcLocking", fe7::kAddrSpawnProcLocking, true,
     FuzzMode::kTracesOnly},
    {"ProcEnd", fe7::kAddrProcEnd, false, FuzzMode::kTracesOnly},
    {"ProcRun", fe7::kAddrProcRun, false, FuzzMode::kTracesOnly},
    {"ProcBreak", fe7::kAddrProcBreak, false, FuzzMode::kTracesOnly},
    {"ProcFind", fe7::kAddrProcFind, true, FuzzMode::kScalarRegs},
    {"ProcFindNonBlocked", fe7::kAddrProcFindNonBlocked, true,
     FuzzMode::kScalarRegs},
    {"ProcFindMarked", fe7::kAddrProcFindMarked, true, FuzzMode::kScalarRegs},
    {"ProcGoto", fe7::kAddrProcGoto, false, FuzzMode::kTracesOnly},
    {"ProcGotoScript", fe7::kAddrProcGotoScript, false, FuzzMode::kTracesOnly},
    {"ProcMark", fe7::kAddrProcMark, false, FuzzMode::kTracesOnly},
    {"ProcSetEndFunc", fe7::kAddrProcSetEndFunc, false, FuzzMode::kTracesOnly},
    {"ProcSleepRepeat", fe7::kAddrProcSleepRepeat, false,
     FuzzMode::kTracesOnly},
    {"ProcRunScript", fe7::kAddrProcRunScript, false, FuzzMode::kTracesOnly},
    // Wave 2: Unit & Stat Getters (0x08017508..0x08018D38)
    {"ClearUnit", fe7::kAddrClearUnit, false, FuzzMode::kUnitPtrInR0},
    {"CopyUnit", fe7::kAddrCopyUnit, false, FuzzMode::kUnitCopyPtrs},
    {"GetUnitVision", fe7::kAddrGetUnitVision, true, FuzzMode::kUnitPtrInR0},
    {"SetUnitStatus", fe7::kAddrSetUnitStatus, false, FuzzMode::kUnitPtrInR0},
    {"SetUnitStatusExt", fe7::kAddrSetUnitStatusExt, false,
     FuzzMode::kUnitPtrInR0},
    {"GetUnitItemCount", fe7::kAddrGetUnitItemCount, true,
     FuzzMode::kUnitPtrInR0},
    {"GetUnitByPid", fe7::kAddrGetUnitByPid, true, FuzzMode::kScalarRegs},
    {"GetUnitAid", fe7::kAddrGetUnitAid, true, FuzzMode::kUnitPtrInR0},
    {"GetUnitMagRange", fe7::kAddrGetUnitMagRange, true,
     FuzzMode::kUnitPtrInR0},
    {"UnitKnowsMagic", fe7::kAddrUnitKnowsMagic, true, FuzzMode::kUnitPtrInR0},
    {"GetUnitCurrentHp", fe7::kAddrGetUnitCurrentHp, true,
     FuzzMode::kUnitPtrInR0},
    {"GetUnitMaxHp", fe7::kAddrGetUnitMaxHp, true, FuzzMode::kUnitPtrInR0},
    {"GetUnitPower", fe7::kAddrGetUnitPower, true, FuzzMode::kUnitPtrInR0},
    {"GetUnitSkill", fe7::kAddrGetUnitSkill, true, FuzzMode::kUnitPtrInR0},
    {"GetUnitSpeed", fe7::kAddrGetUnitSpeed, true, FuzzMode::kUnitPtrInR0},
    {"GetUnitDefense", fe7::kAddrGetUnitDefense, true, FuzzMode::kUnitPtrInR0},
    {"GetUnitResistance", fe7::kAddrGetUnitResistance, true,
     FuzzMode::kUnitPtrInR0},
    {"GetUnitLuck", fe7::kAddrGetUnitLuck, true, FuzzMode::kUnitPtrInR0},
    {"GetUnitFid", fe7::kAddrGetUnitFid, true, FuzzMode::kUnitPtrInR0},
    {"GetUnitMiniFid", fe7::kAddrGetUnitMiniFid, true, FuzzMode::kUnitPtrInR0},
    {"SetUnitHp", fe7::kAddrSetUnitHp, false, FuzzMode::kUnitPtrInR0},
    {"AddUnitHp", fe7::kAddrAddUnitHp, false, FuzzMode::kUnitPtrInR0},
    {"GetUnit", fe7::kAddrGetUnit, true, FuzzMode::kScalarRegs},
    {"GetJobInfo", fe7::kAddrGetJobInfo, true, FuzzMode::kScalarRegs},
    {"GetPersonInfo", fe7::kAddrGetPersonInfo, true, FuzzMode::kScalarRegs},
    // Wave 2: Items & Weapon Stats (0x08016040..0x080174AC)
    {"GetItemHpBonus", fe7::kAddrItemHpBonus, true, FuzzMode::kValidItemInR0},
    {"GetItemPowBonus", fe7::kAddrItemPowBonus, true, FuzzMode::kValidItemInR0},
    {"GetItemSklBonus", fe7::kAddrItemSklBonus, true, FuzzMode::kValidItemInR0},
    {"GetItemSpdBonus", fe7::kAddrItemSpdBonus, true, FuzzMode::kValidItemInR0},
    {"GetItemDefBonus", fe7::kAddrItemDefBonus, true, FuzzMode::kValidItemInR0},
    {"GetItemResBonus", fe7::kAddrItemResBonus, true, FuzzMode::kValidItemInR0},
    {"GetItemLckBonus", fe7::kAddrItemLckBonus, true, FuzzMode::kValidItemInR0},
    {"CreateItem", fe7::kAddrCreateItem, true, FuzzMode::kScalarRegs},
    {"CanUnitUseWeapon", fe7::kAddrCanUnitUseWeapon, true,
     FuzzMode::kUnitAndItem},
    {"CanUnitUseWeaponNow", fe7::kAddrCanUnitUseWeaponNow, true,
     FuzzMode::kUnitAndItem},
    {"CanUnitUseStaff", fe7::kAddrCanUnitUseStaff, true,
     FuzzMode::kUnitAndItem},
    {"CanUnitUseStaffNow", fe7::kAddrCanUnitUseStaffNow, true,
     FuzzMode::kUnitAndItem},
    {"GetItemAfterUse", fe7::kAddrGetItemAfterUse, true, FuzzMode::kScalarRegs},
    {"GetUnitEquippedWeapon", fe7::kAddrUnitEquippedWeapon, true,
     FuzzMode::kUnitPtrInR0},
    {"GetUnitEquippedWeaponSlot", fe7::kAddrUnitEquippedWeaponSlot, true,
     FuzzMode::kUnitPtrInR0},
    {"CanItemReachDistance", fe7::kAddrCanItemReachDistance, true,
     FuzzMode::kScalarRegs},
    {"UnitEquipItemSlot", fe7::kAddrUnitEquipItemSlot, false,
     FuzzMode::kUnitAndSlot},
    {"IsItemEffectiveAgainst", fe7::kAddrIsItemEffectiveAgainst, true,
     FuzzMode::kItemAndUnit},
    {"GetWeaponLevelFromExp", fe7::kAddrGetWeaponLevelFromExp, true,
     FuzzMode::kScalarRegs},
    {"GetItemIid", fe7::kAddrGetItemIid, true, FuzzMode::kScalarRegs},
    {"GetItemDescId", fe7::kAddrGetItemDescId, true, FuzzMode::kScalarRegs},
    {"GetItemUseDescId", fe7::kAddrGetItemUseDescId, true,
     FuzzMode::kScalarRegs},
    {"GetItemKind", fe7::kAddrGetItemKind, true, FuzzMode::kScalarRegs},
    {"GetItemAttributes", fe7::kAddrGetItemAttributes, true,
     FuzzMode::kScalarRegs},
    {"GetItemUses", fe7::kAddrGetItemUses, true, FuzzMode::kScalarRegs},
    {"GetItemMaxUses", fe7::kAddrGetItemMaxUses, true, FuzzMode::kScalarRegs},
    {"GetItemMight", fe7::kAddrGetItemMight, true, FuzzMode::kScalarRegs},
    {"GetItemHit", fe7::kAddrGetItemHit, true, FuzzMode::kScalarRegs},
    {"GetItemWeight", fe7::kAddrGetItemWeight, true, FuzzMode::kScalarRegs},
    {"GetItemCrit", fe7::kAddrGetItemCrit, true, FuzzMode::kScalarRegs},
    {"GetItemValue", fe7::kAddrGetItemValue, true, FuzzMode::kScalarRegs},
    {"GetItemMinRange", fe7::kAddrGetItemMinRange, true, FuzzMode::kScalarRegs},
    {"GetItemMaxRange", fe7::kAddrGetItemMaxRange, true, FuzzMode::kScalarRegs},
    {"GetItemEncodedRange", fe7::kAddrGetItemEncodedRange, true,
     FuzzMode::kScalarRegs},
    {"GetItemRequiredExp", fe7::kAddrGetItemRequiredExp, true,
     FuzzMode::kScalarRegs},
    {"GetItemEffectiveness", fe7::kAddrGetItemEffectiveness, true,
     FuzzMode::kScalarRegs},
    {"GetItemBonuses", fe7::kAddrGetItemBonuses, true, FuzzMode::kScalarRegs},
    {"GetItemIcon", fe7::kAddrGetItemIcon, true, FuzzMode::kScalarRegs},
    {"GetItemWeaponEffect", fe7::kAddrGetItemWeaponEffect, true,
     FuzzMode::kScalarRegs},
    {"GetItemEffect", fe7::kAddrGetItemEffect, true, FuzzMode::kScalarRegs},
    {"GetItemCostPerUse", fe7::kAddrGetItemCostPerUse, true,
     FuzzMode::kScalarRegs},
    {"GetItemMaxValue", fe7::kAddrGetItemMaxValue, true, FuzzMode::kScalarRegs},
    {"GetItemWeaponExp", fe7::kAddrGetItemWeaponExp, true,
     FuzzMode::kScalarRegs},
    {"GetIInfo", fe7::kAddrGetIInfo, true, FuzzMode::kScalarRegs},
    // Wave 2: Battle Formulas (0x080289B0..0x08028EEC, 0x0802A120..0x0802A170)
    {"ComputeBattleUnitStats", fe7::kAddrComputeBattleUnitStats, false,
     FuzzMode::kBattlePair},
    {"ComputeBattleUnitEffectiveStats",
     fe7::kAddrComputeBattleUnitEffectiveStats, false, FuzzMode::kBattlePair},
    {"ComputeBattleUnitDefense", fe7::kAddrComputeBattleUnitDefense, false,
     FuzzMode::kBattlePair},
    {"ComputeBattleUnitBaseDefense", fe7::kAddrComputeBattleUnitBaseDefense,
     false, FuzzMode::kBattlePair},
    {"ComputeBattleUnitAttack", fe7::kAddrComputeBattleUnitAttack, false,
     FuzzMode::kBattlePair},
    {"ComputeBattleUnitSpeed", fe7::kAddrComputeBattleUnitSpeed, false,
     FuzzMode::kBattlePair},
    {"ComputeBattleUnitHitRate", fe7::kAddrComputeBattleUnitHitRate, false,
     FuzzMode::kBattlePair},
    {"ComputeBattleUnitAvoidRate", fe7::kAddrComputeBattleUnitAvoidRate, false,
     FuzzMode::kBattlePair},
    {"ComputeBattleUnitCritRate", fe7::kAddrComputeBattleUnitCritRate, false,
     FuzzMode::kBattlePair},
    {"ComputeBattleUnitDodgeRate", fe7::kAddrComputeBattleUnitDodgeRate, false,
     FuzzMode::kBattlePair},
    {"ComputeBattleUnitEffectiveHitRate",
     fe7::kAddrComputeBattleUnitEffectiveHitRate, false, FuzzMode::kBattlePair},
    {"ComputeBattleUnitEffectiveCritRate",
     fe7::kAddrComputeBattleUnitEffectiveCritRate, false,
     FuzzMode::kBattlePair},
    {"ComputeBattleUnitSilencerRate", fe7::kAddrComputeBattleUnitSilencerRate,
     false, FuzzMode::kBattlePair},
    {"ComputeBattleUnitWeaponRankBonuses",
     fe7::kAddrComputeBattleUnitWeaponRankBonuses, false,
     FuzzMode::kBattlePair},
    {"ComputeBattleUnitStatusBonuses", fe7::kAddrComputeBattleUnitStatusBonuses,
     false, FuzzMode::kBattlePair},
    {"ComputeBattleUnitSpecialWeaponStats",
     fe7::kAddrComputeBattleUnitSpecialWeaponStats, false,
     FuzzMode::kBattlePair},
    {"BattleApplyReaverEffect", fe7::kAddrBattleApplyReaverEffect, false,
     FuzzMode::kBattlePair},
    {"BattleApplyWeaponTriangleEffect",
     fe7::kAddrBattleApplyWeaponTriangleEffect, false, FuzzMode::kBattlePair},
    // Wave 2: Map & Pathfinding (0x08004408, 0x0801906C..0x0801B1DC)
    {"MapInit", fe7::kAddrMapInit, false, FuzzMode::kMapState},
    {"MapFill", fe7::kAddrMapFill, false, FuzzMode::kMapState},
    {"MapFillEdges", fe7::kAddrMapFillEdges, false, FuzzMode::kMapState},
    {"SetWorkingMap", fe7::kAddrSetWorkingMap, false, FuzzMode::kMapState},
    {"SetWorkingMoveTable", fe7::kAddrSetWorkingMoveTable, false,
     FuzzMode::kMapState},
    {"GetWorkingMoveTable", fe7::kAddrGetWorkingMoveTable, true,
     FuzzMode::kScalarRegs},
    {"MapFloodCoreStep", fe7::kAddrMapFloodCoreStep, false,
     FuzzMode::kMapState},
    {"MapFloodCoreRam", fe7::kAddrMapFloodCoreRam, false, FuzzMode::kMapState},
    {"BeginMapFlood", fe7::kAddrBeginMapFlood, false, FuzzMode::kMapState},
    {"MapFloodUnit", fe7::kAddrMapFloodUnit, false, FuzzMode::kMapState},
    {"MapFloodUnitMovement", fe7::kAddrMapFloodUnitMovement, false,
     FuzzMode::kMapState},
    {"MapFloodUnitExtended", fe7::kAddrMapFloodUnitExtended, false,
     FuzzMode::kMapState},
    {"MapFloodRangeUnitless", fe7::kAddrMapFloodRangeUnitless, false,
     FuzzMode::kMapState},
    {"MapFloodExtended", fe7::kAddrMapFloodExtended, false,
     FuzzMode::kMapState},
    {"MapFloodOnWorkingMap", fe7::kAddrMapFloodOnWorkingMap, false,
     FuzzMode::kMapState},
    {"MapAddInRange", fe7::kAddrMapAddInRange, false, FuzzMode::kMapState},
    {"MapSetInRange", fe7::kAddrMapSetInRange, false, FuzzMode::kMapState},
    // Wave 3: Battle Simulation (0x0802857C..0x0802ABA0)
    {"BattleRoll1RN", fe7::kAddrBattleRoll1RN, true, FuzzMode::kBattleSim},
    {"BattleRoll2RN", fe7::kAddrBattleRoll2RN, true, FuzzMode::kBattleSim},
    {"InitBattleUnit", fe7::kAddrInitBattleUnit, false, FuzzMode::kBattleSim},
    {"InitBattleUnitWithoutBonuses", fe7::kAddrInitBattleUnitWithoutBonuses,
     false, FuzzMode::kBattleSim},
    {"ClearBattleHits", fe7::kAddrClearBattleHits, false, FuzzMode::kBattleSim},
    {"BattleUnwind", fe7::kAddrBattleUnwind, false, FuzzMode::kBattleSim},
    {"BattleGetBattleUnitOrder", fe7::kAddrBattleGetBattleUnitOrder, false,
     FuzzMode::kBattleSim},
    {"BattleGetFollowUpOrder", fe7::kAddrBattleGetFollowUpOrder, true,
     FuzzMode::kBattleSim},
    {"BattleGenerateRoundHits", fe7::kAddrBattleGenerateRoundHits, true,
     FuzzMode::kBattleSim},
    {"BattleGetEffectiveHitCount", fe7::kAddrBattleGetEffectiveHitCount, true,
     FuzzMode::kBattleSim},
    {"BattleCheckBrave", fe7::kAddrBattleCheckBrave, true,
     FuzzMode::kBattleSim},
    {"BattleCheckTriangleAttackOrder", fe7::kAddrBattleCheckTriangleAttackOrder,
     true, FuzzMode::kBattleSim},
    {"BattleUpdateBattleStats", fe7::kAddrBattleUpdateBattleStats, false,
     FuzzMode::kBattleSim},
    {"BattleGenerateHitAttributes", fe7::kAddrBattleGenerateHitAttributes,
     false, FuzzMode::kBattleSim},
    {"BattleGenerateHitTriangleAttack",
     fe7::kAddrBattleGenerateHitTriangleAttack, false, FuzzMode::kBattleSim},
    {"BattleGenerateHitEffects", fe7::kAddrBattleGenerateHitEffects, false,
     FuzzMode::kBattleSim},
    {"BattleGenerateHit", fe7::kAddrBattleGenerateHit, true,
     FuzzMode::kBattleSim},
    {"BattleUnitTargetSetEquippedWeapon",
     fe7::kAddrBattleUnitTargetSetEquippedWeapon, false, FuzzMode::kBattleSim},
    {"BattleUnitTargetCheckHasRealWeapon",
     fe7::kAddrBattleUnitTargetCheckHasRealWeapon, false, FuzzMode::kBattleSim},
    {"BattleInitTargetCanCounter", fe7::kAddrBattleInitTargetCanCounter, false,
     FuzzMode::kBattleSim},
    {"BattleHitAdvance", fe7::kAddrBattleHitAdvance, false,
     FuzzMode::kBattleSim},
    {"BattleHitTerminate", fe7::kAddrBattleHitTerminate, false,
     FuzzMode::kBattleSim},
    // Wave 3: Turn Flow & Phase Management (0x08015308..0x080868B8)
    {"LockGame", fe7::kAddrLockGame, false, FuzzMode::kScalarRegs},
    {"ReleaseGame", fe7::kAddrReleaseGame, false, FuzzMode::kScalarRegs},
    {"GetGameLock", fe7::kAddrGetGameLock, true, FuzzMode::kScalarRegs},
    {"SwitchPhases", fe7::kAddrSwitchPhases, false, FuzzMode::kPhaseState},
    {"BmMainChangePhase", fe7::kAddrBmMainChangePhase, true,
     FuzzMode::kTracesOnly},
    {"BmMainCheckVictory", fe7::kAddrBmMainCheckVictory, true,
     FuzzMode::kTracesOnly},
    {"BmMainStartPhase", fe7::kAddrBmMainStartPhase, false,
     FuzzMode::kTracesOnly},
    {"BmMainResumePlayerPhase", fe7::kAddrBmMainResumePlayerPhase, false,
     FuzzMode::kTracesOnly},
    {"BmMainUpdateTraps", fe7::kAddrBmMainUpdateTraps, true,
     FuzzMode::kTracesOnly},
    {"RefreshFactionUnits", fe7::kAddrRefreshFactionUnits, false,
     FuzzMode::kPhaseState},
    {"ClearAllUnitsBit8", fe7::kAddrClearAllUnitsBit8, false,
     FuzzMode::kPhaseState},
    {"PlayerPhaseHandleAutoEnd", fe7::kAddrPlayerPhaseHandleAutoEnd, false,
     FuzzMode::kPhaseState},
    {"GetPhaseAbleUnitCount", fe7::kAddrGetPhaseAbleUnitCount, true,
     FuzzMode::kPhaseState},
    {"CountUnitsInState", fe7::kAddrCountUnitsInState, true,
     FuzzMode::kPhaseState},
    {"AreUnitIdsAllied", fe7::kAddrAreUnitIdsAllied, true,
     FuzzMode::kScalarRegs},
    {"IsSameAllegiance", fe7::kAddrIsSameAllegiance, true,
     FuzzMode::kScalarRegs},
    {"GetCurrentPhase", fe7::kAddrGetCurrentPhase, true, FuzzMode::kPhaseState},
    {"GetNonActiveFaction", fe7::kAddrGetNonActiveFaction, true,
     FuzzMode::kPhaseState},
    {"GetGold", fe7::kAddrGetGold, true, FuzzMode::kPhaseState},
    {"SetGold", fe7::kAddrSetGold, false, FuzzMode::kScalarRegs},
    {"AddGold", fe7::kAddrAddGold, false, FuzzMode::kPhaseState},
    {"CountUnitsByFaction", fe7::kAddrCountUnitsByFaction, true,
     FuzzMode::kPhaseState},
    // Wave 3: Post-Battle EXP, Level-Up, WEXP & Updates
    // (0x0802956C..0x0802A05C)
    {"BattleApplyExpGains", fe7::kAddrBattleApplyExpGains, false,
     FuzzMode::kBattleExp},
    {"GetStatIncrease", fe7::kAddrGetStatIncrease, true, FuzzMode::kBattleExp},
    {"GetAutoleveledStatIncrease", fe7::kAddrGetAutoleveledStatIncrease, true,
     FuzzMode::kBattleExp},
    {"CanBattleUnitGainExp", fe7::kAddrCanBattleUnitGainExp, true,
     FuzzMode::kBattleExp},
    {"CheckBattleUnitLevelUp", fe7::kAddrCheckBattleUnitLevelUp, false,
     FuzzMode::kBattleExp},
    {"UnitPromote", fe7::kAddrUnitPromote, false, FuzzMode::kBattleExp},
    {"GenerateBattleUnitStatGainsComparatively",
     fe7::kAddrGenerateBattleUnitStatGainsComparatively, false,
     FuzzMode::kBattleExp},
    {"CheckBattleUnitStatCaps", fe7::kAddrCheckBattleUnitStatCaps, false,
     FuzzMode::kBattleExp},
    {"BattleApplyUnitUpdates", fe7::kAddrBattleApplyUnitUpdates, false,
     FuzzMode::kBattleExp},
    {"BattleGetExpGainStub", fe7::kAddrBattleGetExpGainStub, true,
     FuzzMode::kScalarRegs},
    {"GetBattleUnitUpdatedWeaponExp", fe7::kAddrGetBattleUnitUpdatedWeaponExp,
     true, FuzzMode::kBattleExp},
    {"HasBattleUnitGainedWeaponLevel", fe7::kAddrHasBattleUnitGainedWeaponLevel,
     true, FuzzMode::kBattleExp},
    {"UpdateUnitFromBattle", fe7::kAddrUpdateUnitFromBattle, false,
     FuzzMode::kBattleExp},
    {"UpdateUnitDuringBattle", fe7::kAddrUpdateUnitDuringBattle, false,
     FuzzMode::kBattleExp},
    {"BattleApplyBallistaUpdates", fe7::kAddrBattleApplyBallistaUpdates, false,
     FuzzMode::kBattleExp},
    {"ClearBattleExpScratch", fe7::kAddrClearBattleExpScratch, false,
     FuzzMode::kScalarRegs},
    {"GetUnitExpLevel", fe7::kAddrGetUnitExpLevel, true, FuzzMode::kBattleExp},
    {"GetUnitRoundExp", fe7::kAddrGetUnitRoundExp, true, FuzzMode::kBattleExp},
    {"GetUnitPowerLevel", fe7::kAddrGetUnitPowerLevel, true,
     FuzzMode::kBattleExp},
    {"GetUnitClassKillExpBonus", fe7::kAddrGetUnitClassKillExpBonus, true,
     FuzzMode::kBattleExp},
    {"GetUnitExpMultiplier", fe7::kAddrGetUnitExpMultiplier, true,
     FuzzMode::kBattleExp},
    {"GetUnitKillExpBonus", fe7::kAddrGetUnitKillExpBonus, true,
     FuzzMode::kBattleExp},
    {"GetBattleUnitExpGain", fe7::kAddrGetBattleUnitExpGain, true,
     FuzzMode::kBattleExp},
    {"BattleApplyMiscActionExpGains", fe7::kAddrBattleApplyMiscActionExpGains,
     false, FuzzMode::kBattleExp},
    {"GetBattleUnitStaffExp", fe7::kAddrGetBattleUnitStaffExp, true,
     FuzzMode::kBattleExp},
    {"BattleApplyStealExpGain", fe7::kAddrBattleApplyStealExpGain, false,
     FuzzMode::kBattleExp},
    // Wave 3: Supports (0x08026628..0x08026C08)
    {"GetUnitSupporterCount", fe7::kAddrGetUnitSupporterCount, true,
     FuzzMode::kSupportState},
    {"GetUnitSupporterCharacter", fe7::kAddrGetUnitSupporterCharacter, true,
     FuzzMode::kSupportState},
    {"GetUnitSupporterUnit", fe7::kAddrGetUnitSupporterUnit, true,
     FuzzMode::kSupportState},
    {"GetSupportLevelBySupportIndex", fe7::kAddrGetSupportLevelBySupportIndex,
     true, FuzzMode::kSupportState},
    {"GetUnitTotalSupportLevel", fe7::kAddrGetUnitTotalSupportLevel, true,
     FuzzMode::kSupportState},
    {"UnitGainSupportExp", fe7::kAddrUnitGainSupportExp, false,
     FuzzMode::kSupportState},
    {"UnitGainSupportLevel", fe7::kAddrUnitGainSupportLevel, false,
     FuzzMode::kSupportState},
    {"CanUnitSupportNow", fe7::kAddrCanUnitSupportNow, true,
     FuzzMode::kSupportState},
    {"GetUnitSupporterInitialExp", fe7::kAddrGetUnitSupporterInitialExp, true,
     FuzzMode::kSupportState},
    {"GetUnitSupporterNum", fe7::kAddrGetUnitSupporterNum, true,
     FuzzMode::kSupportState},
    {"ClearUnitSupports", fe7::kAddrClearUnitSupports, false,
     FuzzMode::kSupportState},
    {"ProcessSupportGains", fe7::kAddrSupportProcessGains, false,
     FuzzMode::kSupportState},
    {"GetAffinityBonusEntry", fe7::kAddrGetAffinityBonusEntry, true,
     FuzzMode::kSupportState},
    {"ApplyAffinitySupportBonuses", fe7::kAddrApplyAffinitySupportBonuses,
     false, FuzzMode::kSupportState},
    {"InitSupportBonuses", fe7::kAddrInitSupportBonuses, false,
     FuzzMode::kSupportState},
    {"GetUnitSupportBonuses", fe7::kAddrGetUnitSupportBonuses, true,
     FuzzMode::kSupportState},
    {"GetUnitAffinityIcon", fe7::kAddrGetUnitAffinityIcon, true,
     FuzzMode::kSupportState},
    {"GetCharacterAffinityIcon", fe7::kAddrGetCharacterAffinityIcon, true,
     FuzzMode::kSupportState},
    {"GetSupportLevelUiChar", fe7::kAddrGetSupportLevelUiChar, true,
     FuzzMode::kSupportState},
    {"SetSupportLevelGained", fe7::kAddrSetSupportLevelGained, false,
     FuzzMode::kSupportState},
    {"HasUnitGainedSupportLevel", fe7::kAddrHasUnitGainedSupportLevel, true,
     FuzzMode::kSupportState},
    {"ArePidsAtMaxSupport", fe7::kAddrArePidsAtMaxSupport, true,
     FuzzMode::kSupportState},
    // Wave 3: Enemy AI Decision & Scoring Core (0x08034E30..0x080393BC)
    {"AiClearDecision", fe7::kAddrAiClearDecision, false,
     FuzzMode::kScalarRegs},
    {"AiSetDecision", fe7::kAddrAiSetDecision, false, FuzzMode::kTracesOnly},
    {"AiUpdateDecision", fe7::kAddrAiUpdateDecision, false,
     FuzzMode::kTracesOnly},
    {"AiIsWithinReachByBirdsEyeDistance",
     fe7::kAddrAiIsWithinReachByBirdsEyeDistance, true, FuzzMode::kAiState},
    {"AiCouldReachByBirdsEyeDistance", fe7::kAddrAiCouldReachByBirdsEyeDistance,
     true, FuzzMode::kAiState},
    {"AiIsInShortList", fe7::kAddrAiIsInShortList, true, FuzzMode::kAiState},
    {"AiIsInByteList", fe7::kAddrAiIsInByteList, true, FuzzMode::kAiState},
    {"AiGetJobRank", fe7::kAddrAiGetJobRank, true, FuzzMode::kAiState},
    {"AiUpdateUnitStaffFlags", fe7::kAddrAiUpdateUnitStaffFlags, false,
     FuzzMode::kAiState},
    {"AiUpdateUnitItemFlags", fe7::kAddrAiUpdateUnitItemFlags, false,
     FuzzMode::kAiState},
    {"AiCountNearbyAllies", fe7::kAddrAiCountNearbyAllies, false,
     FuzzMode::kAiState},
    {"UnitInitAiFromInfo", fe7::kAddrUnitInitAiFromInfo, false,
     FuzzMode::kAiState},
    {"AiFindBestPositionInRange", fe7::kAddrAiFindBestPositionInRange, true,
     FuzzMode::kAiState},
    {"AiGetDamageDealtCombatScoreComponent",
     fe7::kAddrAiGetDamageDealtCombatScoreComponent, true, FuzzMode::kAiState},
    {"AiGetOpponentLowHpScoreComponent",
     fe7::kAddrAiGetOpponentLowHpScoreComponent, true, FuzzMode::kAiState},
    {"AiGetFriendZoneCombatScoreComponent",
     fe7::kAddrAiGetFriendZoneCombatScoreComponent, true, FuzzMode::kAiState},
    {"AiGetTargetClassCombatScoreComponent",
     fe7::kAddrAiGetTargetClassCombatScoreComponent, true, FuzzMode::kAiState},
    {"AiGetTurnCombatScoreComponent", fe7::kAddrAiGetTurnCombatScoreComponent,
     true, FuzzMode::kAiState},
    {"AiGetDamageTakenScoreComponent", fe7::kAddrAiGetDamageTakenScoreComponent,
     true, FuzzMode::kAiState},
    {"AiGetDangerScoreComponent", fe7::kAddrAiGetDangerScoreComponent, true,
     FuzzMode::kAiState},
    {"AiGetLowHpScoreComponent", fe7::kAddrAiGetLowHpScoreComponent, true,
     FuzzMode::kAiState},
    {"AiComputeCombatScore", fe7::kAddrAiComputeCombatScore, false,
     FuzzMode::kAiState},
    {"AiGetInRangeCombatPositionScoreComponent",
     fe7::kAddrAiGetInRangeCombatPositionScoreComponent, true,
     FuzzMode::kAiState},
    {"AiGetTerrainCombatPositionScoreComponent",
     fe7::kAddrAiGetTerrainCombatPositionScoreComponent, true,
     FuzzMode::kAiState},
    {"AiGetFriendZonePositionScoreComponent",
     fe7::kAddrAiGetFriendZonePositionScoreComponent, true, FuzzMode::kAiState},
    {"AiRefreshDangerMap", fe7::kAddrAiRefreshDangerMap, false,
     FuzzMode::kAiState},
    // E2E leaf-stub decompilation batch (19 functions).
    {"GetUnitMovementCost", fe7::kAddrGetUnitMovementCost, true,
     FuzzMode::kUnitPtrInR0},
    {"UnitRemoveInvalidItems", fe7::kAddrUnitRemoveInvalidItems, false,
     FuzzMode::kUnitPtrInR0},
    {"UnitCheckStatOverflow", fe7::kAddrUnitCheckStatOverflow, false,
     FuzzMode::kUnitPtrInR0},
    {"HasTacticianAfaBonus", fe7::kAddrHasTacticianAfaBonus, true,
     FuzzMode::kUnitPtrInR0},
    {"ComputeBattleUnitSupportBonuses",
     fe7::kAddrComputeBattleUnitSupportBonuses, false, FuzzMode::kBattlePair},
    {"PidStatsAddScore", fe7::kAddrPidStatsAddScore, false, FuzzMode::kAiState},
    {"PidStatsAddExpGained", fe7::kAddrPidStatsAddExpGained, false,
     FuzzMode::kAiState},
    {"RecordUnitTurnDeployment", fe7::kAddrRecordUnitTurnDeployment, false,
     FuzzMode::kAiState},
    {"AiMakeMoveRangeMapsForUnitAndWeapon",
     fe7::kAddrAiMakeMoveRangeMapsForUnitAndWeapon, false, FuzzMode::kAiState},
    {"AiFillDangerMap", fe7::kAddrAiFillDangerMap, false, FuzzMode::kAiState},
    {"GetChapterInfo", fe7::kAddrGetChapterInfo, true, FuzzMode::kAiState},
    {"GetChapterEventInfo", fe7::kAddrGetChapterEventInfo, true,
     FuzzMode::kAiState},
    {"CheckChapterFlag", fe7::kAddrCheckChapterFlag, true, FuzzMode::kAiState},
    {"CheckPermanentFlag", fe7::kAddrCheckPermanentFlag, true,
     FuzzMode::kAiState},
    {"GetFlag", fe7::kAddrGetFlag, true, FuzzMode::kAiState},
    {"EventListEndHandler", fe7::kAddrEventListEndHandler, true,
     FuzzMode::kScalarRegs},
    {"SearchEventList", fe7::kAddrSearchEventList, true, FuzzMode::kAiState},
    {"CheckTurnEvents", fe7::kAddrCheckTurnEvents, true, FuzzMode::kAiState},
    {"CheckVictoryEvents", fe7::kAddrCheckVictoryEvents, true,
     FuzzMode::kAiState},
    // Tactical Layer 1: Traps, Obstacles, Light Runes & Fog Maps (27 fns).
    {"RefreshUnitsOnMaps", fe7::kAddrRefreshUnitsOnMaps, false,
     FuzzMode::kTrapState},
    {"RefreshTorchlightOnFogMap", fe7::kAddrRefreshTorchlightOnFogMap, false,
     FuzzMode::kTrapState},
    {"RefreshMinesOnHiddenMap", fe7::kAddrRefreshMinesOnHiddenMap, false,
     FuzzMode::kTrapState},
    {"RefreshEntityMaps", fe7::kAddrRefreshEntityMaps, false,
     FuzzMode::kTrapState},
    {"InitTraps", fe7::kAddrInitTraps, false, FuzzMode::kTrapState},
    {"GetTrap", fe7::kAddrGetTrap, true, FuzzMode::kTrapState},
    {"GetTrapAt", fe7::kAddrGetTrapAt, true, FuzzMode::kTrapState},
    {"GetSpecificTrapAt", fe7::kAddrGetSpecificTrapAt, true,
     FuzzMode::kTrapState},
    {"AddTrap", fe7::kAddrAddTrap, true, FuzzMode::kTrapState},
    {"AddDamagingTrap", fe7::kAddrAddDamagingTrap, true, FuzzMode::kTracesOnly},
    {"RemoveTrap", fe7::kAddrRemoveTrap, false, FuzzMode::kTrapState},
    {"AddFireTrap", fe7::kAddrAddFireTrap, true, FuzzMode::kTrapState},
    {"AddGasTrap", fe7::kAddrAddGasTrap, true, FuzzMode::kTracesOnly},
    {"AddArrowTrap", fe7::kAddrAddArrowTrap, true, FuzzMode::kTrapState},
    {"AddTrap6", fe7::kAddrAddTrap6, true, FuzzMode::kTrapState},
    {"AddTrap8", fe7::kAddrAddTrap8, true, FuzzMode::kTrapState},
    {"AddTrap9", fe7::kAddrAddTrap9, true, FuzzMode::kTrapState},
    {"InitMapObstacles", fe7::kAddrInitMapObstacles, false,
     FuzzMode::kTrapState},
    {"ApplyLightRuneTerrain", fe7::kAddrApplyLightRuneTerrain, false,
     FuzzMode::kTrapState},
    {"GetTrapExtraAt", fe7::kAddrGetTrapExtraAt, true, FuzzMode::kTrapState},
    {"AddMapChangeTrap", fe7::kAddrAddMapChangeTrap, false,
     FuzzMode::kTrapState},
    {"RemoveMapChangeTrap", fe7::kAddrRemoveMapChangeTrap, false,
     FuzzMode::kTrapState},
    {"UnitHideIfUnderRoof", fe7::kAddrUnitHideIfUnderRoof, false,
     FuzzMode::kTrapState},
    {"TickDamagingTraps", fe7::kAddrTickDamagingTraps, false,
     FuzzMode::kTrapState},
    {"ResetExpiredDamagingTraps", fe7::kAddrResetExpiredDamagingTraps, false,
     FuzzMode::kTrapState},
    {"AddLightRune", fe7::kAddrAddLightRune, true, FuzzMode::kTrapState},
    {"ApplyLightRuneTerrainAlt", fe7::kAddrApplyLightRuneTerrainAlt, false,
     FuzzMode::kTrapState},
    // Tactical Layer 2: Unit Actions, Rescue/Drop, Autolevel & Boosters (26).
    {"FindUnitItemSlot", fe7::kAddrFindUnitItemSlot, true,
     FuzzMode::kActionState},
    {"UnitHasItem", fe7::kAddrUnitHasItem, true, FuzzMode::kActionState},
    {"UnitInitStats", fe7::kAddrUnitInitStats, false, FuzzMode::kActionState},
    {"UnitApplyAltPinfo", fe7::kAddrUnitApplyAltPinfo, false,
     FuzzMode::kActionState},
    {"UnitInitSupports", fe7::kAddrUnitInitSupports, false,
     FuzzMode::kActionState},
    {"UnitAutolevelWeaponExp", fe7::kAddrUnitAutolevelWeaponExp, false,
     FuzzMode::kActionState},
    {"UnitAutolevelCore", fe7::kAddrUnitAutolevelCore, false,
     FuzzMode::kActionState},
    {"UnitApplyBonusLevels", fe7::kAddrUnitApplyBonusLevels, false,
     FuzzMode::kActionState},
    {"UnitAutolevelPlayer", fe7::kAddrUnitAutolevelPlayer, false,
     FuzzMode::kActionState},
    {"GetUnitByPidInFaction", fe7::kAddrGetUnitByPidInFaction, true,
     FuzzMode::kActionState},
    {"CanUnitCarry", fe7::kAddrCanUnitCarry, true, FuzzMode::kActionState},
    {"UnitRescue", fe7::kAddrUnitRescue, false, FuzzMode::kActionState},
    {"UnitDropRescue", fe7::kAddrUnitDropRescue, false, FuzzMode::kActionState},
    {"UnitGiveRescue", fe7::kAddrUnitGiveRescue, false, FuzzMode::kActionState},
    {"KillUnit", fe7::kAddrKillUnit, false, FuzzMode::kActionState},
    {"UnitSyncMovement", fe7::kAddrUnitSyncMovement, false,
     FuzzMode::kActionState},
    {"UnitBeginAction", fe7::kAddrUnitBeginAction, false,
     FuzzMode::kActionState},
    {"UnitBeginReMoveAction", fe7::kAddrUnitBeginReMoveAction, false,
     FuzzMode::kActionState},
    {"UnitUpdateUsedItem", fe7::kAddrUnitUpdateUsedItem, false,
     FuzzMode::kActionState},
    {"UnitRevealAtPosition", fe7::kAddrUnitRevealAtPosition, false,
     FuzzMode::kActionState},
    {"GetUnitKeyItemSlotForTerrain", fe7::kAddrGetUnitKeyItemSlotForTerrain,
     true, FuzzMode::kActionState},
    {"GetAidIconFromAttributes", fe7::kAddrGetAidIconFromAttributes, true,
     FuzzMode::kScalarRegs},
    {"GetUnitUseFlags", fe7::kAddrGetUnitUseFlags, true,
     FuzzMode::kActionState},
    {"GetEnemyFactionUseFlags", fe7::kAddrGetEnemyFactionUseFlags, true,
     FuzzMode::kActionState},
    {"GetUnitLastItem", fe7::kAddrGetUnitLastItem, true,
     FuzzMode::kActionState},
    {"ApplyItemStatBoost", fe7::kAddrApplyItemStatBoost, true,
     FuzzMode::kActionState},
    // Top-Level Enemy AI (item ranking, recovery, range-counting, dance/play).
    {"AiEquipBestItem", fe7::kAddrAiEquipBestItem, false,
     FuzzMode::kAiDecideState},
    {"AiCompare", fe7::kAddrAiCompare, true, FuzzMode::kAiDecideState},
    {"AiTryFindNearestUnitOfClass", fe7::kAddrAiTryFindNearestUnitOfClass, true,
     FuzzMode::kAiDecideState},
    {"AiTryMoveRandomly", fe7::kAddrAiTryMoveRandomly, false,
     FuzzMode::kAiDecideState},
    {"AiGetMoveCostAt", fe7::kAddrAiGetMoveCostAt, true,
     FuzzMode::kAiDecideState},
    {"AiCountUnitsInRange", fe7::kAddrAiCountUnitsInRange, true,
     FuzzMode::kAiDecideState},
    {"AiCountEnemiesInRange", fe7::kAddrAiCountEnemiesInRange, true,
     FuzzMode::kAiDecideState},
    {"AiCountAlliesInRange", fe7::kAddrAiCountAlliesInRange, true,
     FuzzMode::kAiDecideState},
    {"AiCountUnitsNearby", fe7::kAddrAiCountUnitsNearby, true,
     FuzzMode::kAiDecideState},
    {"AiCountEnemiesNearby", fe7::kAddrAiCountEnemiesNearby, true,
     FuzzMode::kAiDecideState},
    {"AiCountAlliesNearby", fe7::kAddrAiCountAlliesNearby, true,
     FuzzMode::kAiDecideState},
    {"AiIsWithinDangerThreshold", fe7::kAddrAiIsWithinDangerThreshold, true,
     FuzzMode::kAiDecideState},
    {"AiUpdateFactionRecovery", fe7::kAddrAiUpdateFactionRecovery, false,
     FuzzMode::kAiDecideState},
    {"AiUpdateUnitRecoveryMode", fe7::kAddrAiUpdateUnitRecoveryMode, true,
     FuzzMode::kAiDecideState},
    {"AiGetClosestEscapePoint", fe7::kAddrAiGetClosestEscapePoint, true,
     FuzzMode::kAiDecideState},
    {"AiCanEquipBestItem", fe7::kAddrAiCanEquipBestItem, true,
     FuzzMode::kAiDecideState},
    {"AiRankUnitItems", fe7::kAddrAiRankUnitItems, true,
     FuzzMode::kAiDecideState},
    {"AiComputeEnemyThreatAtTile", fe7::kAddrAiComputeEnemyThreatAtTile, false,
     FuzzMode::kTracesOnly},
    {"AiEquipBestItemWithMask", fe7::kAddrAiEquipBestItemWithMask, false,
     FuzzMode::kAiDecideState},
    {"AiChooseBestItemToEquip", fe7::kAddrAiChooseBestItemToEquip, false,
     FuzzMode::kAiDecideState},
    {"AiUpdateUnitSpecialStaffFlags", fe7::kAddrAiUpdateUnitSpecialStaffFlags,
     false, FuzzMode::kAiDecideState},
    {"AiIsWithinMoveBirdsEye", fe7::kAddrAiIsWithinMoveBirdsEye, true,
     FuzzMode::kAiDecideState},
    {"AiGetUnitAttackPower", fe7::kAddrAiGetUnitAttackPower, true,
     FuzzMode::kAiDecideState},
    {"AiTryDanceOrPlay", fe7::kAddrAiTryDanceOrPlay, true,
     FuzzMode::kAiDecideState},
};

uint32_t NextXorshift(uint32_t& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

// Synthesizes valid pre-call traces for functions taking a uint16_t[3] pointer
// in r0, with varied EWRAM/IWRAM state and aligned EWRAM buffer addresses.
std::vector<fe7::TraceRecord> MakePtrBufferTraces(uint32_t gba_addr,
                                                  size_t count) {
  std::vector<fe7::TraceRecord> out;
  out.reserve(count);
  uint32_t rng = 0x5EED1234u ^ gba_addr;
  for (size_t i = 0; i < count; ++i) {
    fe7::TraceRecord rec;
    rec.gba_addr = gba_addr;
    rec.ewram.assign(fe7::gMem.ewram, fe7::gMem.ewram + fe7::kEwramSize);
    rec.iwram.assign(fe7::gMem.iwram, fe7::gMem.iwram + fe7::kIwramSize);

    const uint32_t offset = 0x1000u + static_cast<uint32_t>(i * 8u);
    rec.regs[0] = 0x02000000u + offset;
    for (size_t b = 0; b < 6; ++b) {
      const uint32_t v = NextXorshift(rng);
      rec.ewram[offset + b] = static_cast<uint8_t>(v & 0xFFu);
      rec.iwram[b] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    }
    out.push_back(std::move(rec));
  }
  return out;
}

// Synthesizes valid pre-call traces for FormatTime(frames, &h, &m, &s).
std::vector<fe7::TraceRecord> MakeFormatTimeTraces(uint32_t gba_addr,
                                                   size_t count) {
  std::vector<fe7::TraceRecord> out;
  out.reserve(count);
  uint32_t rng = 0xC10C4001u ^ gba_addr;
  for (size_t i = 0; i < count; ++i) {
    fe7::TraceRecord rec;
    rec.gba_addr = gba_addr;
    rec.ewram.assign(fe7::gMem.ewram, fe7::gMem.ewram + fe7::kEwramSize);
    rec.iwram.assign(fe7::gMem.iwram, fe7::gMem.iwram + fe7::kIwramSize);

    const uint32_t offset = 0x1000u + static_cast<uint32_t>(i * 8u);
    rec.regs[0] = NextXorshift(rng);
    rec.regs[1] = 0x02000000u + offset;
    rec.regs[2] = 0x02000000u + offset + 2u;
    rec.regs[3] = 0x02000000u + offset + 4u;
    out.push_back(std::move(rec));
  }
  return out;
}

// Populates a realistic Unit in `ewram` at `offset` referencing real FE7 US
// ROM PInfo/JInfo entries so stat, item, and attribute getters exercise real
// ROM data tables.
void PopulateSyntheticUnit(std::vector<uint8_t>& ewram, uint32_t offset,
                           uint32_t& rng) {
  fe7::Unit unit{};
  const int pid = static_cast<int>(1u + (NextXorshift(rng) % 40u));
  const int jid = static_cast<int>(1u + (NextXorshift(rng) % 40u));
  unit.pinfo = fe7::GetPersonInfo(pid);
  unit.jinfo = fe7::GetJobInfo(jid);
  unit.id = static_cast<int8_t>(1u + (NextXorshift(rng) % 50u));
  unit.flags = (NextXorshift(rng) & 1u) != 0u ? fe7::kUnitFlagRescuing : 0u;
  unit.max_hp = static_cast<int8_t>(15u + (NextXorshift(rng) % 45u));
  unit.hp = static_cast<int8_t>(NextXorshift(rng) % 65u);
  unit.pow = static_cast<int8_t>(NextXorshift(rng) % 30u);
  unit.skl = static_cast<int8_t>(NextXorshift(rng) % 30u);
  unit.spd = static_cast<int8_t>(NextXorshift(rng) % 30u);
  unit.def = static_cast<int8_t>(NextXorshift(rng) % 25u);
  unit.res = static_cast<int8_t>(NextXorshift(rng) % 25u);
  unit.lck = static_cast<int8_t>(NextXorshift(rng) % 30u);
  unit.bonus_con = static_cast<int8_t>(NextXorshift(rng) % 5u);
  unit.torch = static_cast<uint8_t>(NextXorshift(rng) & 0x7u);
  unit.barrier = static_cast<uint8_t>(NextXorshift(rng) & 0x7u);
  // Give a mix of empty inventories and valid FE7 weapon IDs (1..0x4F) with
  // weapon ranks so GetUnitEquippedWeapon + GetItem*Bonus run real ROM paths.
  for (int i = 0; i < fe7::kUnitItemCount; ++i) {
    if ((NextXorshift(rng) & 1u) != 0u) {
      const uint16_t iid =
          static_cast<uint16_t>(1u + (NextXorshift(rng) % 0x4Fu));
      unit.items[i] = static_cast<uint16_t>(iid | (0x14u << 8));
    }
  }
  for (int i = 0; i < fe7::kUnitWexpCount; ++i) {
    unit.wexp[i] =
        static_cast<uint8_t>((NextXorshift(rng) & 1u) != 0u ? 0xFB : 0);
  }
  std::memcpy(ewram.data() + offset, &unit, sizeof(fe7::Unit));
}

std::vector<fe7::TraceRecord> MakeUnitTraces(uint32_t gba_addr, size_t count,
                                             bool two_units) {
  std::vector<fe7::TraceRecord> out;
  out.reserve(count);
  uint32_t rng = 0x0A170001u ^ gba_addr;
  for (size_t i = 0; i < count; ++i) {
    fe7::TraceRecord rec;
    rec.gba_addr = gba_addr;
    rec.ewram.assign(fe7::gMem.ewram, fe7::gMem.ewram + fe7::kEwramSize);
    rec.iwram.assign(fe7::gMem.iwram, fe7::gMem.iwram + fe7::kIwramSize);

    constexpr uint32_t kUnitAOffset = 0x2000u;
    constexpr uint32_t kUnitBOffset = 0x2080u;
    PopulateSyntheticUnit(rec.ewram, kUnitAOffset, rng);
    rec.ewram[fe7::kChapterVisionAddr - 0x02000000u] =
        static_cast<uint8_t>(NextXorshift(rng) % 8u);
    rec.regs[0] = 0x02000000u + kUnitAOffset;
    if (two_units) {
      PopulateSyntheticUnit(rec.ewram, kUnitBOffset, rng);
      rec.regs[1] = 0x02000000u + kUnitBOffset;
    } else {
      rec.regs[1] = static_cast<uint32_t>(
          static_cast<int32_t>(NextXorshift(rng) % 80u) - 40);
      rec.regs[2] = NextXorshift(rng) & 0xFu;
    }
    out.push_back(std::move(rec));
  }
  return out;
}

// Synthesizes traces with valid FE7 US item IDs (0x00..0x9F) and random uses.
std::vector<fe7::TraceRecord> MakeValidItemTraces(uint32_t gba_addr,
                                                  size_t count) {
  std::vector<fe7::TraceRecord> out;
  out.reserve(count);
  uint32_t rng = 0x17E30001u ^ gba_addr;
  for (size_t i = 0; i < count; ++i) {
    fe7::TraceRecord rec;
    rec.gba_addr = gba_addr;
    rec.ewram.assign(fe7::gMem.ewram, fe7::gMem.ewram + fe7::kEwramSize);
    rec.iwram.assign(fe7::gMem.iwram, fe7::gMem.iwram + fe7::kIwramSize);
    const uint32_t iid = (i == 0) ? 0u : (NextXorshift(rng) % 0x9Fu);
    const uint32_t uses = NextXorshift(rng) & 0xFFu;
    rec.regs[0] = (uses << 8) | iid;
    out.push_back(std::move(rec));
  }
  return out;
}

// Synthesizes traces pairing a populated Unit with a valid item or slot index.
std::vector<fe7::TraceRecord> MakeUnitItemTraces(uint32_t gba_addr,
                                                 size_t count, FuzzMode mode) {
  std::vector<fe7::TraceRecord> out;
  out.reserve(count);
  uint32_t rng = 0x17E30002u ^ gba_addr;
  for (size_t i = 0; i < count; ++i) {
    fe7::TraceRecord rec;
    rec.gba_addr = gba_addr;
    rec.ewram.assign(fe7::gMem.ewram, fe7::gMem.ewram + fe7::kEwramSize);
    rec.iwram.assign(fe7::gMem.iwram, fe7::gMem.iwram + fe7::kIwramSize);

    constexpr uint32_t kUnitOffset = 0x2000u;
    PopulateSyntheticUnit(rec.ewram, kUnitOffset, rng);
    const uint32_t unit_addr = 0x02000000u + kUnitOffset;
    const uint32_t iid = (i == 0) ? 0u : (NextXorshift(rng) % 0x9Fu);
    const uint32_t item = ((NextXorshift(rng) & 0x3Fu) << 8) | iid;

    if (mode == FuzzMode::kUnitAndItem) {
      rec.regs[0] = unit_addr;
      rec.regs[1] = item;
    } else if (mode == FuzzMode::kUnitAndSlot) {
      rec.regs[0] = unit_addr;
      rec.regs[1] =
          NextXorshift(rng) % static_cast<uint32_t>(fe7::kUnitItemCount);
    } else {
      rec.regs[0] = item;
      rec.regs[1] = unit_addr;
    }
    out.push_back(std::move(rec));
  }
  return out;
}

// Synthesizes paired BattleUnits at kBattleActorAddr / kBattleTargetAddr with
// real ROM PInfo/JInfo, weapons, terrain, status rings, and tactician flags.
std::vector<fe7::TraceRecord> MakeBattleUnitTraces(uint32_t gba_addr,
                                                   size_t count) {
  std::vector<fe7::TraceRecord> out;
  out.reserve(count);
  uint32_t rng = 0xBA771E01u ^ gba_addr;
  for (size_t i = 0; i < count; ++i) {
    fe7::TraceRecord rec;
    rec.gba_addr = gba_addr;
    rec.ewram.assign(fe7::gMem.ewram, fe7::gMem.ewram + fe7::kEwramSize);
    rec.iwram.assign(fe7::gMem.iwram, fe7::gMem.iwram + fe7::kIwramSize);

    constexpr uint32_t kActorOff = fe7::kBattleActorAddr - 0x02000000u;
    constexpr uint32_t kTargetOff = fe7::kBattleTargetAddr - 0x02000000u;
    for (uint32_t off : {kActorOff, kTargetOff}) {
      PopulateSyntheticUnit(rec.ewram, off, rng);
      fe7::BattleUnit bu{};
      std::memcpy(&bu.unit, rec.ewram.data() + off, sizeof(fe7::Unit));
      bu.unit.status = static_cast<uint8_t>(NextXorshift(rng) % 9u);
      const uint16_t iid =
          static_cast<uint16_t>(1u + (NextXorshift(rng) % 0x9Eu));
      bu.weapon = static_cast<uint16_t>((0x14u << 8) | iid);
      bu.weapon_before = bu.weapon;
      bu.weapon_attributes = fe7::GetItemAttributes(bu.weapon);
      bu.weapon_type = static_cast<uint8_t>(fe7::GetItemKind(bu.weapon));
      bu.wtriangle_hit_bonus = static_cast<int8_t>(
          static_cast<int32_t>(NextXorshift(rng) % 3u) * 15 - 15);
      bu.wtriangle_dmg_bonus =
          static_cast<int8_t>(static_cast<int32_t>(NextXorshift(rng) % 3u) - 1);
      bu.terrain_defense = static_cast<int8_t>(NextXorshift(rng) % 4u);
      bu.terrain_avoid = static_cast<int8_t>((NextXorshift(rng) % 4u) * 10u);
      bu.terrain_resistance = static_cast<int8_t>(NextXorshift(rng) % 4u);
      bu.battle_speed = static_cast<int16_t>(NextXorshift(rng) % 30u);
      bu.battle_hit_rate = static_cast<int16_t>(NextXorshift(rng) % 150u);
      bu.battle_avoid_rate = static_cast<int16_t>(NextXorshift(rng) % 80u);
      bu.battle_crit_rate = static_cast<int16_t>(NextXorshift(rng) % 60u);
      bu.battle_dodge_rate = static_cast<int16_t>(NextXorshift(rng) % 30u);
      std::memcpy(rec.ewram.data() + off, &bu, sizeof(fe7::BattleUnit));
    }

    // Vary tactician flags & stars in gPlayStatus+0x2B..0x2D.
    const uint32_t play_off = fe7::kPlayStatusAddr - 0x02000000u;
    rec.ewram[play_off + 0x1Bu] = static_cast<uint8_t>(NextXorshift(rng) % 4u);
    rec.ewram[play_off + 0x2Bu] = static_cast<uint8_t>(
        ((NextXorshift(rng) % 7u) << 4) | (NextXorshift(rng) & 1u));
    const uint16_t stars =
        static_cast<uint16_t>((NextXorshift(rng) % 150u) << 4);
    std::memcpy(rec.ewram.data() + play_off + 0x2Cu, &stars, sizeof(stars));

    rec.regs[0] = fe7::kBattleActorAddr;
    rec.regs[1] = fe7::kBattleTargetAddr;
    out.push_back(std::move(rec));
  }
  return out;
}

// Writes a padded FE7 row-pointer table + tile grid into `ewram` at `buf_off`
// and stores `row_ptrs + 2` into `ewram` at `map_ptr_addr`.
uint32_t InitSyntheticMapBuffer(std::vector<uint8_t>& ewram, uint32_t buf_off,
                                uint32_t map_ptr_addr, int width, int height,
                                uint8_t fill_byte, int pad_rows = 2,
                                int pad_cols = 1) {
  const uint32_t buf_addr = 0x02000000u + buf_off;
  const uint32_t rows = static_cast<uint32_t>(height + 2 * pad_rows);
  const uint32_t stride = static_cast<uint32_t>(width + 2 * pad_cols);
  const uint32_t tile_base_off = buf_off + rows * 4u;
  std::memset(ewram.data() + tile_base_off, fill_byte, rows * stride);
  for (uint32_t r = 0; r < rows; ++r) {
    const uint32_t row_addr = 0x02000000u + tile_base_off + r * stride +
                              static_cast<uint32_t>(pad_cols);
    std::memcpy(ewram.data() + buf_off + r * 4u, &row_addr, sizeof(row_addr));
  }
  const uint32_t map_addr = buf_addr + static_cast<uint32_t>(pad_rows) * 4u;
  std::memcpy(ewram.data() + (map_ptr_addr - 0x02000000u), &map_addr,
              sizeof(map_addr));
  return map_addr;
}

// Synthesizes valid 2D padded maps, terrain, unit grid, movement tables, and
// BFS queue state for all 17 Wave 2 Map & Pathfinding functions.
std::vector<fe7::TraceRecord> MakeMapTraces(uint32_t gba_addr, size_t count) {
  std::vector<fe7::TraceRecord> out;
  out.reserve(count);
  uint32_t rng = 0x4A9F100Du ^ gba_addr;
  for (size_t i = 0; i < count; ++i) {
    fe7::TraceRecord rec;
    rec.gba_addr = gba_addr;
    rec.ewram.assign(fe7::gMem.ewram, fe7::gMem.ewram + fe7::kEwramSize);
    rec.iwram.assign(fe7::gMem.iwram, fe7::gMem.iwram + fe7::kIwramSize);

    const int width = 6 + static_cast<int>(NextXorshift(rng) % 6u);
    const int height = 6 + static_cast<int>(NextXorshift(rng) % 6u);
    const fe7::MapSize sz{static_cast<int16_t>(width),
                          static_cast<int16_t>(height)};
    std::memcpy(rec.ewram.data() + (fe7::kMapSizeAddr - 0x02000000u), &sz,
                sizeof(sz));

    constexpr uint32_t kTerrainBufOff = 0x10000u;
    constexpr uint32_t kUnitMapBufOff = 0x11000u;
    constexpr uint32_t kMovMapBufOff = 0x12000u;
    constexpr uint32_t kRngMapBufOff = 0x13000u;
    constexpr uint32_t kInitBufOff = 0x14000u;
    constexpr uint32_t kMovTableOff = 0x15000u;
    constexpr uint32_t kUnitObjOff = 0x15100u;

    InitSyntheticMapBuffer(rec.ewram, kTerrainBufOff, fe7::kMapTerrainAddr,
                           width, height, 0x00u);
    InitSyntheticMapBuffer(rec.ewram, kUnitMapBufOff, fe7::kMapUnitAddr, width,
                           height, 0x00u);
    const uint32_t mov_map_addr = InitSyntheticMapBuffer(
        rec.ewram, kMovMapBufOff, fe7::kMapMovementAddr, width, height, 0xFFu);
    InitSyntheticMapBuffer(rec.ewram, kRngMapBufOff, fe7::kMapRangeAddr, width,
                           height, 0x00u);

    // Populate interior terrain (1=plains, 2=forest, occasional 0=wall) and
    // unit occupancy while keeping the 1-tile/2-tile padding border at 0.
    const uint32_t rows = static_cast<uint32_t>(height + 4);
    const uint32_t stride = static_cast<uint32_t>(width + 2);
    const uint32_t terrain_tiles = kTerrainBufOff + rows * 4u;
    const uint32_t unit_tiles = kUnitMapBufOff + rows * 4u;
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        const uint32_t tile_off = static_cast<uint32_t>(y + 2) * stride +
                                  static_cast<uint32_t>(x + 1);
        const uint32_t r = NextXorshift(rng) % 10u;
        rec.ewram[terrain_tiles + tile_off] =
            (r == 0u) ? 0u : ((r < 7u) ? 1u : 2u);
        const uint32_t u = NextXorshift(rng) % 12u;
        if (u == 0u) {
          rec.ewram[unit_tiles + tile_off] = 0x01u;
        } else if (u == 1u) {
          rec.ewram[unit_tiles + tile_off] = 0x81u;
        }
      }
    }

    // Set gWorkingMap = gMapMovement.
    std::memcpy(rec.iwram.data() + (fe7::kWorkingMapAddr - 0x03000000u),
                &mov_map_addr, sizeof(mov_map_addr));

    // Populate 65-byte movement cost table in both EWRAM and gWorkingMovTable.
    std::array<int8_t, 65> mov_table{};
    mov_table.fill(1);
    mov_table[0] = -1;  // Border/wall terrain is impassable.
    mov_table[1] = 1;
    mov_table[2] = 2;
    std::memcpy(rec.ewram.data() + kMovTableOff, mov_table.data(),
                mov_table.size());
    std::memcpy(rec.iwram.data() + (fe7::kWorkingMovTableAddr - 0x03000000u),
                mov_table.data(), mov_table.size());

    // Populate a synthetic Unit positioned inside the map.
    PopulateSyntheticUnit(rec.ewram, kUnitObjOff, rng);
    const int ux = 1 + static_cast<int>(NextXorshift(rng) %
                                        static_cast<uint32_t>(width - 2));
    const int uy = 1 + static_cast<int>(NextXorshift(rng) %
                                        static_cast<uint32_t>(height - 2));
    fe7::Unit unit{};
    std::memcpy(&unit, rec.ewram.data() + kUnitObjOff, sizeof(unit));
    unit.x = static_cast<int8_t>(ux);
    unit.y = static_cast<int8_t>(uy);
    unit.bonus_mov = static_cast<int8_t>(NextXorshift(rng) % 3u);
    std::memcpy(rec.ewram.data() + kUnitObjOff, &unit, sizeof(unit));

    // Seed gMapFloodSquareBufB and gMapFloodSt for MapFloodCoreRam / Step.
    fe7::MapFloodSquare root{static_cast<int8_t>(ux), static_cast<int8_t>(uy),
                             5u, 0u};
    fe7::MapFloodSquare sentinel{0, 0, 4u, 0u};
    const uint32_t buf_b_off = fe7::kMapFloodBufBAddr - 0x03000000u;
    std::memcpy(rec.iwram.data() + buf_b_off, &root, sizeof(root));
    std::memcpy(rec.iwram.data() + buf_b_off + sizeof(root), &sentinel,
                sizeof(sentinel));
    fe7::MapFloodState st{};
    st.active_queue = fe7::GbaPtr<fe7::MapFloodSquare>(fe7::kMapFloodBufBAddr);
    st.next_queue = fe7::GbaPtr<fe7::MapFloodSquare>(fe7::kMapFloodBufAAddr);
    st.has_move_limit = static_cast<int8_t>(NextXorshift(rng) & 1u);
    st.max_move = static_cast<uint8_t>(2u + (NextXorshift(rng) % 6u));
    st.mover_id =
        static_cast<uint8_t>((NextXorshift(rng) & 1u) ? 0x01u : 0x81u);
    st.unk_0b = 0x78u;
    std::memcpy(rec.iwram.data() + (fe7::kMapFloodStAddr - 0x03000000u), &st,
                sizeof(st));

    const uint32_t unit_addr = 0x02000000u + kUnitObjOff;
    const uint32_t mov_table_addr = 0x02000000u + kMovTableOff;
    const uint32_t mov_budget = 1u + (NextXorshift(rng) % 7u);

    switch (gba_addr) {
      case fe7::kAddrMapInit:
        rec.regs[0] = 0x02000000u + kInitBufOff;
        rec.regs[1] = fe7::kMapMovementAddr;
        rec.regs[2] = static_cast<uint32_t>(width);
        rec.regs[3] = static_cast<uint32_t>(height);
        break;
      case fe7::kAddrMapFill:
      case fe7::kAddrMapFillEdges:
        rec.regs[0] = mov_map_addr;
        rec.regs[1] = NextXorshift(rng) & 0xFFu;
        break;
      case fe7::kAddrSetWorkingMap:
        rec.regs[0] = mov_map_addr;
        break;
      case fe7::kAddrSetWorkingMoveTable:
        rec.regs[0] = mov_table_addr;
        break;
      case fe7::kAddrMapFloodCoreStep: {
        static constexpr int kDx[4] = {-1, 1, 0, 0};
        static constexpr int kDy[4] = {0, 0, -1, 1};
        const uint32_t dir = NextXorshift(rng) & 3u;
        rec.regs[0] = dir;
        rec.regs[1] = static_cast<uint32_t>(static_cast<int32_t>(kDx[dir]));
        rec.regs[2] = static_cast<uint32_t>(static_cast<int32_t>(kDy[dir]));
        break;
      }
      case fe7::kAddrMapFloodCoreRam:
        break;
      case fe7::kAddrBeginMapFlood:
        rec.regs[0] = static_cast<uint32_t>(ux);
        rec.regs[1] = static_cast<uint32_t>(uy);
        rec.regs[2] = mov_budget;
        rec.regs[3] = (NextXorshift(rng) % 3u == 0u) ? 0u : 0x01u;
        break;
      case fe7::kAddrMapFloodUnit:
      case fe7::kAddrMapFloodUnitExtended:
        rec.regs[0] = unit_addr;
        break;
      case fe7::kAddrMapFloodUnitMovement:
        rec.regs[0] = unit_addr;
        rec.regs[1] = mov_budget;
        break;
      case fe7::kAddrMapFloodRangeUnitless:
      case fe7::kAddrMapFloodExtended:
        rec.regs[0] = static_cast<uint32_t>(ux);
        rec.regs[1] = static_cast<uint32_t>(uy);
        rec.regs[2] = mov_table_addr;
        break;
      case fe7::kAddrMapFloodOnWorkingMap:
        rec.regs[0] = unit_addr;
        rec.regs[1] = static_cast<uint32_t>(ux);
        rec.regs[2] = static_cast<uint32_t>(uy);
        rec.regs[3] = mov_budget;
        break;
      case fe7::kAddrMapAddInRange:
        rec.regs[0] = static_cast<uint32_t>(ux);
        rec.regs[1] = static_cast<uint32_t>(uy);
        rec.regs[2] = NextXorshift(rng) % 6u;
        rec.regs[3] = static_cast<uint32_t>(
            static_cast<int32_t>(NextXorshift(rng) % 5u) - 2);
        break;
      case fe7::kAddrMapSetInRange: {
        const uint32_t min_r = NextXorshift(rng) % 4u;
        const uint32_t max_r = min_r + (NextXorshift(rng) % 4u);
        rec.regs[0] = static_cast<uint32_t>(ux);
        rec.regs[1] = static_cast<uint32_t>(uy);
        rec.regs[2] = min_r;
        rec.regs[3] = max_r;
        break;
      }
      default:
        break;
    }

    out.push_back(std::move(rec));
  }
  return out;
}

// Synthesizes full battle simulation state (gBattleActor, gBattleTarget,
// gBattleStats, gBattleHitArray, gBattleHitIt, gMapUnit, and allied units for
// Triangle Attack checks) for all 22 Wave 3 Battle Simulation functions.
std::vector<fe7::TraceRecord> MakeBattleSimTraces(uint32_t gba_addr,
                                                  size_t count) {
  std::vector<fe7::TraceRecord> out;
  out.reserve(count);
  uint32_t rng = 0x51A0BA77u ^ gba_addr;
  for (size_t i = 0; i < count; ++i) {
    fe7::TraceRecord rec;
    rec.gba_addr = gba_addr;
    rec.ewram.assign(fe7::gMem.ewram, fe7::gMem.ewram + fe7::kEwramSize);
    rec.iwram.assign(fe7::gMem.iwram, fe7::gMem.iwram + fe7::kIwramSize);

    // Initialize an 8x8 gMapUnit so Triangle Attack neighbor lookups are safe.
    constexpr int kMapW = 8;
    constexpr int kMapH = 8;
    constexpr uint32_t kUnitMapBufOff = 0x11000u;
    InitSyntheticMapBuffer(rec.ewram, kUnitMapBufOff, fe7::kMapUnitAddr, kMapW,
                           kMapH, 0x00u);

    // Populate 3 blue units in gUnitArrayBlue (ids 1..3) so GetUnit(1..3) has
    // valid ROM PInfo/JInfo (including Pegasus Knight JID 0x35 for TA paths).
    for (int u = 0; u < 3; ++u) {
      const uint32_t off = fe7::GetUnit(u + 1).addr - 0x02000000u;
      PopulateSyntheticUnit(rec.ewram, off, rng);
      fe7::Unit ally{};
      std::memcpy(&ally, rec.ewram.data() + off, sizeof(ally));
      ally.id = static_cast<int8_t>(u + 1);
      if ((NextXorshift(rng) & 1u) != 0u) {
        ally.jinfo = fe7::GetJobInfo(0x35);  // Pegasus Knight
      }
      ally.status = static_cast<uint8_t>(NextXorshift(rng) % 5u);
      std::memcpy(rec.ewram.data() + off, &ally, sizeof(ally));
    }

    // Place allies 1..3 around defender at (4, 4) on gMapUnit.
    const uint32_t unit_tiles = kUnitMapBufOff + (kMapH + 4u) * 4u;
    const uint32_t stride = kMapW + 2u;
    static constexpr int kDx[4] = {-1, 0, 1, 0};
    static constexpr int kDy[4] = {0, -1, 0, 1};
    for (int d = 0; d < 4; ++d) {
      if ((NextXorshift(rng) & 1u) != 0u) {
        const uint32_t tile_off =
            static_cast<uint32_t>(4 + kDy[d] + 2) * stride +
            static_cast<uint32_t>(4 + kDx[d] + 1);
        rec.ewram[unit_tiles + tile_off] =
            static_cast<uint8_t>(1u + (NextXorshift(rng) % 3u));
      }
    }

    // Populate gBattleActor and gBattleTarget with varied weapons & stats.
    constexpr uint32_t kActorOff = fe7::kBattleActorAddr - 0x02000000u;
    constexpr uint32_t kTargetOff = fe7::kBattleTargetAddr - 0x02000000u;
    for (uint32_t off : {kActorOff, kTargetOff}) {
      PopulateSyntheticUnit(rec.ewram, off, rng);
      fe7::BattleUnit bu{};
      std::memcpy(&bu.unit, rec.ewram.data() + off, sizeof(fe7::Unit));
      bu.unit.x = 4;
      bu.unit.y = 4;
      bu.unit.hp = static_cast<int8_t>(1u + (NextXorshift(rng) % 35u));
      bu.unit.max_hp =
          static_cast<int8_t>(static_cast<int>(bu.unit.hp) +
                              static_cast<int>(NextXorshift(rng) % 15u));
      bu.unit.id = static_cast<int8_t>((off == kActorOff) ? 0x01 : 0x81);
      if ((NextXorshift(rng) & 3u) == 0u) {
        bu.unit.jinfo = fe7::GetJobInfo(0x35);  // Pegasus Knight
      }
      bu.unit.status = static_cast<uint8_t>(NextXorshift(rng) % 6u);
      const uint16_t iid =
          (NextXorshift(rng) % 5u == 0u)
              ? 0u
              : static_cast<uint16_t>(1u + (NextXorshift(rng) % 0x60u));
      bu.weapon = iid == 0u ? 0u : static_cast<uint16_t>((0x14u << 8) | iid);
      bu.weapon_before = (NextXorshift(rng) & 1u) ? bu.weapon : 0u;
      bu.weapon_attributes = fe7::GetItemAttributes(bu.weapon);
      bu.weapon_type = static_cast<uint8_t>(fe7::GetItemKind(bu.weapon));
      bu.can_counter = static_cast<int8_t>(NextXorshift(rng) & 1u);
      bu.battle_attack = static_cast<int16_t>(NextXorshift(rng) % 35u);
      bu.battle_defense = static_cast<int16_t>(NextXorshift(rng) % 25u);
      bu.battle_speed = static_cast<int16_t>(NextXorshift(rng) % 26u);
      bu.battle_effective_hit_rate =
          static_cast<int16_t>(NextXorshift(rng) % 105u);
      bu.battle_effective_crit_rate =
          static_cast<int16_t>(NextXorshift(rng) % 50u);
      bu.battle_silencer_rate = static_cast<int16_t>(NextXorshift(rng) % 50u);
      std::memcpy(rec.ewram.data() + off, &bu, sizeof(fe7::BattleUnit));
    }

    // Initialize gBattleStats and gBattleHitArray / gBattleHitIt.
    fe7::BattleStats stats{};
    stats.config = static_cast<uint16_t>(
        ((NextXorshift(rng) & 1u) ? fe7::kBattleConfigSimulate
                                  : fe7::kBattleConfigReal) |
        ((NextXorshift(rng) & 7u) == 0u ? fe7::kBattleConfigArena : 0u));
    stats.range = static_cast<uint8_t>(1u + (NextXorshift(rng) & 1u));
    stats.attack = static_cast<int16_t>(NextXorshift(rng) % 35u);
    stats.defense = static_cast<int16_t>(NextXorshift(rng) % 25u);
    stats.hit_rate = static_cast<int16_t>(NextXorshift(rng) % 105u);
    stats.crit_rate = static_cast<int16_t>(NextXorshift(rng) % 50u);
    stats.silencer_rate = static_cast<int16_t>(NextXorshift(rng) % 50u);
    stats.damage = static_cast<int16_t>(NextXorshift(rng) % 30u);
    std::memcpy(rec.ewram.data() + (fe7::kBattleStatsAddr - 0x02000000u),
                &stats, sizeof(stats));

    const uint32_t hits_off = fe7::kBattleHitsAddr - 0x02000000u;
    std::memset(rec.ewram.data() + hits_off, 0,
                sizeof(fe7::BattleHit) * fe7::kBattleHitMaxCount);
    fe7::BattleHit initial_hit{};
    initial_hit.info = (NextXorshift(rng) & 1u) ? fe7::kBattleHitInfoBegin : 0u;
    std::memcpy(rec.ewram.data() + hits_off, &initial_hit, sizeof(initial_hit));
    const uint32_t hit_it = fe7::kBattleHitsAddr;
    std::memcpy(rec.ewram.data() + (fe7::kBattleHitItAddr - 0x02000000u),
                &hit_it, sizeof(hit_it));

    // Scratch Unit and out-pointer slots in EWRAM.
    constexpr uint32_t kScratchUnitOff = 0x16000u;
    constexpr uint32_t kOutPtrAOff = 0x16100u;
    constexpr uint32_t kOutPtrBOff = 0x16104u;
    PopulateSyntheticUnit(rec.ewram, kScratchUnitOff, rng);

    switch (gba_addr) {
      case fe7::kAddrBattleRoll1RN:
      case fe7::kAddrBattleRoll2RN:
        rec.regs[0] = NextXorshift(rng) % 120u;
        rec.regs[1] = NextXorshift(rng) & 1u;
        break;
      case fe7::kAddrInitBattleUnit:
      case fe7::kAddrInitBattleUnitWithoutBonuses:
        rec.regs[0] = fe7::kBattleActorAddr;
        rec.regs[1] = 0x02000000u + kScratchUnitOff;
        break;
      case fe7::kAddrBattleGetBattleUnitOrder:
      case fe7::kAddrBattleGetFollowUpOrder:
        rec.regs[0] = 0x02000000u + kOutPtrAOff;
        rec.regs[1] = 0x02000000u + kOutPtrBOff;
        break;
      case fe7::kAddrBattleGenerateRoundHits:
      case fe7::kAddrBattleCheckTriangleAttackOrder:
      case fe7::kAddrBattleUpdateBattleStats:
      case fe7::kAddrBattleGenerateHitTriangleAttack:
      case fe7::kAddrBattleGenerateHitEffects:
      case fe7::kAddrBattleGenerateHit:
        rec.regs[0] = fe7::kBattleActorAddr;
        rec.regs[1] = fe7::kBattleTargetAddr;
        break;
      case fe7::kAddrBattleGetEffectiveHitCount:
      case fe7::kAddrBattleCheckBrave:
      case fe7::kAddrBattleGenerateHitAttributes:
      case fe7::kAddrBattleUnitTargetSetEquippedWeapon:
      case fe7::kAddrBattleUnitTargetCheckHasRealWeapon:
        rec.regs[0] = (NextXorshift(rng) & 1u) ? fe7::kBattleActorAddr
                                               : fe7::kBattleTargetAddr;
        break;
      default:
        break;
    }

    out.push_back(std::move(rec));
  }
  return out;
}

// Synthesizes valid gPlayStatus, faction unit arrays (Blue/Green/Red), and a
// scratch Proc + ProcScr for Wave 3 Turn Flow & Phase Management functions.
std::vector<fe7::TraceRecord> MakePhaseTraces(uint32_t gba_addr, size_t count) {
  std::vector<fe7::TraceRecord> out;
  out.reserve(count);
  uint32_t rng = 0x94A5E001u ^ gba_addr;
  static constexpr uint8_t kFactions[3] = {fe7::kFactionBlue, fe7::kFactionRed,
                                           fe7::kFactionGreen};

  for (size_t i = 0; i < count; ++i) {
    fe7::TraceRecord rec;
    rec.gba_addr = gba_addr;
    rec.ewram.assign(fe7::gMem.ewram, fe7::gMem.ewram + fe7::kEwramSize);
    rec.iwram.assign(fe7::gMem.iwram, fe7::gMem.iwram + fe7::kIwramSize);

    // Populate gPlayStatus fields: gold (+0x08), faction (+0x0F), turn (+0x10),
    // and config2 (+0x41).
    const uint32_t play_off = fe7::kPhasePlayStatusAddr - 0x02000000u;
    const int32_t gold = static_cast<int32_t>(NextXorshift(rng) % 1200000u);
    std::memcpy(rec.ewram.data() + play_off + 0x08u, &gold, sizeof(gold));
    const uint8_t faction = kFactions[NextXorshift(rng) % 3u];
    rec.ewram[play_off + 0x0Fu] = faction;
    const uint16_t turn = static_cast<uint16_t>(
        (NextXorshift(rng) % 8u == 0u) ? 999u : (1u + NextXorshift(rng) % 20u));
    std::memcpy(rec.ewram.data() + play_off + 0x10u, &turn, sizeof(turn));
    rec.ewram[play_off + 0x41u] = (NextXorshift(rng) & 1u) ? 0x40u : 0x00u;

    // Populate a handful of units across Blue, Green, and Red factions with
    // varied state flags and statuses so faction-iteration routines exercise
    // every branch safely.
    for (uint8_t f : kFactions) {
      for (int slot = 1; slot <= 4; ++slot) {
        const int uid = static_cast<int>(f) + slot;
        fe7::GbaPtr<fe7::Unit> ptr = fe7::GetUnit(uid);
        if (!ptr) {
          continue;
        }
        const uint32_t uoff = ptr.addr - 0x02000000u;
        if ((NextXorshift(rng) & 3u) == 0u) {
          std::memset(rec.ewram.data() + uoff, 0, sizeof(fe7::Unit));
          continue;
        }
        PopulateSyntheticUnit(rec.ewram, uoff, rng);
        fe7::Unit u{};
        std::memcpy(&u, rec.ewram.data() + uoff, sizeof(u));
        u.id = static_cast<int8_t>(uid);
        u.flags = NextXorshift(rng) & 0x000105FFu;
        u.status = static_cast<uint8_t>(NextXorshift(rng) % 6u);
        std::memcpy(rec.ewram.data() + uoff, &u, sizeof(u));
      }
    }

    // Scratch Proc + minimal ProcScr containing label 3 for
    // PlayerPhaseHandleAutoEnd.
    constexpr uint32_t kScrOff = 0x17000u;
    constexpr uint32_t kProcOff = 0x17100u;
    fe7::ProcScr script[2] = {
        {static_cast<int16_t>(fe7::ProcCmd::kLabel), 3,
         fe7::GbaPtr<const void>(0)},
        {static_cast<int16_t>(fe7::ProcCmd::kEnd), 0,
         fe7::GbaPtr<const void>(0)},
    };
    std::memcpy(rec.ewram.data() + kScrOff, script, sizeof(script));
    fe7::Proc proc{};
    proc.proc_script = fe7::GbaPtr<const fe7::ProcScr>(0x02000000u + kScrOff);
    proc.proc_script_pc = proc.proc_script;
    std::memcpy(rec.ewram.data() + kProcOff, &proc, sizeof(proc));

    switch (gba_addr) {
      case fe7::kAddrPlayerPhaseHandleAutoEnd:
        rec.regs[0] = 0x02000000u + kProcOff;
        break;
      case fe7::kAddrGetPhaseAbleUnitCount:
      case fe7::kAddrCountUnitsByFaction:
        rec.regs[0] = kFactions[NextXorshift(rng) % 3u];
        break;
      case fe7::kAddrCountUnitsInState:
        rec.regs[0] = kFactions[NextXorshift(rng) % 3u];
        rec.regs[1] = NextXorshift(rng) & 0x000100FFu;
        break;
      case fe7::kAddrAddGold:
        rec.regs[0] = static_cast<uint32_t>(
            static_cast<int32_t>(NextXorshift(rng) % 500000u) - 50000);
        break;
      default:
        break;
    }

    out.push_back(std::move(rec));
  }
  return out;
}

// Synthesizes valid BattleUnits, backing EWRAM Units, gBattleStats,
// gBattleHitArray, and gPlayStatus/gBmStatus flags for all 26 Wave 3
// Post-Battle EXP, Level-Up, WEXP, and Update functions.
std::vector<fe7::TraceRecord> MakeBattleExpTraces(uint32_t gba_addr,
                                                  size_t count) {
  std::vector<fe7::TraceRecord> out;
  out.reserve(count);
  uint32_t rng = 0xE890BA77u ^ gba_addr;
  for (size_t i = 0; i < count; ++i) {
    fe7::TraceRecord rec;
    rec.gba_addr = gba_addr;
    rec.ewram.assign(fe7::gMem.ewram, fe7::gMem.ewram + fe7::kEwramSize);
    rec.iwram.assign(fe7::gMem.iwram, fe7::gMem.iwram + fe7::kIwramSize);

    // Vary gPlayStatus mode/flags (+0x14, +0x1B) and gBmStatus (+0x04).
    const uint32_t play_off = fe7::kPlayStatusAddr - 0x02000000u;
    rec.ewram[play_off + 0x14u] =
        static_cast<uint8_t>(((NextXorshift(rng) & 7u) == 0u ? 0x80u : 0x00u) |
                             ((NextXorshift(rng) & 1u) ? 0x40u : 0x00u));
    rec.ewram[play_off + 0x1Bu] = static_cast<uint8_t>(NextXorshift(rng) % 4u);
    const uint32_t bm_off = fe7::kBmStatusAddr - 0x02000000u;
    rec.ewram[bm_off + 0x04u] = (NextXorshift(rng) & 7u) == 0u ? 0x40u : 0x00u;

    // Populate gBattleActor (id=1) and gBattleTarget (id=0x81) plus their
    // backing Unit slots in EWRAM so GetUnit(bu->unit.id) always finds valid
    // ROM PInfo/JInfo.
    constexpr uint32_t kActorOff = fe7::kBattleActorAddr - 0x02000000u;
    constexpr uint32_t kTargetOff = fe7::kBattleTargetAddr - 0x02000000u;
    const uint32_t actor_unit_off = fe7::GetUnit(1).addr - 0x02000000u;
    const uint32_t target_unit_off = fe7::GetUnit(0x81).addr - 0x02000000u;

    for (int side = 0; side < 2; ++side) {
      const uint32_t bu_off = (side == 0) ? kActorOff : kTargetOff;
      const uint32_t u_off = (side == 0) ? actor_unit_off : target_unit_off;
      PopulateSyntheticUnit(rec.ewram, u_off, rng);
      fe7::Unit u{};
      std::memcpy(&u, rec.ewram.data() + u_off, sizeof(u));
      // Pick a class with non-zero power_level and valid jid_promote.
      int jid = 1 + static_cast<int>(NextXorshift(rng) % 40u);
      while (fe7::GetJobInfo(jid)->power_level == 0 ||
             fe7::GetJobInfo(jid)->jid_promote == 0) {
        jid = 1 + (jid % 40);
      }
      u.jinfo = fe7::GetJobInfo(jid);
      u.id = static_cast<int8_t>(side == 0 ? 1 : 0x81);
      u.level =
          static_cast<int8_t>(1 + static_cast<int>(NextXorshift(rng) % 20u));
      u.exp = static_cast<uint8_t>(
          (NextXorshift(rng) & 7u) == 0u ? 0xFFu : (NextXorshift(rng) % 130u));
      u.hp = static_cast<int8_t>(
          (NextXorshift(rng) & 3u) == 0u
              ? 0
              : (1 + static_cast<int>(NextXorshift(rng) % 30u)));
      u.max_hp = static_cast<int8_t>(static_cast<int>(u.hp) +
                                     static_cast<int>(NextXorshift(rng) % 25u));
      u.ballista_trap = static_cast<uint8_t>(NextXorshift(rng) % 4u);
      if ((NextXorshift(rng) & 3u) == 0u) {
        u.flags |= 0x2000u;  // Afa's Drops
      }
      std::memcpy(rec.ewram.data() + u_off, &u, sizeof(u));

      fe7::BattleUnit bu{};
      bu.unit = u;
      const uint16_t iid =
          static_cast<uint16_t>(1u + (NextXorshift(rng) % 0x60u));
      bu.weapon = static_cast<uint16_t>((0x14u << 8) | iid);
      bu.weapon_before = bu.weapon;
      bu.weapon_attributes = fe7::GetItemAttributes(bu.weapon);
      const int kind = fe7::GetItemKind(bu.weapon);
      bu.weapon_type = static_cast<uint8_t>(kind < 8 ? kind : 0);
      bu.weapon_slot_index = static_cast<uint8_t>(NextXorshift(rng) % 5u);
      bu.can_counter = static_cast<int8_t>(NextXorshift(rng) & 1u);
      bu.exp_gain = static_cast<int8_t>(NextXorshift(rng) % 40u);
      bu.status_out = (NextXorshift(rng) & 1u)
                          ? -1
                          : static_cast<int8_t>(NextXorshift(rng) % 5u);
      bu.change_hp = static_cast<int8_t>(NextXorshift(rng) % 3u);
      bu.change_pow = static_cast<int8_t>(NextXorshift(rng) % 3u);
      bu.change_skl = static_cast<int8_t>(NextXorshift(rng) % 3u);
      bu.change_spd = static_cast<int8_t>(NextXorshift(rng) % 3u);
      bu.change_def = static_cast<int8_t>(NextXorshift(rng) % 3u);
      bu.change_res = static_cast<int8_t>(NextXorshift(rng) % 3u);
      bu.change_lck = static_cast<int8_t>(NextXorshift(rng) % 3u);
      bu.wexp_multiplier = static_cast<int8_t>(NextXorshift(rng) % 4u);
      bu.non_zero_damage = static_cast<int8_t>(NextXorshift(rng) & 1u);
      std::memcpy(rec.ewram.data() + bu_off, &bu, sizeof(bu));
    }

    // Initialize gBattleStats and gBattleHitArray.
    fe7::BattleStats stats{};
    stats.config = static_cast<uint16_t>(
        ((NextXorshift(rng) & 3u) == 0u ? fe7::kBattleConfigBallista : 0u) |
        ((NextXorshift(rng) & 7u) == 0u ? fe7::kBattleConfigArena : 0u));
    std::memcpy(rec.ewram.data() + (fe7::kBattleStatsAddr - 0x02000000u),
                &stats, sizeof(stats));

    const uint32_t hits_off = fe7::kBattleHitsAddr - 0x02000000u;
    std::memset(rec.ewram.data() + hits_off, 0,
                sizeof(fe7::BattleHit) * fe7::kBattleHitMaxCount);
    fe7::BattleHit h0{};
    if ((NextXorshift(rng) & 3u) == 0u) {
      h0.attributes |= fe7::kBattleHitAttrSilencer;
    }
    if ((NextXorshift(rng) & 3u) == 0u) {
      h0.attributes |= fe7::kBattleHitAttrMiss;
    }
    std::memcpy(rec.ewram.data() + hits_off, &h0, sizeof(h0));

    switch (gba_addr) {
      case fe7::kAddrGetStatIncrease:
        rec.regs[0] = NextXorshift(rng) % 250u;
        break;
      case fe7::kAddrGetAutoleveledStatIncrease:
        rec.regs[0] = NextXorshift(rng) % 120u;
        rec.regs[1] = 1u + (NextXorshift(rng) % 20u);
        break;
      case fe7::kAddrCanBattleUnitGainExp:
      case fe7::kAddrCheckBattleUnitLevelUp:
      case fe7::kAddrGetBattleUnitUpdatedWeaponExp:
      case fe7::kAddrHasBattleUnitGainedWeaponLevel:
      case fe7::kAddrGetBattleUnitStaffExp:
        rec.regs[0] = (NextXorshift(rng) & 1u) ? fe7::kBattleActorAddr
                                               : fe7::kBattleTargetAddr;
        break;
      case fe7::kAddrUnitPromote:
      case fe7::kAddrGetUnitExpLevel:
      case fe7::kAddrGetUnitPowerLevel:
        rec.regs[0] = 0x02000000u + actor_unit_off;
        break;
      case fe7::kAddrGenerateBattleUnitStatGainsComparatively:
        rec.regs[0] = fe7::kBattleActorAddr;
        rec.regs[1] = 0x02000000u + actor_unit_off;
        break;
      case fe7::kAddrCheckBattleUnitStatCaps:
      case fe7::kAddrUpdateUnitFromBattle:
      case fe7::kAddrUpdateUnitDuringBattle:
        rec.regs[0] = 0x02000000u + actor_unit_off;
        rec.regs[1] = fe7::kBattleActorAddr;
        break;
      case fe7::kAddrGetUnitRoundExp:
      case fe7::kAddrGetUnitClassKillExpBonus:
      case fe7::kAddrGetUnitExpMultiplier:
      case fe7::kAddrGetUnitKillExpBonus:
      case fe7::kAddrGetBattleUnitExpGain:
        rec.regs[0] = fe7::kBattleActorAddr;
        rec.regs[1] = fe7::kBattleTargetAddr;
        break;
      default:
        break;
    }

    out.push_back(std::move(rec));
  }
  return out;
}

// Synthesizes paired FE7 US blue units with real ROM PInfo::support_info
// entries, varied support EXP/bits/distances, and a scratch SupportBonuses
// buffer for all 22 Wave 3 Support functions.
std::vector<fe7::TraceRecord> MakeSupportTraces(uint32_t gba_addr,
                                                size_t count) {
  std::vector<fe7::TraceRecord> out;
  out.reserve(count);
  uint32_t rng = 0x50990A71u ^ gba_addr;
  static constexpr uint8_t kExpSamples[8] = {0, 40, 78, 80, 158, 160, 240, 241};

  for (size_t i = 0; i < count; ++i) {
    fe7::TraceRecord rec;
    rec.gba_addr = gba_addr;
    rec.ewram.assign(fe7::gMem.ewram, fe7::gMem.ewram + fe7::kEwramSize);
    rec.iwram.assign(fe7::gMem.iwram, fe7::gMem.iwram + fe7::kIwramSize);

    const uint32_t play_off = fe7::kSupportPlayStatusAddr - 0x02000000u;
    const uint16_t turn = static_cast<uint16_t>(1u + (NextXorshift(rng) % 5u));
    std::memcpy(rec.ewram.data() + play_off + 0x10u, &turn, sizeof(turn));
    rec.ewram[play_off + 0x14u] =
        static_cast<uint8_t>(((NextXorshift(rng) & 7u) == 0u ? 0x80u : 0x00u) |
                             ((NextXorshift(rng) & 7u) == 0u ? 0x08u : 0x00u));
    rec.ewram[play_off + 0x1Bu] = static_cast<uint8_t>(NextXorshift(rng) % 4u);
    const uint32_t bm_off = fe7::kSupportBmStatusAddr - 0x02000000u;
    rec.ewram[bm_off + 0x04u] = (NextXorshift(rng) & 7u) == 0u ? 0x40u : 0x00u;

    // Pick Eliwood (PID 1) and one of his real ROM support partners.
    constexpr uint8_t kPidA = 1;
    auto sup_a = fe7::GetPersonInfo(kPidA)->support_info;
    const int slot = static_cast<int>(NextXorshift(rng) % sup_a->count);
    const uint8_t pid_b = sup_a->pids[slot];

    const uint32_t u1_off = fe7::GetUnit(1).addr - 0x02000000u;
    const uint32_t u2_off = fe7::GetUnit(2).addr - 0x02000000u;
    fe7::Unit u1{};
    u1.pinfo = fe7::GetPersonInfo(kPidA);
    u1.jinfo = fe7::GetJobInfo(1);
    u1.id = 1;
    u1.x = 5;
    u1.y = 5;
    u1.support_bits = static_cast<uint8_t>(NextXorshift(rng) & 0x7Fu);
    for (int s = 0; s < fe7::kUnitSupportCount; ++s) {
      u1.supports[s] = kExpSamples[NextXorshift(rng) & 7u];
    }

    fe7::Unit u2{};
    u2.pinfo = fe7::GetPersonInfo(pid_b);
    u2.jinfo = fe7::GetJobInfo(2);
    u2.id = 2;
    const int dist_choice = static_cast<int>(NextXorshift(rng) % 5u);
    u2.x = static_cast<int8_t>(5 + dist_choice);
    u2.y = 5;
    if (dist_choice == 0 && (NextXorshift(rng) & 1u)) {
      u1.rescue = 2;
      u2.flags |= 0x20u;
    }
    u2.support_bits = static_cast<uint8_t>(NextXorshift(rng) & 0x7Fu);
    for (int s = 0; s < fe7::kUnitSupportCount; ++s) {
      u2.supports[s] = kExpSamples[NextXorshift(rng) & 7u];
    }

    std::memcpy(rec.ewram.data() + u1_off, &u1, sizeof(u1));
    std::memcpy(rec.ewram.data() + u2_off, &u2, sizeof(u2));

    constexpr uint32_t kBonusOff = 0x18000u;
    const uint32_t bonus_addr = 0x02000000u + kBonusOff;
    const uint32_t u1_addr = 0x02000000u + u1_off;

    switch (gba_addr) {
      case fe7::kAddrGetUnitSupporterCount:
      case fe7::kAddrGetUnitTotalSupportLevel:
      case fe7::kAddrClearUnitSupports:
      case fe7::kAddrGetUnitAffinityIcon:
        rec.regs[0] = u1_addr;
        break;
      case fe7::kAddrGetUnitSupporterCharacter:
      case fe7::kAddrGetUnitSupporterUnit:
      case fe7::kAddrGetSupportLevelBySupportIndex:
      case fe7::kAddrUnitGainSupportExp:
      case fe7::kAddrUnitGainSupportLevel:
      case fe7::kAddrCanUnitSupportNow:
      case fe7::kAddrGetUnitSupporterInitialExp:
      case fe7::kAddrHasUnitGainedSupportLevel:
        rec.regs[0] = u1_addr;
        rec.regs[1] = static_cast<uint32_t>(slot);
        break;
      case fe7::kAddrGetUnitSupporterNum:
        rec.regs[0] = u1_addr;
        rec.regs[1] = (NextXorshift(rng) & 1u) ? pid_b : 99u;
        break;
      case fe7::kAddrSupportProcessGains:
        break;
      case fe7::kAddrGetAffinityBonusEntry:
        rec.regs[0] = 1u + (NextXorshift(rng) % 8u);
        break;
      case fe7::kAddrApplyAffinitySupportBonuses:
        rec.regs[0] = bonus_addr;
        rec.regs[1] = 1u + (NextXorshift(rng) % 7u);
        rec.regs[2] = NextXorshift(rng) % 4u;
        break;
      case fe7::kAddrInitSupportBonuses:
        rec.regs[0] = bonus_addr;
        break;
      case fe7::kAddrGetUnitSupportBonuses:
        rec.regs[0] = u1_addr;
        rec.regs[1] = bonus_addr;
        break;
      case fe7::kAddrGetCharacterAffinityIcon:
        rec.regs[0] = 1u + (NextXorshift(rng) % 40u);
        break;
      case fe7::kAddrGetSupportLevelUiChar:
        rec.regs[0] = NextXorshift(rng) % 4u;
        break;
      case fe7::kAddrSetSupportLevelGained:
      case fe7::kAddrArePidsAtMaxSupport:
        rec.regs[0] = kPidA;
        rec.regs[1] = pid_b;
        break;
      default:
        break;
    }

    out.push_back(std::move(rec));
  }
  return out;
}

// Synthesizes valid 8x8 padded maps (terrain, unit, movement, range, other),
// active/battle units, AI weight-table pointers, and scratch buffers for all
// Wave 3 Enemy AI Decision & Scoring functions.
std::vector<fe7::TraceRecord> MakeAiTraces(uint32_t gba_addr, size_t count) {
  std::vector<fe7::TraceRecord> out;
  out.reserve(count);
  uint32_t rng = 0xA15C0AE1u ^ gba_addr;
  for (size_t i = 0; i < count; ++i) {
    fe7::TraceRecord rec;
    rec.gba_addr = gba_addr;
    rec.ewram.assign(fe7::gMem.ewram, fe7::gMem.ewram + fe7::kEwramSize);
    rec.iwram.assign(fe7::gMem.iwram, fe7::gMem.iwram + fe7::kIwramSize);

    constexpr int kW = 8;
    constexpr int kH = 8;
    const fe7::MapSize sz{kW, kH};
    std::memcpy(rec.ewram.data() + (fe7::kMapSizeAddr - 0x02000000u), &sz,
                sizeof(sz));

    constexpr uint32_t kTerrainBufOff = 0x10000u;
    constexpr uint32_t kUnitMapBufOff = 0x11000u;
    constexpr uint32_t kMovMapBufOff = 0x12000u;
    constexpr uint32_t kRngMapBufOff = 0x13000u;
    constexpr uint32_t kOtherMapBufOff = 0x14000u;
    InitSyntheticMapBuffer(rec.ewram, kTerrainBufOff, fe7::kMapTerrainAddr, kW,
                           kH, 0x00u);
    InitSyntheticMapBuffer(rec.ewram, kUnitMapBufOff, fe7::kMapUnitAddr, kW, kH,
                           0x00u, /*pad_rows=*/4, /*pad_cols=*/4);
    const uint32_t mov_map_addr = InitSyntheticMapBuffer(
        rec.ewram, kMovMapBufOff, fe7::kMapMovementAddr, kW, kH, 0x02u);
    InitSyntheticMapBuffer(rec.ewram, kRngMapBufOff, fe7::kMapRangeAddr, kW, kH,
                           0x01u);
    InitSyntheticMapBuffer(rec.ewram, kOtherMapBufOff, fe7::kAiMapOtherAddr, kW,
                           kH, 0x00u);
    std::memcpy(rec.iwram.data() + (fe7::kWorkingMapAddr - 0x03000000u),
                &mov_map_addr, sizeof(mov_map_addr));

    // Populate interior terrain (1=plains, 2=forest), units, and danger values.
    const uint32_t rows = kH + 4u;
    const uint32_t stride = kW + 2u;
    const uint32_t unit_rows = kH + 8u;
    const uint32_t unit_stride = kW + 8u;
    const uint32_t terrain_tiles = kTerrainBufOff + rows * 4u;
    const uint32_t unit_tiles = kUnitMapBufOff + unit_rows * 4u;
    const uint32_t mov_tiles = kMovMapBufOff + rows * 4u;
    const uint32_t rng_tiles = kRngMapBufOff + rows * 4u;
    const uint32_t other_tiles = kOtherMapBufOff + rows * 4u;
    for (int y = 0; y < kH; ++y) {
      for (int x = 0; x < kW; ++x) {
        const uint32_t toff = static_cast<uint32_t>(y + 2) * stride +
                              static_cast<uint32_t>(x + 1);
        const uint32_t utoff = static_cast<uint32_t>(y + 4) * unit_stride +
                               static_cast<uint32_t>(x + 4);
        rec.ewram[terrain_tiles + toff] =
            static_cast<uint8_t>(1u + (NextXorshift(rng) % 2u));
        const uint32_t u = NextXorshift(rng) % 10u;
        if (u == 0u) rec.ewram[unit_tiles + utoff] = 0x01u;
        if (u == 1u) rec.ewram[unit_tiles + utoff] = 0x81u;
        rec.ewram[mov_tiles + toff] = (NextXorshift(rng) & 1u) ? 0x02u : 0xFFu;
        rec.ewram[rng_tiles + toff] = (NextXorshift(rng) & 1u) ? 1u : 0u;
        rec.ewram[other_tiles + toff] =
            static_cast<uint8_t>(NextXorshift(rng) & 0x7Fu);
      }
    }

    // Active unit (enemy id 0x81) and target unit (blue id 1).
    const uint32_t unit_a_off = fe7::GetUnit(0x81).addr - 0x02000000u;
    const uint32_t unit_b_off = fe7::GetUnit(1).addr - 0x02000000u;
    PopulateSyntheticUnit(rec.ewram, unit_a_off, rng);
    PopulateSyntheticUnit(rec.ewram, unit_b_off, rng);
    const int ax = 2 + static_cast<int>(NextXorshift(rng) % 4u);
    const int ay = 2 + static_cast<int>(NextXorshift(rng) % 4u);
    const int bx = 2 + static_cast<int>(NextXorshift(rng) % 4u);
    const int by = 2 + static_cast<int>(NextXorshift(rng) % 4u);

    fe7::Unit ua{};
    std::memcpy(&ua, rec.ewram.data() + unit_a_off, sizeof(ua));
    ua.id = static_cast<int8_t>(0x81);
    ua.x = static_cast<int8_t>(ax);
    ua.y = static_cast<int8_t>(ay);
    std::memcpy(rec.ewram.data() + unit_a_off, &ua, sizeof(ua));

    fe7::Unit ub{};
    std::memcpy(&ub, rec.ewram.data() + unit_b_off, sizeof(ub));
    ub.id = 1;
    ub.x = static_cast<int8_t>(bx);
    ub.y = static_cast<int8_t>(by);
    std::memcpy(rec.ewram.data() + unit_b_off, &ub, sizeof(ub));

    // Set gActiveUnitId and gActiveUnit.
    rec.ewram[fe7::kAiActiveUnitIdAddr - 0x02000000u] = 0x81u;
    const uint32_t ua_addr = 0x02000000u + unit_a_off;
    const uint32_t ub_addr = 0x02000000u + unit_b_off;
    std::memcpy(rec.iwram.data() + (fe7::kAiActiveUnitAddr - 0x03000000u),
                &ua_addr, sizeof(ua_addr));

    // Populate gBattleActor and gBattleTarget for combat score components.
    fe7::BattleUnit ba{};
    ba.unit = ua;
    ba.unit.hp = static_cast<int8_t>(NextXorshift(rng) % 30u);
    ba.battle_attack = static_cast<int16_t>(NextXorshift(rng) % 30u);
    ba.battle_defense = static_cast<int16_t>(NextXorshift(rng) % 20u);
    ba.battle_effective_hit_rate =
        static_cast<int16_t>(NextXorshift(rng) % 101u);
    std::memcpy(rec.ewram.data() + (fe7::kBattleActorAddr - 0x02000000u), &ba,
                sizeof(ba));

    fe7::BattleUnit bt{};
    bt.unit = ub;
    bt.unit.hp = static_cast<int8_t>(NextXorshift(rng) % 30u);
    bt.weapon = (NextXorshift(rng) & 1u) ? 0x1401u : 0u;
    bt.battle_attack = static_cast<int16_t>(NextXorshift(rng) % 30u);
    bt.battle_defense = static_cast<int16_t>(NextXorshift(rng) % 20u);
    bt.battle_effective_hit_rate =
        static_cast<int16_t>(NextXorshift(rng) % 101u);
    std::memcpy(rec.ewram.data() + (fe7::kBattleTargetAddr - 0x02000000u), &bt,
                sizeof(bt));

    // Set gAiState weight_id (+0x7D), danger_filled (+0x7A), turn (+0x10), and
    // gAiScoreCoeffPtr (0x030013C0).
    const uint8_t weight_id = static_cast<uint8_t>(NextXorshift(rng) % 4u);
    rec.ewram[(fe7::kAiStateAddr - 0x02000000u) + 0x7Du] = weight_id;
    rec.ewram[(fe7::kAiStateAddr - 0x02000000u) + 0x7Au] =
        static_cast<uint8_t>(NextXorshift(rng) & 1u);
    const uint32_t coeff_ptr = fe7::kAiCombatScoreCoeffTableAddr +
                               static_cast<uint32_t>(weight_id) *
                                   sizeof(fe7::AiCombatScoreCoefficients);
    std::memcpy(rec.iwram.data() + (fe7::kAiScoreCoeffPtrAddr - 0x03000000u),
                &coeff_ptr, sizeof(coeff_ptr));
    const uint16_t turn = static_cast<uint16_t>(1u + (NextXorshift(rng) % 15u));
    std::memcpy(
        rec.ewram.data() + (fe7::kAiPlayStatusAddr - 0x02000000u) + 0x10u,
        &turn, sizeof(turn));

    // Scratch buffers in EWRAM.
    constexpr uint32_t kShortListOff = 0x18100u;
    constexpr uint32_t kByteListOff = 0x18140u;
    constexpr uint32_t kUdefOff = 0x18180u;
    constexpr uint32_t kPosOff = 0x181C0u;
    constexpr uint32_t kSimStOff = 0x18200u;
    const uint16_t slist[4] = {0x11u, 0x22u, 0x33u, 0u};
    const uint8_t blist[4] = {0x05u, 0x0Au, 0x0Fu, 0u};
    std::memcpy(rec.ewram.data() + kShortListOff, slist, sizeof(slist));
    std::memcpy(rec.ewram.data() + kByteListOff, blist, sizeof(blist));
    for (uint32_t b = 0; b < 16; ++b) {
      rec.ewram[kUdefOff + b] = static_cast<uint8_t>(NextXorshift(rng) & 0xFFu);
    }

    const uint16_t item = static_cast<uint16_t>(
        (0x14u << 8) | (1u + (NextXorshift(rng) % 0x70u)));

    switch (gba_addr) {
      case fe7::kAddrAiIsWithinReachByBirdsEyeDistance:
      case fe7::kAddrAiCouldReachByBirdsEyeDistance:
        rec.regs[0] = ua_addr;
        rec.regs[1] = ub_addr;
        rec.regs[2] = item;
        break;
      case fe7::kAddrAiIsInShortList:
        rec.regs[0] = 0x02000000u + kShortListOff;
        rec.regs[1] = (NextXorshift(rng) & 1u) ? 0x22u : 0x44u;
        break;
      case fe7::kAddrAiIsInByteList:
        rec.regs[0] = 0x02000000u + kByteListOff;
        rec.regs[1] = (NextXorshift(rng) & 1u) ? 0x0Au : 0x99u;
        break;
      case fe7::kAddrAiGetJobRank:
        rec.regs[0] = 1u + (NextXorshift(rng) % 60u);
        break;
      case fe7::kAddrAiUpdateUnitStaffFlags:
      case fe7::kAddrAiUpdateUnitItemFlags:
        rec.regs[0] = ua_addr;
        rec.regs[1] = item;
        break;
      case fe7::kAddrAiCountNearbyAllies:
        rec.regs[0] = ua_addr;
        break;
      case fe7::kAddrUnitInitAiFromInfo:
        rec.regs[0] = ua_addr;
        rec.regs[1] = 0x02000000u + kUdefOff;
        break;
      case fe7::kAddrAiFindBestPositionInRange:
        rec.regs[0] = 0x02000000u + kPosOff;
        break;
      case fe7::kAddrAiComputeCombatScore:
        rec.regs[0] = 0x02000000u + kSimStOff;
        break;
      case fe7::kAddrAiGetInRangeCombatPositionScoreComponent:
        rec.regs[0] = static_cast<uint32_t>(ax);
        rec.regs[1] = static_cast<uint32_t>(ay);
        rec.regs[2] = ub_addr;
        break;
      case fe7::kAddrAiGetTerrainCombatPositionScoreComponent:
      case fe7::kAddrAiGetFriendZonePositionScoreComponent:
        rec.regs[0] = static_cast<uint32_t>(ax);
        rec.regs[1] = static_cast<uint32_t>(ay);
        break;
      case fe7::kAddrPidStatsAddScore:
      case fe7::kAddrPidStatsAddExpGained:
        rec.regs[0] = 1u + (NextXorshift(rng) % 0x45u);
        rec.regs[1] = static_cast<uint32_t>(
            static_cast<int32_t>(NextXorshift(rng) % 100u) - 20);
        break;
      case fe7::kAddrRecordUnitTurnDeployment:
        rec.regs[0] = 1u + (NextXorshift(rng) % 0x45u);
        break;
      case fe7::kAddrAiMakeMoveRangeMapsForUnitAndWeapon:
        rec.regs[0] = ua_addr;
        rec.regs[1] = 0x1401u;  // Iron Sword (valid range 1-1)
        break;
      case fe7::kAddrGetChapterInfo:
      case fe7::kAddrGetChapterEventInfo:
        rec.regs[0] = NextXorshift(rng) % 0x20u;
        break;
      case fe7::kAddrCheckChapterFlag:
        rec.regs[0] = NextXorshift(rng) % 0x64u;
        break;
      case fe7::kAddrCheckPermanentFlag:
        rec.regs[0] = 0x64u + (NextXorshift(rng) % 0x40u);
        break;
      case fe7::kAddrGetFlag:
        rec.regs[0] = NextXorshift(rng) % 0xA0u;
        break;
      case fe7::kAddrSearchEventList: {
        // Point EventListInfo::list_ptr to an empty 0-terminated ROM/EWRAM
        // list.
        constexpr uint32_t kEventInfoOff = 0x18240u;
        constexpr uint32_t kEmptyListOff = 0x18280u;
        const uint32_t list_addr = 0x02000000u + kEmptyListOff;
        std::memcpy(rec.ewram.data() + kEventInfoOff, &list_addr,
                    sizeof(list_addr));
        rec.regs[0] = 0x02000000u + kEventInfoOff;
        break;
      }
      case fe7::kAddrCheckTurnEvents:
      case fe7::kAddrCheckVictoryEvents:
        rec.ewram[(fe7::kPhasePlayStatusAddr - 0x02000000u) + 0x0Eu] =
            static_cast<uint8_t>(NextXorshift(rng) % 10u);
        break;
      default:
        break;
    }

    out.push_back(std::move(rec));
  }
  return out;
}

// Synthesizes valid padded maps (terrain, unit, fog, hidden), active units,
// and gTrapArray entries for all 27 Trap & Fog functions.
std::vector<fe7::TraceRecord> MakeTrapTraces(uint32_t gba_addr, size_t count) {
  std::vector<fe7::TraceRecord> out;
  out.reserve(count);
  uint32_t rng = 0x54524150u ^ gba_addr;
  for (size_t i = 0; i < count; ++i) {
    fe7::TraceRecord rec;
    rec.gba_addr = gba_addr;
    rec.ewram.assign(fe7::kEwramSize, 0u);
    rec.iwram.assign(fe7::kIwramSize, 0u);
    std::memcpy(rec.iwram.data(), fe7::gMem.iwram, fe7::kIwramSize);

    constexpr int kW = 8;
    constexpr int kH = 8;
    const fe7::MapSize sz{kW, kH};
    std::memcpy(rec.ewram.data() + (fe7::kMapSizeAddr - 0x02000000u), &sz,
                sizeof(sz));

    constexpr uint32_t kTerrainBufOff = 0x10000u;
    constexpr uint32_t kUnitMapBufOff = 0x11000u;
    constexpr uint32_t kFogMapBufOff = 0x12000u;
    constexpr uint32_t kHiddenMapBufOff = 0x13000u;
    InitSyntheticMapBuffer(rec.ewram, kTerrainBufOff, fe7::kMapTerrainAddr, kW,
                           kH, 0x00u);
    InitSyntheticMapBuffer(rec.ewram, kUnitMapBufOff, fe7::kMapUnitAddr, kW, kH,
                           0x00u);
    const uint32_t fog_addr = InitSyntheticMapBuffer(
        rec.ewram, kFogMapBufOff, fe7::kMapFogAddr, kW, kH, 0x00u);
    InitSyntheticMapBuffer(rec.ewram, kHiddenMapBufOff, fe7::kMapHiddenAddr, kW,
                           kH, 0x00u);
    std::memcpy(rec.iwram.data() + (fe7::kWorkingMapAddr - 0x03000000u),
                &fog_addr, sizeof(fog_addr));

    // Populate interior terrain (plains, breakable wall 0x1B, snag 0x33,
    // roof 0x22).
    const uint32_t rows = kH + 4u;
    const uint32_t stride = kW + 2u;
    const uint32_t terrain_tiles = kTerrainBufOff + rows * 4u;
    for (int y = 0; y < kH; ++y) {
      for (int x = 0; x < kW; ++x) {
        const uint32_t toff = static_cast<uint32_t>(y + 2) * stride +
                              static_cast<uint32_t>(x + 1);
        const uint32_t r = NextXorshift(rng) % 10u;
        uint8_t tid = 1u;
        if (r == 0u) tid = 0x1Bu;
        if (r == 1u) tid = 0x33u;
        if (r == 2u) tid = 0x22u;
        rec.ewram[terrain_tiles + toff] = tid;
      }
    }

    // Populate Blue unit 1 and Red unit 0x81 in EWRAM.
    const uint32_t u1_off = fe7::GetUnit(1).addr - 0x02000000u;
    const uint32_t e1_off = fe7::GetUnit(0x81).addr - 0x02000000u;
    PopulateSyntheticUnit(rec.ewram, u1_off, rng);
    PopulateSyntheticUnit(rec.ewram, e1_off, rng);
    fe7::Unit u1{};
    std::memcpy(&u1, rec.ewram.data() + u1_off, sizeof(u1));
    u1.id = 1;
    u1.x = static_cast<int8_t>(NextXorshift(rng) % kW);
    u1.y = static_cast<int8_t>(NextXorshift(rng) % kH);
    std::memcpy(rec.ewram.data() + u1_off, &u1, sizeof(u1));

    fe7::Unit e1{};
    std::memcpy(&e1, rec.ewram.data() + e1_off, sizeof(e1));
    e1.id = static_cast<int8_t>(0x81);
    e1.x = static_cast<int8_t>(NextXorshift(rng) % kW);
    e1.y = static_cast<int8_t>(NextXorshift(rng) % kH);
    std::memcpy(rec.ewram.data() + e1_off, &e1, sizeof(e1));

    // Fog vision (+0x0D), chapter (+0x0E), faction (+0x0F).
    rec.ewram[(fe7::kMapPlayStatusAddr - 0x02000000u) + 0x0Du] =
        (NextXorshift(rng) & 1u) ? 3u : 0u;
    rec.ewram[(fe7::kMapPlayStatusAddr - 0x02000000u) + 0x0Eu] =
        static_cast<uint8_t>(NextXorshift(rng) % 8u);
    rec.ewram[(fe7::kMapPlayStatusAddr - 0x02000000u) + 0x0Fu] =
        (NextXorshift(rng) & 1u) ? fe7::kFactionBlue : fe7::kFactionRed;

    // Seed 5 varied traps in gTrapArray (terminated by slot 5 type == 0).
    const uint32_t trap_off = fe7::kTrapArrayAddr - 0x02000000u;
    const fe7::Trap seed_traps[5] = {
        {2, 3, static_cast<uint8_t>(fe7::TrapType::kFire), 0, {2, 3, 1, 10}},
        {4, 5, static_cast<uint8_t>(fe7::TrapType::kTorchlight), 2, {}},
        {5, 1, static_cast<uint8_t>(fe7::TrapType::kMine), 0, {}},
        {3,
         2,
         static_cast<uint8_t>(fe7::TrapType::kLightRune),
         1,
         {0, 0, 3, 0}},
        {0, 0, static_cast<uint8_t>(fe7::TrapType::kMapChange), 4, {}},
    };
    std::memcpy(rec.ewram.data() + trap_off, seed_traps, sizeof(seed_traps));

    const uint32_t tx = NextXorshift(rng) % kW;
    const uint32_t ty = NextXorshift(rng) % kH;
    switch (gba_addr) {
      case fe7::kAddrGetTrap:
        rec.regs[0] = NextXorshift(rng) % 10u;
        break;
      case fe7::kAddrGetTrapAt:
      case fe7::kAddrGetTrapExtraAt:
      case fe7::kAddrAddTrap8:
      case fe7::kAddrAddLightRune:
        rec.regs[0] = (NextXorshift(rng) & 1u) ? 2u : tx;
        rec.regs[1] = (NextXorshift(rng) & 1u) ? 3u : ty;
        break;
      case fe7::kAddrGetSpecificTrapAt:
        rec.regs[0] = 2u;
        rec.regs[1] = 3u;
        rec.regs[2] = (NextXorshift(rng) & 1u) ? 4u : 5u;
        break;
      case fe7::kAddrAddTrap:
        rec.regs[0] = tx;
        rec.regs[1] = ty;
        rec.regs[2] = 1u + (NextXorshift(rng) % 11u);
        rec.regs[3] = NextXorshift(rng) % 20u;
        break;
      case fe7::kAddrRemoveTrap:
        rec.regs[0] = fe7::kTrapArrayAddr + (NextXorshift(rng) % 5u) * 8u;
        break;
      case fe7::kAddrAddFireTrap:
      case fe7::kAddrAddTrap6:
        rec.regs[0] = tx;
        rec.regs[1] = ty;
        rec.regs[2] = 1u + (NextXorshift(rng) % 4u);
        rec.regs[3] = 1u + (NextXorshift(rng) % 4u);
        break;
      case fe7::kAddrAddArrowTrap:
        rec.regs[0] = tx;
        rec.regs[1] = 1u + (NextXorshift(rng) % 4u);
        rec.regs[2] = 1u + (NextXorshift(rng) % 4u);
        break;
      case fe7::kAddrAddTrap9:
        rec.regs[0] = tx;
        rec.regs[1] = ty;
        rec.regs[2] = NextXorshift(rng) % 10u;
        break;
      case fe7::kAddrAddMapChangeTrap:
      case fe7::kAddrRemoveMapChangeTrap:
        rec.regs[0] = (NextXorshift(rng) & 1u) ? 4u : 7u;
        break;
      case fe7::kAddrUnitHideIfUnderRoof:
        rec.regs[0] = 0x02000000u + u1_off;
        break;
      default:
        break;
    }

    out.push_back(std::move(rec));
  }
  return out;
}

// Synthesizes valid units, rescue partners, traps, gMapUnit, and stat boosters
// for all 26 Unit Action functions.
std::vector<fe7::TraceRecord> MakeActionTraces(uint32_t gba_addr,
                                               size_t count) {
  std::vector<fe7::TraceRecord> out;
  out.reserve(count);
  uint32_t rng = 0x4143544Eu ^ gba_addr;
  for (size_t i = 0; i < count; ++i) {
    fe7::TraceRecord rec;
    rec.gba_addr = gba_addr;
    rec.ewram.assign(fe7::kEwramSize, 0u);
    rec.iwram.assign(fe7::kIwramSize, 0u);
    std::memcpy(rec.iwram.data(), fe7::gMem.iwram, fe7::kIwramSize);

    constexpr int kW = 8;
    constexpr int kH = 8;
    const fe7::MapSize sz{kW, kH};
    std::memcpy(rec.ewram.data() + (fe7::kMapSizeAddr - 0x02000000u), &sz,
                sizeof(sz));
    InitSyntheticMapBuffer(rec.ewram, 0x11000u, fe7::kMapUnitAddr, kW, kH,
                           0x00u);

    // Populate Blue 1, Blue 2, Blue 3, and Red 0x81 in EWRAM.
    const uint32_t u1_off = fe7::GetUnit(1).addr - 0x02000000u;
    const uint32_t u2_off = fe7::GetUnit(2).addr - 0x02000000u;
    const uint32_t u3_off = fe7::GetUnit(3).addr - 0x02000000u;
    const uint32_t e1_off = fe7::GetUnit(0x81).addr - 0x02000000u;
    PopulateSyntheticUnit(rec.ewram, u1_off, rng);
    PopulateSyntheticUnit(rec.ewram, u2_off, rng);
    PopulateSyntheticUnit(rec.ewram, u3_off, rng);
    PopulateSyntheticUnit(rec.ewram, e1_off, rng);

    fe7::Unit u1{};
    std::memcpy(&u1, rec.ewram.data() + u1_off, sizeof(u1));
    u1.id = 1;
    u1.x = static_cast<int8_t>(NextXorshift(rng) % kW);
    u1.y = static_cast<int8_t>(NextXorshift(rng) % kH);
    u1.level = static_cast<int8_t>(1 + (NextXorshift(rng) % 10u));
    u1.rescue = 2u;  // Carrying Blue 2 for Drop/Give/Sync tests.
    u1.ballista_trap = 0u;
    if (gba_addr == fe7::kAddrApplyItemStatBoost) {
      static constexpr uint8_t kBoosters[10] = {0x5A, 0x5B, 0x5C, 0x5D, 0x5E,
                                                0x5F, 0x60, 0x61, 0x62, 0x88};
      u1.items[0] =
          static_cast<uint16_t>((1u << 8) | kBoosters[NextXorshift(rng) % 10u]);
    } else if (u1.items[0] == 0u) {
      u1.items[0] = 0x1401u;
    }
    std::memcpy(rec.ewram.data() + u1_off, &u1, sizeof(u1));

    fe7::Unit u2{};
    std::memcpy(&u2, rec.ewram.data() + u2_off, sizeof(u2));
    u2.id = 2;
    u2.rescue = 1u;
    std::memcpy(rec.ewram.data() + u2_off, &u2, sizeof(u2));

    fe7::Unit u3{};
    std::memcpy(&u3, rec.ewram.data() + u3_off, sizeof(u3));
    u3.id = 3;
    std::memcpy(rec.ewram.data() + u3_off, &u3, sizeof(u3));

    fe7::Unit e1{};
    std::memcpy(&e1, rec.ewram.data() + e1_off, sizeof(e1));
    e1.id = static_cast<int8_t>(0x81);
    std::memcpy(rec.ewram.data() + e1_off, &e1, sizeof(e1));

    rec.ewram[(fe7::kActionPlayStatusAddr - 0x02000000u) + 0x0Fu] =
        (NextXorshift(rng) & 1u) ? fe7::kFactionBlue : fe7::kFactionRed;

    constexpr uint32_t kUdefOff = 0x18300u;
    rec.ewram[kUdefOff + 3u] = static_cast<uint8_t>(NextXorshift(rng) & 1u);

    const uint32_t u1_addr = 0x02000000u + u1_off;
    const uint32_t u2_addr = 0x02000000u + u2_off;
    const uint32_t u3_addr = 0x02000000u + u3_off;
    const uint32_t e1_addr = 0x02000000u + e1_off;

    switch (gba_addr) {
      case fe7::kAddrFindUnitItemSlot:
      case fe7::kAddrUnitHasItem:
        rec.regs[0] = u1_addr;
        rec.regs[1] = (NextXorshift(rng) & 1u)
                          ? (u1.items[0] & 0xFFu)
                          : (1u + (NextXorshift(rng) % 0x80u));
        break;
      case fe7::kAddrUnitInitStats:
        rec.regs[0] = u1_addr;
        rec.regs[1] = u1.pinfo.addr;
        break;
      case fe7::kAddrUnitApplyAltPinfo:
      case fe7::kAddrUnitInitSupports:
      case fe7::kAddrUnitAutolevelPlayer:
      case fe7::kAddrUnitSyncMovement:
      case fe7::kAddrUnitBeginAction:
      case fe7::kAddrUnitBeginReMoveAction:
      case fe7::kAddrGetUnitUseFlags:
      case fe7::kAddrGetUnitLastItem:
        rec.regs[0] = u1_addr;
        break;
      case fe7::kAddrKillUnit:
        rec.regs[0] = (NextXorshift(rng) & 1u) ? u1_addr : e1_addr;
        break;
      case fe7::kAddrUnitAutolevelWeaponExp:
        rec.regs[0] = u1_addr;
        rec.regs[1] = 0x02000000u + kUdefOff;
        break;
      case fe7::kAddrUnitAutolevelCore:
        rec.regs[0] = u1_addr;
        rec.regs[1] = u1.jinfo->id;
        rec.regs[2] = NextXorshift(rng) % 6u;
        break;
      case fe7::kAddrUnitApplyBonusLevels:
        rec.regs[0] = u1_addr;
        rec.regs[1] = NextXorshift(rng) % 5u;
        break;
      case fe7::kAddrGetUnitByPidInFaction:
        rec.regs[0] = (NextXorshift(rng) & 1u) ? u1.pinfo->id : 0x30u;
        rec.regs[1] = fe7::kFactionBlue;
        break;
      case fe7::kAddrCanUnitCarry:
      case fe7::kAddrUnitRescue:
        rec.regs[0] = u1_addr;
        rec.regs[1] = u2_addr;
        break;
      case fe7::kAddrUnitGiveRescue:
        rec.regs[0] = u1_addr;
        rec.regs[1] = u3_addr;
        break;
      case fe7::kAddrUnitDropRescue:
      case fe7::kAddrUnitRevealAtPosition:
        rec.regs[0] = u1_addr;
        rec.regs[1] = NextXorshift(rng) % kW;
        rec.regs[2] = NextXorshift(rng) % kH;
        break;
      case fe7::kAddrUnitUpdateUsedItem:
      case fe7::kAddrApplyItemStatBoost:
        rec.regs[0] = u1_addr;
        rec.regs[1] = 0u;
        break;
      case fe7::kAddrGetUnitKeyItemSlotForTerrain:
        rec.regs[0] = u1_addr;
        rec.regs[1] = (NextXorshift(rng) & 1u) ? 0x1Eu : 0x21u;
        break;
      default:
        break;
    }

    out.push_back(std::move(rec));
  }
  return out;
}

// Synthesizes valid 8x8 padded maps (with 4-tile padding on gMapUnit for
// nearby-offset table scans), live units, AI decision state, play-status
// faction/chapter fields, and scratch buffers for top-level Enemy AI functions.
std::vector<fe7::TraceRecord> MakeAiDecideTraces(uint32_t gba_addr,
                                                 size_t count) {
  std::vector<fe7::TraceRecord> out;
  out.reserve(count);
  uint32_t rng = 0xA1DEC1DEu ^ gba_addr;
  for (size_t i = 0; i < count; ++i) {
    fe7::TraceRecord rec;
    rec.gba_addr = gba_addr;
    rec.ewram.assign(fe7::gMem.ewram, fe7::gMem.ewram + fe7::kEwramSize);
    rec.iwram.assign(fe7::gMem.iwram, fe7::gMem.iwram + fe7::kIwramSize);

    constexpr int kW = 8;
    constexpr int kH = 8;
    const fe7::MapSize sz{kW, kH};
    std::memcpy(rec.ewram.data() + (fe7::kMapSizeAddr - 0x02000000u), &sz,
                sizeof(sz));

    constexpr uint32_t kTerrainBufOff = 0x10000u;
    constexpr uint32_t kUnitMapBufOff = 0x11000u;
    constexpr uint32_t kMovMapBufOff = 0x12000u;
    constexpr uint32_t kRngMapBufOff = 0x13000u;
    constexpr uint32_t kOtherMapBufOff = 0x14000u;
    constexpr uint32_t kScratchOff = 0x16000u;
    const uint32_t scratch_addr = 0x02000000u + kScratchOff;

    InitSyntheticMapBuffer(rec.ewram, kTerrainBufOff, fe7::kMapTerrainAddr, kW,
                           kH, 0x00u);
    InitSyntheticMapBuffer(rec.ewram, kUnitMapBufOff, fe7::kMapUnitAddr, kW, kH,
                           0x00u, /*pad_rows=*/4, /*pad_cols=*/4);
    const uint32_t mov_map_addr = InitSyntheticMapBuffer(
        rec.ewram, kMovMapBufOff, fe7::kMapMovementAddr, kW, kH, 0x02u);
    InitSyntheticMapBuffer(rec.ewram, kRngMapBufOff, fe7::kMapRangeAddr, kW, kH,
                           0x01u);
    InitSyntheticMapBuffer(rec.ewram, kOtherMapBufOff, fe7::kAiMapOtherAddr, kW,
                           kH, 0x00u);
    std::memcpy(rec.iwram.data() + (fe7::kWorkingMapAddr - 0x03000000u),
                &mov_map_addr, sizeof(mov_map_addr));

    const uint32_t rows = kH + 4u;
    const uint32_t stride = kW + 2u;
    const uint32_t unit_rows = kH + 8u;
    const uint32_t unit_stride = kW + 8u;
    const uint32_t terrain_tiles = kTerrainBufOff + rows * 4u;
    const uint32_t unit_tiles = kUnitMapBufOff + unit_rows * 4u;
    const uint32_t mov_tiles = kMovMapBufOff + rows * 4u;
    const uint32_t rng_tiles = kRngMapBufOff + rows * 4u;
    const uint32_t other_tiles = kOtherMapBufOff + rows * 4u;

    for (int y = 0; y < kH; ++y) {
      for (int x = 0; x < kW; ++x) {
        const uint32_t toff = static_cast<uint32_t>(y + 2) * stride +
                              static_cast<uint32_t>(x + 1);
        const uint32_t utoff = static_cast<uint32_t>(y + 4) * unit_stride +
                               static_cast<uint32_t>(x + 4);
        rec.ewram[terrain_tiles + toff] =
            static_cast<uint8_t>(1u + (NextXorshift(rng) % 2u));
        const uint32_t u = NextXorshift(rng) % 10u;
        if (u == 0u) rec.ewram[unit_tiles + utoff] = 0x01u;
        if (u == 1u) rec.ewram[unit_tiles + utoff] = 0x81u;
        if (u == 2u) rec.ewram[unit_tiles + utoff] = 0x82u;
        rec.ewram[mov_tiles + toff] =
            (NextXorshift(rng) & 1u)
                ? static_cast<uint8_t>(NextXorshift(rng) % 10u)
                : 0xFFu;
        rec.ewram[rng_tiles + toff] =
            (NextXorshift(rng) & 1u)
                ? static_cast<uint8_t>(NextXorshift(rng) & 0xFFu)
                : 0u;
        rec.ewram[other_tiles + toff] =
            static_cast<uint8_t>(NextXorshift(rng) & 0xFFu);
      }
    }

    // Populate three live units: enemy 0x81 (active), enemy 0x82 (ally), and
    // player 0x01 (non-allied).
    const uint32_t unit_a_off = fe7::GetUnit(0x81).addr - 0x02000000u;
    const uint32_t unit_a2_off = fe7::GetUnit(0x82).addr - 0x02000000u;
    const uint32_t unit_b_off = fe7::GetUnit(0x01).addr - 0x02000000u;
    PopulateSyntheticUnit(rec.ewram, unit_a_off, rng);
    PopulateSyntheticUnit(rec.ewram, unit_a2_off, rng);
    PopulateSyntheticUnit(rec.ewram, unit_b_off, rng);

    auto fixup_unit = [&](uint32_t off, int8_t uid) {
      fe7::Unit u{};
      std::memcpy(&u, rec.ewram.data() + off, sizeof(u));
      u.id = uid;
      u.x = static_cast<int8_t>(1 + (NextXorshift(rng) % (kW - 2)));
      u.y = static_cast<int8_t>(1 + (NextXorshift(rng) % (kH - 2)));
      if (u.max_hp <= 0) u.max_hp = 20;
      u.hp = static_cast<int8_t>(
          1 + (NextXorshift(rng) % static_cast<uint32_t>(u.max_hp)));
      u.ai_config = static_cast<uint16_t>(NextXorshift(rng) & 3u);
      u.ai_flags = static_cast<uint8_t>(NextXorshift(rng) & 0x1Fu);
      std::memcpy(rec.ewram.data() + off, &u, sizeof(u));
    };
    fixup_unit(unit_a_off, static_cast<int8_t>(0x81));
    fixup_unit(unit_a2_off, static_cast<int8_t>(0x82));
    fixup_unit(unit_b_off, 0x01);

    rec.ewram[fe7::kAiActiveUnitIdAddr - 0x02000000u] = 0x81u;
    const uint32_t ua_addr = 0x02000000u + unit_a_off;
    const uint32_t ub_addr = 0x02000000u + unit_b_off;
    std::memcpy(rec.iwram.data() + (fe7::kAiActiveUnitAddr - 0x03000000u),
                &ua_addr, sizeof(ua_addr));

    // Seed gAiDecision with an in-bounds move coordinate.
    fe7::AiDecision dec{};
    dec.action_id = static_cast<uint8_t>(NextXorshift(rng) % 4u);
    dec.x_move = static_cast<uint8_t>(NextXorshift(rng) % kW);
    dec.y_move = static_cast<uint8_t>(NextXorshift(rng) % kH);
    std::memcpy(rec.ewram.data() + (fe7::kAiDecisionAddr - 0x02000000u), &dec,
                sizeof(dec));

    // Seed gPlayStatus chapter (0 or 8) and faction (0x00, 0x40, or 0x80).
    static constexpr uint8_t kFactions[3] = {0x00u, 0x40u, 0x80u};
    rec.ewram[fe7::kAiPlayStatusAddr + 0x0Eu - 0x02000000u] =
        (NextXorshift(rng) & 1u) ? 8u : 0u;
    rec.ewram[fe7::kAiPlayStatusAddr + 0x0Fu - 0x02000000u] =
        kFactions[NextXorshift(rng) % 3u];

    // Seed scratch buffer with plausible item ranks / comparison byte.
    for (int s = 0; s < 5; ++s) {
      const uint16_t rk = static_cast<uint16_t>(
          ((NextXorshift(rng) % 15u) << 8) | (NextXorshift(rng) & 0x0Fu));
      std::memcpy(rec.ewram.data() + kScratchOff + static_cast<uint32_t>(s) * 2,
                  &rk, sizeof(rk));
    }

    switch (gba_addr) {
      case fe7::kAddrAiCompare:
        rec.regs[0] = scratch_addr;
        rec.regs[1] = NextXorshift(rng) % 7u;
        rec.regs[2] = NextXorshift(rng) & 0xFFu;
        break;
      case fe7::kAddrAiTryFindNearestUnitOfClass:
        rec.regs[0] = 1u + (NextXorshift(rng) % 0x40u);
        rec.regs[1] = scratch_addr;
        break;
      case fe7::kAddrAiGetMoveCostAt:
      case fe7::kAddrAiCountUnitsNearby:
      case fe7::kAddrAiCountEnemiesNearby:
      case fe7::kAddrAiCountAlliesNearby:
      case fe7::kAddrAiTryDanceOrPlay:
        rec.regs[0] = NextXorshift(rng) % kW;
        rec.regs[1] = NextXorshift(rng) % kH;
        break;
      case fe7::kAddrAiIsWithinDangerThreshold:
        rec.regs[0] = NextXorshift(rng) % kW;
        rec.regs[1] = NextXorshift(rng) % kH;
        rec.regs[2] = NextXorshift(rng) & 0xFFu;
        break;
      case fe7::kAddrAiUpdateUnitRecoveryMode:
        rec.regs[0] = (NextXorshift(rng) & 1u) ? ua_addr : ub_addr;
        break;
      case fe7::kAddrAiRankUnitItems:
        rec.regs[0] = scratch_addr;
        break;
      case fe7::kAddrAiEquipBestItemWithMask:
        rec.regs[0] = 1u + (NextXorshift(rng) % 3u);
        rec.regs[1] = scratch_addr;
        break;
      case fe7::kAddrAiChooseBestItemToEquip:
        rec.regs[0] = NextXorshift(rng) % 40u;
        rec.regs[1] = NextXorshift(rng) % 40u;
        rec.regs[2] = rec.regs[0] + rec.regs[1];
        rec.regs[3] = scratch_addr;
        break;
      case fe7::kAddrAiUpdateUnitSpecialStaffFlags: {
        const uint32_t iid = 0x48u + (NextXorshift(rng) % 0x12u);
        rec.regs[0] = iid | ((NextXorshift(rng) % 20u) << 8);
        break;
      }
      case fe7::kAddrAiIsWithinMoveBirdsEye:
        rec.regs[0] = (NextXorshift(rng) & 1u) ? ua_addr : ub_addr;
        rec.regs[1] = NextXorshift(rng) % kW;
        rec.regs[2] = NextXorshift(rng) % kH;
        break;
      case fe7::kAddrAiGetUnitAttackPower:
        rec.regs[0] = (NextXorshift(rng) & 1u) ? ua_addr : ub_addr;
        rec.regs[1] = scratch_addr;
        break;
      default:
        break;
    }

    out.push_back(std::move(rec));
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  absl::SetProgramUsageMessage(
      "Differential oracle verifier for decompiled FE7 native functions.");
  absl::ParseCommandLine(argc, argv);

  const std::string rom_path = absl::GetFlag(FLAGS_rom);
  if (rom_path.empty()) {
    std::cerr << "Error: --rom is required.\n";
    return 1;
  }

  auto rom = fe7::LoadRomFile(rom_path);
  if (!rom.ok()) {
    std::cerr << "Error: " << rom.status().message() << "\n";
    return 1;
  }

  std::vector<fe7::TraceRecord> loaded_traces;
  const std::string trace_path = absl::GetFlag(FLAGS_traces);
  if (!trace_path.empty()) {
    auto traces_or = fe7::ReadTraceFile(trace_path);
    if (!traces_or.ok()) {
      std::cerr << "Error: " << traces_or.status().message() << "\n";
      return 1;
    }
    loaded_traces = std::move(*traces_or);
  }

  if (!fe7::hal::Init(*rom)) {
    std::cerr << "Error: failed to initialize GbaHal\n";
    return 1;
  }

  // Step a few frames so the GBA stack and IWRAM/EWRAM are initialized by the
  // retail ROM's crt0 and main loop before running differential checks.
  for (int f = 0; f < 5; ++f) {
    fe7::hal::StepFrame(0);
  }

  fe7::RegisterAllNativeFunctions();
  const size_t fuzz_iters =
      static_cast<size_t>(absl::GetFlag(FLAGS_fuzz_iters));

  int failures = 0;
  for (const VerifiedEntry& entry : kVerifiedFunctions) {
    fe7::NativeFn fn = fe7::LookupNative(entry.gba_addr);
    if (fn == nullptr) {
      std::cerr << "FAIL " << entry.name << ": not registered\n";
      ++failures;
      continue;
    }

    fe7::DiffOptions opts;
    opts.check_return_value = entry.check_return_value;

    fe7::DiffResult res;
    switch (entry.fuzz_mode) {
      case FuzzMode::kScalarRegs:
        res = fe7::DiffFunction(entry.gba_addr, fn, loaded_traces, fuzz_iters,
                                0xFE700001u ^ entry.gba_addr, opts);
        break;
      case FuzzMode::kPtrU16x3InR0: {
        auto ptr_traces = MakePtrBufferTraces(entry.gba_addr, fuzz_iters);
        res = fe7::DiffFunction(entry.gba_addr, fn, ptr_traces, 0, 0xFE700001u,
                                opts);
        break;
      }
      case FuzzMode::kFormatTimePtrs: {
        auto fmt_traces = MakeFormatTimeTraces(entry.gba_addr, fuzz_iters);
        res = fe7::DiffFunction(entry.gba_addr, fn, fmt_traces, 0, 0xFE700001u,
                                opts);
        break;
      }
      case FuzzMode::kUnitPtrInR0: {
        auto unit_traces = MakeUnitTraces(entry.gba_addr, fuzz_iters, false);
        res = fe7::DiffFunction(entry.gba_addr, fn, unit_traces, 0, 0xFE700001u,
                                opts);
        break;
      }
      case FuzzMode::kUnitCopyPtrs: {
        auto unit_traces = MakeUnitTraces(entry.gba_addr, fuzz_iters, true);
        res = fe7::DiffFunction(entry.gba_addr, fn, unit_traces, 0, 0xFE700001u,
                                opts);
        break;
      }
      case FuzzMode::kValidItemInR0: {
        auto item_traces = MakeValidItemTraces(entry.gba_addr, fuzz_iters);
        res = fe7::DiffFunction(entry.gba_addr, fn, item_traces, 0, 0xFE700001u,
                                opts);
        break;
      }
      case FuzzMode::kUnitAndItem:
      case FuzzMode::kUnitAndSlot:
      case FuzzMode::kItemAndUnit: {
        auto ui_traces =
            MakeUnitItemTraces(entry.gba_addr, fuzz_iters, entry.fuzz_mode);
        res = fe7::DiffFunction(entry.gba_addr, fn, ui_traces, 0, 0xFE700001u,
                                opts);
        break;
      }
      case FuzzMode::kBattlePair: {
        auto battle_traces = MakeBattleUnitTraces(entry.gba_addr, fuzz_iters);
        res = fe7::DiffFunction(entry.gba_addr, fn, battle_traces, 0,
                                0xFE700001u, opts);
        break;
      }
      case FuzzMode::kBattleSim: {
        auto sim_traces = MakeBattleSimTraces(entry.gba_addr, fuzz_iters);
        res = fe7::DiffFunction(entry.gba_addr, fn, sim_traces, 0, 0xFE700001u,
                                opts);
        break;
      }
      case FuzzMode::kBattleExp: {
        auto exp_traces = MakeBattleExpTraces(entry.gba_addr, fuzz_iters);
        res = fe7::DiffFunction(entry.gba_addr, fn, exp_traces, 0, 0xFE700001u,
                                opts);
        break;
      }
      case FuzzMode::kMapState: {
        auto map_traces = MakeMapTraces(entry.gba_addr, fuzz_iters);
        res = fe7::DiffFunction(entry.gba_addr, fn, map_traces, 0, 0xFE700001u,
                                opts);
        break;
      }
      case FuzzMode::kPhaseState: {
        auto phase_traces = MakePhaseTraces(entry.gba_addr, fuzz_iters);
        res = fe7::DiffFunction(entry.gba_addr, fn, phase_traces, 0,
                                0xFE700001u, opts);
        break;
      }
      case FuzzMode::kSupportState: {
        auto sup_traces = MakeSupportTraces(entry.gba_addr, fuzz_iters);
        res = fe7::DiffFunction(entry.gba_addr, fn, sup_traces, 0, 0xFE700001u,
                                opts);
        break;
      }
      case FuzzMode::kAiState: {
        auto ai_traces = MakeAiTraces(entry.gba_addr, fuzz_iters);
        res = fe7::DiffFunction(entry.gba_addr, fn, ai_traces, 0, 0xFE700001u,
                                opts);
        break;
      }
      case FuzzMode::kTrapState: {
        auto trap_traces = MakeTrapTraces(entry.gba_addr, fuzz_iters);
        res = fe7::DiffFunction(entry.gba_addr, fn, trap_traces, 0, 0xFE700001u,
                                opts);
        break;
      }
      case FuzzMode::kActionState: {
        auto act_traces = MakeActionTraces(entry.gba_addr, fuzz_iters);
        res = fe7::DiffFunction(entry.gba_addr, fn, act_traces, 0, 0xFE700001u,
                                opts);
        break;
      }
      case FuzzMode::kAiDecideState: {
        auto decide_traces = MakeAiDecideTraces(entry.gba_addr, fuzz_iters);
        res = fe7::DiffFunction(entry.gba_addr, fn, decide_traces, 0,
                                0xFE700001u, opts);
        break;
      }
      case FuzzMode::kTracesOnly:
        res = fe7::DiffFunction(entry.gba_addr, fn, loaded_traces, 0,
                                0xFE700001u, opts);
        break;
    }

    if (res.ok) {
      std::cout << "PASS " << entry.name << " (0x" << std::hex << entry.gba_addr
                << std::dec << "): traces=" << res.traces_checked
                << " fuzz=" << res.fuzz_checked << "\n";
    } else {
      std::cerr << "FAIL " << entry.name << ": " << res.failure_detail << "\n";
      ++failures;
    }
  }

  fe7::ResetDispatchTable();
  fe7::hal::Shutdown();
  return failures == 0 ? 0 : 1;
}
