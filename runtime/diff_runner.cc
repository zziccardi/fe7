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
#include "game/engine/clock.h"
#include "game/engine/proc.h"
#include "game/engine/rng.h"
#include "game/item/item.h"
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
