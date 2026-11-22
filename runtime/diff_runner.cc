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
#include "game/battle.h"
#include "game/battle/battle.h"
#include "game/battle/battle_sim.h"
#include "game/engine/clock.h"
#include "game/engine/proc.h"
#include "game/engine/rng.h"
#include "game/item/item.h"
#include "game/map/map.h"
#include "game/register_all.h"
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
  kMapState,
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
                                uint8_t fill_byte) {
  const uint32_t buf_addr = 0x02000000u + buf_off;
  const uint32_t rows = static_cast<uint32_t>(height + 4);
  const uint32_t stride = static_cast<uint32_t>(width + 2);
  const uint32_t tile_base_off = buf_off + rows * 4u;
  std::memset(ewram.data() + tile_base_off, fill_byte, rows * stride);
  for (uint32_t r = 0; r < rows; ++r) {
    const uint32_t row_addr = 0x02000000u + tile_base_off + r * stride + 1u;
    std::memcpy(ewram.data() + buf_off + r * 4u, &row_addr, sizeof(row_addr));
  }
  const uint32_t map_addr = buf_addr + 8u;
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
      case FuzzMode::kMapState: {
        auto map_traces = MakeMapTraces(entry.gba_addr, fuzz_iters);
        res = fe7::DiffFunction(entry.gba_addr, fn, map_traces, 0, 0xFE700001u,
                                opts);
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
