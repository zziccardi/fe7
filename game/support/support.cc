// FE7 US (AE7E) support relationships, EXP accumulation, and affinity bonuses.

#include "game/support/support.h"

#include <cstdint>

#include "absl/base/nullability.h"
#include "game/unit.h"
#include "game/unit/unit.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

constexpr uint32_t kSupportSkipMask = 0x0001000Cu;
constexpr uint32_t kSupportBonusSkipMask = 0x0001002Cu;
constexpr uint32_t kUnitFlagRescuedBit = 0x00000020u;

int AbsDiff(int a, int b) { return a >= b ? (a - b) : (b - a); }

// --- Dispatch thunks ---

uint32_t ThunkGetUnitSupporterCount(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                    const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(GetUnitSupporterCount(GbaPtr<const Unit>(r0)));
}

uint32_t ThunkGetUnitSupporterCharacter(uint32_t r0, uint32_t r1, uint32_t,
                                        uint32_t,
                                        const uint32_t* absl_nullable) {
  return GetUnitSupporterCharacter(GbaPtr<const Unit>(r0),
                                   static_cast<int32_t>(r1));
}

uint32_t ThunkGetUnitSupporterUnit(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                                   const uint32_t* absl_nullable) {
  return GetUnitSupporterUnit(GbaPtr<const Unit>(r0), static_cast<int32_t>(r1))
      .addr;
}

uint32_t ThunkGetSupportLevelBySupportIndex(uint32_t r0, uint32_t r1, uint32_t,
                                            uint32_t,
                                            const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(GetSupportLevelBySupportIndex(
      GbaPtr<const Unit>(r0), static_cast<int32_t>(r1)));
}

uint32_t ThunkGetUnitTotalSupportLevel(uint32_t r0, uint32_t, uint32_t,
                                       uint32_t,
                                       const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(
      GetUnitTotalSupportLevel(GbaPtr<const Unit>(r0)));
}

uint32_t ThunkUnitGainSupportExp(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                                 const uint32_t* absl_nullable) {
  UnitGainSupportExp(GbaPtr<Unit>(r0), static_cast<int32_t>(r1));
  return 0;
}

uint32_t ThunkUnitGainSupportLevel(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                                   const uint32_t* absl_nullable) {
  UnitGainSupportLevel(GbaPtr<Unit>(r0), static_cast<int32_t>(r1));
  return 0;
}

uint32_t ThunkCanUnitSupportNow(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                                const uint32_t* absl_nullable) {
  return CanUnitSupportNow(GbaPtr<const Unit>(r0), static_cast<int32_t>(r1))
             ? 1u
             : 0u;
}

uint32_t ThunkGetUnitSupporterInitialExp(uint32_t r0, uint32_t r1, uint32_t,
                                         uint32_t,
                                         const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(GetUnitSupporterInitialExp(
      GbaPtr<const Unit>(r0), static_cast<int32_t>(r1)));
}

uint32_t ThunkGetUnitSupporterNum(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                                  const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(
      GetUnitSupporterNum(GbaPtr<const Unit>(r0), static_cast<uint8_t>(r1)));
}

uint32_t ThunkClearUnitSupports(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                const uint32_t* absl_nullable) {
  ClearUnitSupports(GbaPtr<Unit>(r0));
  return 0;
}

uint32_t ThunkProcessSupportGains(uint32_t, uint32_t, uint32_t, uint32_t,
                                  const uint32_t* absl_nullable) {
  ProcessSupportGains();
  return 0;
}

uint32_t ThunkGetAffinityBonusEntry(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                    const uint32_t* absl_nullable) {
  return GetAffinityBonusEntry(static_cast<int32_t>(r0)).addr;
}

uint32_t ThunkApplyAffinitySupportBonuses(uint32_t r0, uint32_t r1, uint32_t r2,
                                          uint32_t,
                                          const uint32_t* absl_nullable) {
  ApplyAffinitySupportBonuses(GbaPtr<SupportBonuses>(r0),
                              static_cast<int32_t>(r1),
                              static_cast<int32_t>(r2));
  return 0;
}

uint32_t ThunkInitSupportBonuses(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                 const uint32_t* absl_nullable) {
  InitSupportBonuses(GbaPtr<SupportBonuses>(r0));
  return 0;
}

uint32_t ThunkGetUnitSupportBonuses(uint32_t r0, uint32_t r1, uint32_t,
                                    uint32_t, const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(GetUnitSupportBonuses(
      GbaPtr<const Unit>(r0), GbaPtr<SupportBonuses>(r1)));
}

uint32_t ThunkGetUnitAffinityIcon(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                  const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(GetUnitAffinityIcon(GbaPtr<const Unit>(r0)));
}

uint32_t ThunkGetCharacterAffinityIcon(uint32_t r0, uint32_t, uint32_t,
                                       uint32_t,
                                       const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(
      GetCharacterAffinityIcon(static_cast<int32_t>(r0)));
}

uint32_t ThunkGetSupportLevelUiChar(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                    const uint32_t* absl_nullable) {
  return GetSupportLevelUiChar(static_cast<int32_t>(r0));
}

uint32_t ThunkSetSupportLevelGained(uint32_t r0, uint32_t r1, uint32_t,
                                    uint32_t, const uint32_t* absl_nullable) {
  SetSupportLevelGained(static_cast<uint8_t>(r0), static_cast<uint8_t>(r1));
  return 0;
}

uint32_t ThunkHasUnitGainedSupportLevel(uint32_t r0, uint32_t r1, uint32_t,
                                        uint32_t,
                                        const uint32_t* absl_nullable) {
  return HasUnitGainedSupportLevel(GbaPtr<const Unit>(r0),
                                   static_cast<int32_t>(r1))
             ? 1u
             : 0u;
}

uint32_t ThunkArePidsAtMaxSupport(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                                  const uint32_t* absl_nullable) {
  return ArePidsAtMaxSupport(static_cast<uint8_t>(r0), static_cast<uint8_t>(r1))
             ? 1u
             : 0u;
}

}  // namespace

int GetUnitSupporterCount(GbaPtr<const Unit> unit) {
  if (!unit->pinfo->support_info) {
    return 0;
  }
  return unit->pinfo->support_info->count;
}

uint8_t GetUnitSupporterCharacter(GbaPtr<const Unit> unit, int num) {
  if (!unit->pinfo->support_info) {
    return 0;
  }
  return unit->pinfo->support_info->pids[num];
}

GbaPtr<Unit> GetUnitSupporterUnit(GbaPtr<const Unit> unit, int num) {
  const uint8_t target_pid = GetUnitSupporterCharacter(unit, num);
  const int faction = static_cast<int>(static_cast<uint8_t>(unit->id) & 0xC0u);
  for (int id = faction + 1; id < faction + 0x40; ++id) {
    GbaPtr<Unit> other = GetUnit(id);
    if (!other || !other->pinfo) {
      continue;
    }
    if (other->pinfo->id == target_pid) {
      return other;
    }
  }
  return GbaPtr<Unit>(0);
}

int GetSupportLevelBySupportIndex(GbaPtr<const Unit> unit, int num) {
  const uint8_t exp = unit->supports[num];
  if (exp > 0xF0u) {
    return 3;
  }
  if (exp > 0xA0u) {
    return 2;
  }
  if (exp > 0x50u) {
    return 1;
  }
  return 0;
}

int GetUnitTotalSupportLevel(GbaPtr<const Unit> unit) {
  const int count = GetUnitSupporterCount(unit);
  int total = 0;
  for (int i = 0; i < count; ++i) {
    total += GetSupportLevelBySupportIndex(unit, i);
  }
  return total;
}

void UnitGainSupportExp(GbaPtr<Unit> unit, int num) {
  // Lyn Mode (`mode == 1`) disables support EXP accumulation.
  if (*GbaPtr<const uint8_t>(kSupportPlayStatusAddr + 0x1Bu) == 1u) {
    return;
  }
  if (!unit->pinfo->support_info) {
    return;
  }

  int growth = unit->pinfo->support_info->exp_growth[num];
  const int current = unit->supports[num];
  const int level =
      GetSupportLevelBySupportIndex(GbaPtr<const Unit>(unit.addr), num);
  const int cap =
      static_cast<int>(GbaPtr<const uint32_t>(kSupportLevelCapsAddr)[level]);

  if (current + growth > cap) {
    growth = cap - current;
  }

  unit->supports[num] = static_cast<uint8_t>(current + growth);
  GbaPtr<uint16_t> chapter_support(kSupportPlayStatusAddr + 0x16u);
  *chapter_support = static_cast<uint16_t>(*chapter_support + growth);
}

void UnitGainSupportLevel(GbaPtr<Unit> unit, int num) {
  unit->supports[num] = static_cast<uint8_t>(unit->supports[num] + 1u);
  GbaPtr<uint16_t> chapter_support(kSupportPlayStatusAddr + 0x16u);
  *chapter_support = static_cast<uint16_t>(*chapter_support + 1u);

  const uint8_t pid_a = unit->pinfo->id;
  const uint8_t pid_b =
      GetUnitSupporterCharacter(GbaPtr<const Unit>(unit.addr), num);
  SetSupportLevelGained(pid_a, pid_b);
}

bool CanUnitSupportNow(GbaPtr<const Unit> unit, int num) {
  const uint8_t flags = *GbaPtr<const uint8_t>(kSupportPlayStatusAddr + 0x14u);
  if ((flags & 0x80u) != 0u || (flags & 0x08u) != 0u) {
    return false;
  }
  if (HasUnitGainedSupportLevel(unit, num)) {
    return false;
  }
  if (GetUnitTotalSupportLevel(unit) > 4) {
    return false;
  }
  GbaPtr<Unit> partner = GetUnitSupporterUnit(unit, num);
  if (GetUnitTotalSupportLevel(GbaPtr<const Unit>(partner.addr)) > 4) {
    return false;
  }

  const uint8_t exp = unit->supports[num];
  const int level = GetSupportLevelBySupportIndex(unit, num);
  const uint32_t cap = GbaPtr<const uint32_t>(kSupportLevelCapsAddr)[level];
  if (exp == 0xF1u) {
    return false;
  }
  return exp == cap;
}

int GetUnitSupporterInitialExp(GbaPtr<const Unit> unit, int num) {
  if (!unit->pinfo->support_info) {
    return -1;
  }
  return unit->pinfo->support_info->exp_base[num];
}

int GetUnitSupporterNum(GbaPtr<const Unit> unit, uint8_t pid) {
  const int count = GetUnitSupporterCount(unit);
  for (int i = 0; i < count; ++i) {
    if (GetUnitSupporterCharacter(unit, i) == pid) {
      return i;
    }
  }
  return -1;
}

void ClearUnitSupports(GbaPtr<Unit> unit) {
  const int count = GetUnitSupporterCount(GbaPtr<const Unit>(unit.addr));
  for (int i = 0; i < count; ++i) {
    GbaPtr<Unit> partner =
        GetUnitSupporterUnit(GbaPtr<const Unit>(unit.addr), i);
    if (!partner) {
      continue;
    }
    const int back_idx =
        GetUnitSupporterNum(GbaPtr<const Unit>(partner.addr), unit->pinfo->id);
    partner->supports[back_idx] = 0;
    unit->supports[i] = 0;
  }
}

void ProcessSupportGains() {
  if (*GbaPtr<const uint16_t>(kSupportPlayStatusAddr + 0x10u) == 1u) {
    return;
  }
  if ((*GbaPtr<const uint8_t>(kSupportPlayStatusAddr + 0x14u) & 0x80u) != 0u) {
    return;
  }

  for (int id = 1; id < 0x40; ++id) {
    GbaPtr<Unit> unit = GetUnit(id);
    if (!unit || !unit->pinfo) {
      continue;
    }
    if ((unit->flags & kSupportSkipMask) != 0u) {
      continue;
    }
    GbaPtr<const Unit> unit_c(unit.addr);
    if (GetUnitTotalSupportLevel(unit_c) > 4) {
      continue;
    }

    const int count = GetUnitSupporterCount(unit_c);
    for (int i = 0; i < count; ++i) {
      GbaPtr<Unit> partner = GetUnitSupporterUnit(unit_c, i);
      if (!partner) {
        continue;
      }
      if ((partner->flags & kSupportSkipMask) != 0u) {
        continue;
      }
      if ((static_cast<uint8_t>(partner->id) & 0xC0u) != 0u) {
        continue;
      }

      const int dist =
          AbsDiff(unit->x, partner->x) + AbsDiff(unit->y, partner->y);
      if (dist == 0) {
        if (unit->rescue != static_cast<uint8_t>(partner->id)) {
          continue;
        }
      } else if (dist == 1) {
        if ((unit->flags & kUnitFlagRescuedBit) != 0u ||
            (partner->flags & kUnitFlagRescuedBit) != 0u) {
          continue;
        }
      } else {
        continue;
      }

      if (GetUnitTotalSupportLevel(GbaPtr<const Unit>(partner.addr)) > 4) {
        continue;
      }
      UnitGainSupportExp(unit, i);
    }
  }
}

GbaPtr<const SupportBonuses> GetAffinityBonusEntry(int affinity) {
  GbaPtr<const SupportBonuses> entry(kAffinityBonusTableAddr);
  while (entry->affinity != 0u) {
    if (entry->affinity == affinity) {
      return entry;
    }
    entry = GbaPtr<const SupportBonuses>(entry.addr + sizeof(SupportBonuses));
  }
  return GbaPtr<const SupportBonuses>(0);
}

void ApplyAffinitySupportBonuses(GbaPtr<SupportBonuses> bonuses, int affinity,
                                 int level) {
  GbaPtr<const SupportBonuses> entry = GetAffinityBonusEntry(affinity);
  bonuses->bonus_attack =
      static_cast<uint8_t>(bonuses->bonus_attack + entry->bonus_attack * level);
  bonuses->bonus_defense = static_cast<uint8_t>(bonuses->bonus_defense +
                                                entry->bonus_defense * level);
  bonuses->bonus_hit =
      static_cast<uint8_t>(bonuses->bonus_hit + entry->bonus_hit * level);
  bonuses->bonus_avoid =
      static_cast<uint8_t>(bonuses->bonus_avoid + entry->bonus_avoid * level);
  bonuses->bonus_crit =
      static_cast<uint8_t>(bonuses->bonus_crit + entry->bonus_crit * level);
  bonuses->bonus_dodge =
      static_cast<uint8_t>(bonuses->bonus_dodge + entry->bonus_dodge * level);
}

void InitSupportBonuses(GbaPtr<SupportBonuses> bonuses) {
  bonuses->bonus_attack = 0;
  bonuses->bonus_defense = 0;
  bonuses->bonus_hit = 0;
  bonuses->bonus_avoid = 0;
  bonuses->bonus_crit = 0;
  bonuses->bonus_dodge = 0;
}

int GetUnitSupportBonuses(GbaPtr<const Unit> unit,
                          GbaPtr<SupportBonuses> bonuses) {
  int result = 0;
  InitSupportBonuses(bonuses);

  const int count = GetUnitSupporterCount(unit);
  const bool ignore_range =
      (*GbaPtr<const uint8_t>(kSupportBmStatusAddr + 0x04u) & 0x40u) != 0u;

  for (int i = 0; i < count; ++i) {
    result >>= 1;
    GbaPtr<Unit> partner = GetUnitSupporterUnit(unit, i);
    if (!partner) {
      continue;
    }
    if (!ignore_range) {
      const int dist =
          AbsDiff(unit->x, partner->x) + AbsDiff(unit->y, partner->y);
      if (dist > 3) {
        continue;
      }
    }
    if ((partner->flags & kSupportBonusSkipMask) != 0u) {
      continue;
    }

    GbaPtr<const Unit> partner_c(partner.addr);
    const int partner_idx = GetUnitSupporterNum(partner_c, unit->pinfo->id);
    const int partner_level =
        GetSupportLevelBySupportIndex(partner_c, partner_idx);
    ApplyAffinitySupportBonuses(bonuses, partner->pinfo->affinity,
                                partner_level);

    const int unit_level = GetSupportLevelBySupportIndex(unit, i);
    ApplyAffinitySupportBonuses(bonuses, unit->pinfo->affinity, unit_level);

    if (partner_level != 0 && unit_level != 0) {
      result += (1 << (count - 1));
    }
  }

  bonuses->bonus_attack >>= 1;
  bonuses->bonus_defense >>= 1;
  bonuses->bonus_hit >>= 1;
  bonuses->bonus_avoid >>= 1;
  bonuses->bonus_crit >>= 1;
  bonuses->bonus_dodge >>= 1;
  return result;
}

int GetUnitAffinityIcon(GbaPtr<const Unit> unit) {
  const uint8_t affinity = unit->pinfo->affinity;
  return affinity != 0u ? static_cast<int>(affinity) + 0x79 : -1;
}

int GetCharacterAffinityIcon(int pid) {
  const uint8_t affinity = GetPersonInfo(pid)->affinity;
  return affinity != 0u ? static_cast<int>(affinity) + 0x79 : -1;
}

uint8_t GetSupportLevelUiChar(int level) {
  static constexpr uint8_t kChars[4] = {0x14u, 0x1Bu, 0x1Au, 0x19u};
  return kChars[level];
}

void SetSupportLevelGained(uint8_t pid_a, uint8_t pid_b) {
  GbaPtr<Unit> unit_a = GetUnitByPid(pid_a);
  const int idx_b = GetUnitSupporterNum(GbaPtr<const Unit>(unit_a.addr), pid_b);
  unit_a->support_bits =
      static_cast<uint8_t>(unit_a->support_bits | (1u << idx_b));

  GbaPtr<Unit> unit_b = GetUnitByPid(pid_b);
  const int idx_a = GetUnitSupporterNum(GbaPtr<const Unit>(unit_b.addr), pid_a);
  unit_b->support_bits =
      static_cast<uint8_t>(unit_b->support_bits | (1u << idx_a));
}

bool HasUnitGainedSupportLevel(GbaPtr<const Unit> unit, int num) {
  return (unit->support_bits & (1u << num)) != 0u;
}

bool ArePidsAtMaxSupport(uint8_t pid_a, uint8_t pid_b) {
  GbaPtr<Unit> unit_a = GetUnitByPid(pid_a);
  GbaPtr<const Unit> unit_a_c(unit_a.addr);
  const int idx = GetUnitSupporterNum(unit_a_c, pid_b);
  return GetSupportLevelBySupportIndex(unit_a_c, idx) > 2;
}

void RegisterSupportFunctions() {
  RegisterNative(kAddrGetUnitSupporterCount, ThunkGetUnitSupporterCount);
  RegisterNative(kAddrGetUnitSupporterCharacter,
                 ThunkGetUnitSupporterCharacter);
  RegisterNative(kAddrGetUnitSupporterUnit, ThunkGetUnitSupporterUnit);
  RegisterNative(kAddrGetSupportLevelBySupportIndex,
                 ThunkGetSupportLevelBySupportIndex);
  RegisterNative(kAddrGetUnitTotalSupportLevel, ThunkGetUnitTotalSupportLevel);
  RegisterNative(kAddrUnitGainSupportExp, ThunkUnitGainSupportExp);
  RegisterNative(kAddrUnitGainSupportLevel, ThunkUnitGainSupportLevel);
  RegisterNative(kAddrCanUnitSupportNow, ThunkCanUnitSupportNow);
  RegisterNative(kAddrGetUnitSupporterInitialExp,
                 ThunkGetUnitSupporterInitialExp);
  RegisterNative(kAddrGetUnitSupporterNum, ThunkGetUnitSupporterNum);
  RegisterNative(kAddrClearUnitSupports, ThunkClearUnitSupports);
  RegisterNative(kAddrSupportProcessGains, ThunkProcessSupportGains);
  RegisterNative(kAddrGetAffinityBonusEntry, ThunkGetAffinityBonusEntry);
  RegisterNative(kAddrApplyAffinitySupportBonuses,
                 ThunkApplyAffinitySupportBonuses);
  RegisterNative(kAddrInitSupportBonuses, ThunkInitSupportBonuses);
  RegisterNative(kAddrGetUnitSupportBonuses, ThunkGetUnitSupportBonuses);
  RegisterNative(kAddrGetUnitAffinityIcon, ThunkGetUnitAffinityIcon);
  RegisterNative(kAddrGetCharacterAffinityIcon, ThunkGetCharacterAffinityIcon);
  RegisterNative(kAddrGetSupportLevelUiChar, ThunkGetSupportLevelUiChar);
  RegisterNative(kAddrSetSupportLevelGained, ThunkSetSupportLevelGained);
  RegisterNative(kAddrHasUnitGainedSupportLevel,
                 ThunkHasUnitGainedSupportLevel);
  RegisterNative(kAddrArePidsAtMaxSupport, ThunkArePidsAtMaxSupport);
}

}  // namespace fe7
