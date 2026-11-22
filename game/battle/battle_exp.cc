// FE7 US (AE7E) post-battle EXP, level-up stat rolls, WEXP, and unit updates.

#include "game/battle/battle_exp.h"

#include <cstdint>

#include "absl/base/nullability.h"
#include "game/battle.h"
#include "game/battle/battle.h"
#include "game/engine/rng.h"
#include "game/item.h"
#include "game/item/item.h"
#include "game/unit.h"
#include "game/unit/unit.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

constexpr uint32_t kAttrSteal = 0x00000008u;
constexpr uint32_t kAttrPromoted = 0x00000100u;
constexpr uint32_t kAttrBoss = 0x00008000u;
constexpr uint32_t kAttrNoExp = 0x01000000u;
constexpr uint32_t kAttrAssassin = 0x02000000u;

constexpr uint32_t kItemAttrWexpDisallowedMask = 0x00000440u;

constexpr int kWexpCapUnpromoted = 0xB5;  // A-rank (181)
constexpr int kWexpCapPreS = 0xFA;        // 250 (one below S-rank)
constexpr int kWexpCapS = 0xFB;           // S-rank (251)

GbaPtr<BattleStats> BattleStatsPtr() {
  return GbaPtr<BattleStats>(kBattleStatsAddr);
}

GbaPtr<BattleUnit> BattleActorPtr() {
  return GbaPtr<BattleUnit>(kBattleActorAddr);
}

GbaPtr<BattleUnit> BattleTargetPtr() {
  return GbaPtr<BattleUnit>(kBattleTargetAddr);
}

bool IsLinkArenaMode() {
  return (*GbaPtr<const uint8_t>(kPlayStatusAddr + 0x14u) & 0x80u) != 0u;
}

bool IsBmStatusBit6Set() {
  return (*GbaPtr<const uint8_t>(kBmStatusAddr + 0x04u) & 0x40u) != 0u;
}

uint32_t CombinedAttributes(GbaPtr<const Unit> unit) {
  return unit->pinfo->attributes | unit->jinfo->attributes;
}

// --- Dispatch thunks ---

uint32_t ThunkBattleApplyExpGains(uint32_t, uint32_t, uint32_t, uint32_t,
                                  const uint32_t* absl_nullable) {
  BattleApplyExpGains();
  return 0;
}

uint32_t ThunkGetStatIncrease(uint32_t r0, uint32_t, uint32_t, uint32_t,
                              const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(GetStatIncrease(static_cast<int32_t>(r0)));
}

uint32_t ThunkGetAutoleveledStatIncrease(uint32_t r0, uint32_t r1, uint32_t,
                                         uint32_t,
                                         const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(GetAutoleveledStatIncrease(
      static_cast<int32_t>(r0), static_cast<int32_t>(r1)));
}

uint32_t ThunkCanBattleUnitGainExp(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                   const uint32_t* absl_nullable) {
  return CanBattleUnitGainExp(GbaPtr<BattleUnit>(r0)) ? 1u : 0u;
}

uint32_t ThunkCheckBattleUnitLevelUp(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                     const uint32_t* absl_nullable) {
  CheckBattleUnitLevelUp(GbaPtr<BattleUnit>(r0));
  return 0;
}

uint32_t ThunkUnitPromote(uint32_t r0, uint32_t, uint32_t, uint32_t,
                          const uint32_t* absl_nullable) {
  UnitPromote(GbaPtr<Unit>(r0));
  return 0;
}

uint32_t ThunkGenerateBattleUnitStatGainsComparatively(
    uint32_t r0, uint32_t r1, uint32_t, uint32_t,
    const uint32_t* absl_nullable) {
  GenerateBattleUnitStatGainsComparatively(GbaPtr<BattleUnit>(r0),
                                           GbaPtr<const Unit>(r1));
  return 0;
}

uint32_t ThunkCheckBattleUnitStatCaps(uint32_t r0, uint32_t r1, uint32_t,
                                      uint32_t, const uint32_t* absl_nullable) {
  CheckBattleUnitStatCaps(GbaPtr<const Unit>(r0), GbaPtr<BattleUnit>(r1));
  return 0;
}

uint32_t ThunkBattleApplyUnitUpdates(uint32_t, uint32_t, uint32_t, uint32_t,
                                     const uint32_t* absl_nullable) {
  BattleApplyUnitUpdates();
  return 0;
}

uint32_t ThunkBattleGetExpGainStub(uint32_t, uint32_t, uint32_t, uint32_t,
                                   const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(BattleGetExpGainStub());
}

uint32_t ThunkGetBattleUnitUpdatedWeaponExp(uint32_t r0, uint32_t, uint32_t,
                                            uint32_t,
                                            const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(
      GetBattleUnitUpdatedWeaponExp(GbaPtr<BattleUnit>(r0)));
}

uint32_t ThunkHasBattleUnitGainedWeaponLevel(uint32_t r0, uint32_t, uint32_t,
                                             uint32_t,
                                             const uint32_t* absl_nullable) {
  return HasBattleUnitGainedWeaponLevel(GbaPtr<BattleUnit>(r0)) ? 1u : 0u;
}

uint32_t ThunkUpdateUnitFromBattle(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                                   const uint32_t* absl_nullable) {
  UpdateUnitFromBattle(GbaPtr<Unit>(r0), GbaPtr<BattleUnit>(r1));
  return 0;
}

uint32_t ThunkUpdateUnitDuringBattle(uint32_t r0, uint32_t r1, uint32_t,
                                     uint32_t, const uint32_t* absl_nullable) {
  UpdateUnitDuringBattle(GbaPtr<Unit>(r0), GbaPtr<BattleUnit>(r1));
  return 0;
}

uint32_t ThunkBattleApplyBallistaUpdates(uint32_t, uint32_t, uint32_t, uint32_t,
                                         const uint32_t* absl_nullable) {
  BattleApplyBallistaUpdates();
  return 0;
}

uint32_t ThunkClearBattleExpScratch(uint32_t, uint32_t, uint32_t, uint32_t,
                                    const uint32_t* absl_nullable) {
  ClearBattleExpScratch();
  return 0;
}

uint32_t ThunkGetUnitExpLevel(uint32_t r0, uint32_t, uint32_t, uint32_t,
                              const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(GetUnitExpLevel(GbaPtr<const Unit>(r0)));
}

uint32_t ThunkGetUnitRoundExp(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                              const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(
      GetUnitRoundExp(GbaPtr<const Unit>(r0), GbaPtr<const Unit>(r1)));
}

uint32_t ThunkGetUnitPowerLevel(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(GetUnitPowerLevel(GbaPtr<const Unit>(r0)));
}

uint32_t ThunkGetUnitClassKillExpBonus(uint32_t r0, uint32_t r1, uint32_t,
                                       uint32_t,
                                       const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(
      GetUnitClassKillExpBonus(GbaPtr<const Unit>(r0), GbaPtr<const Unit>(r1)));
}

uint32_t ThunkGetUnitExpMultiplier(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                                   const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(
      GetUnitExpMultiplier(GbaPtr<const Unit>(r0), GbaPtr<const Unit>(r1)));
}

uint32_t ThunkGetUnitKillExpBonus(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                                  const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(
      GetUnitKillExpBonus(GbaPtr<const Unit>(r0), GbaPtr<const Unit>(r1)));
}

uint32_t ThunkGetBattleUnitExpGain(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                                   const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(
      GetBattleUnitExpGain(GbaPtr<BattleUnit>(r0), GbaPtr<BattleUnit>(r1)));
}

uint32_t ThunkBattleApplyMiscActionExpGains(uint32_t, uint32_t, uint32_t,
                                            uint32_t,
                                            const uint32_t* absl_nullable) {
  BattleApplyMiscActionExpGains();
  return 0;
}

uint32_t ThunkGetBattleUnitStaffExp(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                    const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(GetBattleUnitStaffExp(GbaPtr<BattleUnit>(r0)));
}

uint32_t ThunkBattleApplyStealExpGain(uint32_t, uint32_t, uint32_t, uint32_t,
                                      const uint32_t* absl_nullable) {
  BattleApplyStealExpGain();
  return 0;
}

}  // namespace

void BattleApplyExpGains() {
  GbaPtr<BattleUnit> actor = BattleActorPtr();
  GbaPtr<BattleUnit> target = BattleTargetPtr();
  if ((static_cast<uint8_t>(actor->unit.id) & 0xC0u) == 0u &&
      (static_cast<uint8_t>(target->unit.id) & 0xC0u) == 0u) {
    return;
  }
  if (IsLinkArenaMode()) {
    return;
  }

  actor->exp_gain = static_cast<int8_t>(GetBattleUnitExpGain(actor, target));
  target->exp_gain = static_cast<int8_t>(GetBattleUnitExpGain(target, actor));

  actor->unit.exp = static_cast<uint8_t>(actor->unit.exp + actor->exp_gain);
  target->unit.exp = static_cast<uint8_t>(target->unit.exp + target->exp_gain);

  CheckBattleUnitLevelUp(actor);
  CheckBattleUnitLevelUp(target);
}

int GetStatIncrease(int growth) {
  int result = 0;
  while (growth > 100) {
    ++result;
    growth -= 100;
  }
  if (RandRoll(growth)) {
    ++result;
  }
  return result;
}

int GetAutoleveledStatIncrease(int growth, int level_count) {
  const int base = static_cast<int>(static_cast<uint32_t>(growth) *
                                    static_cast<uint32_t>(level_count));
  const int variance = RandNext(base / 4) - (base / 8);
  return GetStatIncrease(base + variance);
}

bool CanBattleUnitGainExp(GbaPtr<BattleUnit> bu) {
  if (IsBmStatusBit6Set()) {
    return true;
  }
  if (bu->unit.exp == 0xFFu) {
    return false;
  }
  if ((static_cast<uint8_t>(bu->unit.id) & 0xC0u) != 0u) {
    return false;
  }
  return true;
}

void CheckBattleUnitLevelUp(GbaPtr<BattleUnit> bu) {
  if (!CanBattleUnitGainExp(bu)) {
    return;
  }
  if (bu->unit.exp < 100u) {
    return;
  }

  bu->unit.exp = static_cast<uint8_t>(bu->unit.exp - 100u);
  bu->unit.level = static_cast<int8_t>(bu->unit.level + 1);

  if (bu->unit.level == 20) {
    bu->exp_gain = static_cast<int8_t>(bu->exp_gain - bu->unit.exp);
    bu->unit.exp = 0xFFu;
  }

  const int bonus = (bu->unit.flags & kUnitFlagGrowthBoost) != 0u ? 5 : 0;
  GbaPtr<const PInfo> p = bu->unit.pinfo;

  bu->change_hp = static_cast<int8_t>(GetStatIncrease(p->growth_hp + bonus));
  bu->change_pow = static_cast<int8_t>(GetStatIncrease(p->growth_pow + bonus));
  bu->change_skl = static_cast<int8_t>(GetStatIncrease(p->growth_skl + bonus));
  bu->change_spd = static_cast<int8_t>(GetStatIncrease(p->growth_spd + bonus));
  bu->change_def = static_cast<int8_t>(GetStatIncrease(p->growth_def + bonus));
  bu->change_res = static_cast<int8_t>(GetStatIncrease(p->growth_res + bonus));
  bu->change_lck = static_cast<int8_t>(GetStatIncrease(p->growth_lck + bonus));

  const int total = bu->change_hp + bu->change_pow + bu->change_skl +
                    bu->change_spd + bu->change_def + bu->change_res +
                    bu->change_lck;
  if (total == 0) {
    // Up to 2 reroll passes without the Afa's Drops bonus, stopping as soon as
    // any single stat rolls nonzero (matches FE7 US `0x08029774..0x080297FA`).
    for (int retry = 0; retry < 2; ++retry) {
      bu->change_hp = static_cast<int8_t>(GetStatIncrease(p->growth_hp));
      if (bu->change_hp != 0) break;
      bu->change_pow = static_cast<int8_t>(GetStatIncrease(p->growth_pow));
      if (bu->change_pow != 0) break;
      bu->change_skl = static_cast<int8_t>(GetStatIncrease(p->growth_skl));
      if (bu->change_skl != 0) break;
      bu->change_spd = static_cast<int8_t>(GetStatIncrease(p->growth_spd));
      if (bu->change_spd != 0) break;
      bu->change_def = static_cast<int8_t>(GetStatIncrease(p->growth_def));
      if (bu->change_def != 0) break;
      bu->change_res = static_cast<int8_t>(GetStatIncrease(p->growth_res));
      if (bu->change_res != 0) break;
      bu->change_lck = static_cast<int8_t>(GetStatIncrease(p->growth_lck));
      if (bu->change_lck != 0) break;
    }
  }

  GbaPtr<const Unit> base_unit(GetUnit(bu->unit.id).addr);
  CheckBattleUnitStatCaps(base_unit, bu);
}

void UnitPromote(GbaPtr<Unit> unit) {
  GbaPtr<const JInfo> promoted = GetJobInfo(unit->jinfo->jid_promote);

  auto add_capped = [](int8_t& stat, uint8_t bonus, int8_t cap) {
    const int8_t val = static_cast<int8_t>(stat + bonus);
    stat = val > cap ? cap : val;
  };

  add_capped(unit->max_hp, promoted->promotion_hp, promoted->max_hp);
  add_capped(unit->pow, promoted->promotion_pow, promoted->max_pow);
  add_capped(unit->skl, promoted->promotion_skl, promoted->max_skl);
  add_capped(unit->spd, promoted->promotion_spd, promoted->max_spd);
  add_capped(unit->def, promoted->promotion_def, promoted->max_def);
  add_capped(unit->res, promoted->promotion_res, promoted->max_res);

  for (int i = 0; i < kUnitWexpCount; ++i) {
    unit->wexp[i] = static_cast<uint8_t>(unit->wexp[i] - unit->jinfo->wexp[i]);
  }

  unit->jinfo = promoted;

  for (int i = 0; i < kUnitWexpCount; ++i) {
    int wexp = static_cast<int>(unit->wexp[i]) + promoted->wexp[i];
    if (wexp > kWexpCapS) {
      wexp = kWexpCapS;
    }
    unit->wexp[i] = static_cast<uint8_t>(wexp);
  }

  unit->level = 1;
  unit->exp = 0;
}

void GenerateBattleUnitStatGainsComparatively(GbaPtr<BattleUnit> bu,
                                              GbaPtr<const Unit> unit) {
  bu->change_hp = static_cast<int8_t>(bu->unit.max_hp - unit->max_hp);
  bu->change_pow = static_cast<int8_t>(bu->unit.pow - unit->pow);
  bu->change_skl = static_cast<int8_t>(bu->unit.skl - unit->skl);
  bu->change_spd = static_cast<int8_t>(bu->unit.spd - unit->spd);
  bu->change_def = static_cast<int8_t>(bu->unit.def - unit->def);
  bu->change_res = static_cast<int8_t>(bu->unit.res - unit->res);
  bu->change_lck = static_cast<int8_t>(bu->unit.lck - unit->lck);
  bu->change_con = static_cast<int8_t>(bu->unit.bonus_con - unit->bonus_con);
}

void CheckBattleUnitStatCaps(GbaPtr<const Unit> unit, GbaPtr<BattleUnit> bu) {
  const int max_hp =
      (static_cast<uint8_t>(unit->id) & 0xC0u) == 0x80u ? 120 : 60;
  if (unit->max_hp + bu->change_hp > max_hp) {
    bu->change_hp = static_cast<int8_t>(max_hp - unit->max_hp);
  }
  GbaPtr<const JInfo> j = unit->jinfo;
  if (unit->pow + bu->change_pow > j->max_pow) {
    bu->change_pow = static_cast<int8_t>(j->max_pow - unit->pow);
  }
  if (unit->skl + bu->change_skl > j->max_skl) {
    bu->change_skl = static_cast<int8_t>(j->max_skl - unit->skl);
  }
  if (unit->spd + bu->change_spd > j->max_spd) {
    bu->change_spd = static_cast<int8_t>(j->max_spd - unit->spd);
  }
  if (unit->def + bu->change_def > j->max_def) {
    bu->change_def = static_cast<int8_t>(j->max_def - unit->def);
  }
  if (unit->res + bu->change_res > j->max_res) {
    bu->change_res = static_cast<int8_t>(j->max_res - unit->res);
  }
  if (unit->lck + bu->change_lck > 30) {
    bu->change_lck = static_cast<int8_t>(30 - unit->lck);
  }
}

void BattleApplyUnitUpdates() {
  GbaPtr<BattleUnit> actor = BattleActorPtr();
  GbaPtr<BattleUnit> target = BattleTargetPtr();
  GbaPtr<Unit> actor_unit = GetUnit(actor->unit.id);
  GbaPtr<Unit> target_unit = GetUnit(target->unit.id);

  if (actor->can_counter != 0) {
    actor->unit.items[actor->weapon_slot_index] = actor->weapon;
  }
  if (target->can_counter != 0) {
    target->unit.items[target->weapon_slot_index] = target->weapon;
  }

  UpdateUnitFromBattle(actor_unit, actor);
  if (target_unit) {
    UpdateUnitFromBattle(target_unit, target);
  } else {
    GbaCall(kAddrUpdateObstacleFromBattle, target.addr);
  }
}

int BattleGetExpGainStub() { return 1; }

int GetBattleUnitUpdatedWeaponExp(GbaPtr<BattleUnit> bu) {
  if ((static_cast<uint8_t>(bu->unit.id) & 0xC0u) != 0u) {
    return -1;
  }
  if (bu->unit.hp == 0) {
    return -1;
  }
  if (IsLinkArenaMode()) {
    return -1;
  }
  if (IsBmStatusBit6Set()) {
    return -1;
  }

  if ((BattleStatsPtr()->config & kBattleConfigArena) == 0u) {
    if (bu->can_counter == 0) {
      return -1;
    }
    if ((bu->weapon_attributes & (kItemAttrWeapon | kItemAttrStaff)) == 0u) {
      return -1;
    }
    if ((bu->weapon_attributes & kItemAttrWexpDisallowedMask) != 0u) {
      return -1;
    }
  }

  int wexp = static_cast<int>(bu->unit.wexp[bu->weapon_type]);
  wexp += GetItemWeaponExp(bu->weapon) * bu->wexp_multiplier;

  // Only one S-rank is allowed per unit unless the class naturally starts at S.
  for (int i = 0; i < kUnitWexpCount; ++i) {
    if (i == bu->weapon_type) {
      continue;
    }
    if (bu->unit.jinfo->wexp[i] == kWexpCapS) {
      continue;
    }
    if (bu->unit.wexp[i] > kWexpCapPreS) {
      if (wexp > kWexpCapPreS) {
        wexp = kWexpCapPreS;
      }
      break;
    }
  }

  const bool promoted =
      ((bu->unit.pinfo->attributes | bu->unit.jinfo->attributes) &
       kAttrPromoted) != 0u;
  const int cap = promoted ? kWexpCapS : kWexpCapUnpromoted;
  if (wexp > cap) {
    wexp = cap;
  }
  return wexp;
}

bool HasBattleUnitGainedWeaponLevel(GbaPtr<BattleUnit> bu) {
  const int old_wexp = static_cast<int>(bu->unit.wexp[bu->weapon_type]);
  const int new_wexp = GetBattleUnitUpdatedWeaponExp(bu);
  if (new_wexp < 0) {
    return false;
  }
  return GetWeaponLevelFromExp(old_wexp) != GetWeaponLevelFromExp(new_wexp);
}

void UpdateUnitFromBattle(GbaPtr<Unit> unit, GbaPtr<BattleUnit> bu) {
  unit->level = bu->unit.level;
  unit->exp = bu->unit.exp;
  unit->hp = bu->unit.hp;
  unit->flags = bu->unit.flags;

  *GbaPtr<uint8_t>(kBattleUnitPaletteScratchAddr) =
      static_cast<uint8_t>((bu->unit.flags >> 17) & 7u);

  if (bu->status_out >= 0) {
    SetUnitStatus(unit, bu->status_out);
  }

  unit->max_hp = static_cast<int8_t>(unit->max_hp + bu->change_hp);
  unit->pow = static_cast<int8_t>(unit->pow + bu->change_pow);
  unit->skl = static_cast<int8_t>(unit->skl + bu->change_skl);
  unit->spd = static_cast<int8_t>(unit->spd + bu->change_spd);
  unit->def = static_cast<int8_t>(unit->def + bu->change_def);
  unit->res = static_cast<int8_t>(unit->res + bu->change_res);
  unit->lck = static_cast<int8_t>(unit->lck + bu->change_lck);

  UnitCheckStatOverflow(unit);

  const int new_wexp = GetBattleUnitUpdatedWeaponExp(bu);
  if (new_wexp > 0) {
    unit->wexp[bu->weapon_type] = static_cast<uint8_t>(new_wexp);
  }

  for (int i = 0; i < kUnitItemCount; ++i) {
    unit->items[i] = bu->unit.items[i];
  }
  UnitRemoveInvalidItems(unit);

  if (bu->exp_gain != 0) {
    PidStatsAddExpGained(unit->pinfo->id, static_cast<int>(bu->exp_gain));
  }
}

void UpdateUnitDuringBattle(GbaPtr<Unit> unit, GbaPtr<BattleUnit> bu) {
  unit->hp = bu->unit.hp;
  const int new_wexp = GetBattleUnitUpdatedWeaponExp(bu);
  if (new_wexp > 0) {
    unit->wexp[bu->weapon_type] = static_cast<uint8_t>(new_wexp);
  }
}

void BattleApplyBallistaUpdates() {
  if ((BattleStatsPtr()->config & kBattleConfigBallista) != 0u) {
    GbaPtr<BattleUnit> actor = BattleActorPtr();
    const uint8_t uses = static_cast<uint8_t>(GetItemUses(actor->weapon));
    const uint32_t trap_addr = GbaCall(kAddrGetTrap, actor->unit.ballista_trap);
    *GbaPtr<uint8_t>(trap_addr + 6u) = uses;
  }
}

void ClearBattleExpScratch() {
  GbaPtr<uint8_t> scratch(kBattleExpScratchAddr);
  scratch[0] = 0;
  scratch[1] = 0;
  scratch[2] = 0;
}

int GetUnitExpLevel(GbaPtr<const Unit> unit) {
  int level = unit->level;
  if ((CombinedAttributes(unit) & kAttrPromoted) != 0u) {
    level += 20;
  }
  return level;
}

int GetUnitRoundExp(GbaPtr<const Unit> actor, GbaPtr<const Unit> target) {
  int exp = 31 - (GetUnitExpLevel(actor) - GetUnitExpLevel(target));
  if (exp < 0) {
    exp = 0;
  }
  return exp / actor->jinfo->power_level;
}

int GetUnitPowerLevel(GbaPtr<const Unit> unit) {
  int power = unit->level * unit->jinfo->power_level;
  if ((CombinedAttributes(unit) & kAttrPromoted) != 0u) {
    power += 20 * GetJobInfo(unit->jinfo->jid_promote)->power_level;
  }
  return power;
}

int GetUnitClassKillExpBonus(GbaPtr<const Unit> /*actor*/,
                             GbaPtr<const Unit> target) {
  int bonus = 0;
  const uint32_t attrs = CombinedAttributes(target);
  if ((attrs & kAttrSteal) != 0u) {
    bonus += 20;
  }
  if ((attrs & kAttrBoss) != 0u) {
    bonus += 40;
  }
  return bonus;
}

int GetUnitExpMultiplier(GbaPtr<const Unit> actor,
                         GbaPtr<const Unit> /*target*/) {
  if ((CombinedAttributes(actor) & kAttrAssassin) == 0u) {
    return 1;
  }
  GbaPtr<const BattleHit> hits(kBattleHitsAddr);
  for (int i = 0; i < kBattleHitMaxCount; ++i) {
    if ((hits[i].attributes & kBattleHitAttrSilencer) != 0u) {
      return 2;
    }
  }
  return 1;
}

int GetUnitKillExpBonus(GbaPtr<const Unit> actor, GbaPtr<const Unit> target) {
  if (target->hp != 0) {
    return 0;
  }

  int result = 20;
  const uint8_t mode = *GbaPtr<const uint8_t>(kPlayStatusAddr + 0x1Bu);
  const uint8_t flags = *GbaPtr<const uint8_t>(kPlayStatusAddr + 0x14u);
  if (mode == 1u || (flags & 0x40u) != 0u) {
    result = GetUnitPowerLevel(target) + 20 - GetUnitPowerLevel(actor);
  } else {
    const int target_power = GetUnitPowerLevel(target);
    const int actor_power = GetUnitPowerLevel(actor);
    if (target_power <= actor_power) {
      result += target_power - (actor_power / 2);
    } else {
      result += target_power - actor_power;
    }
  }

  result += GetUnitClassKillExpBonus(actor, target);
  result *= GetUnitExpMultiplier(actor, target);
  if (result < 0) {
    result = 0;
  }
  return result;
}

int GetBattleUnitExpGain(GbaPtr<BattleUnit> actor, GbaPtr<BattleUnit> target) {
  GbaPtr<const Unit> actor_u(actor.addr);
  GbaPtr<const Unit> target_u(target.addr);
  if (!CanBattleUnitGainExp(actor) || actor->unit.hp == 0 ||
      (CombinedAttributes(target_u) & kAttrNoExp) != 0u) {
    return 0;
  }
  if (actor->non_zero_damage == 0) {
    return 1;
  }

  int exp = GetUnitRoundExp(actor_u, target_u) +
            GetUnitKillExpBonus(actor_u, target_u);
  if (exp > 100) {
    exp = 100;
  }
  if (exp < 0) {
    exp = 0;
  }
  return exp;
}

void BattleApplyMiscActionExpGains() {
  if (IsLinkArenaMode()) {
    return;
  }
  GbaPtr<BattleUnit> actor = BattleActorPtr();
  if ((actor->weapon_attributes & kItemAttrStaff) != 0u) {
    if ((static_cast<uint8_t>(actor->unit.id) & 0xC0u) == 0u) {
      actor->wexp_multiplier = static_cast<int8_t>(actor->wexp_multiplier + 1);
    }
    actor->exp_gain = static_cast<int8_t>(GetBattleUnitStaffExp(actor));
    actor->unit.exp = static_cast<uint8_t>(actor->unit.exp + actor->exp_gain);
    CheckBattleUnitLevelUp(actor);
  } else if (actor->weapon_type == static_cast<uint8_t>(ItemKind::kDance) &&
             actor->unit.exp != 0xFFu) {
    actor->exp_gain = 20;
    actor->unit.exp = static_cast<uint8_t>(actor->unit.exp + 20u);
    CheckBattleUnitLevelUp(actor);
  }
}

int GetBattleUnitStaffExp(GbaPtr<BattleUnit> bu) {
  if (!CanBattleUnitGainExp(bu)) {
    return 0;
  }
  if ((GbaPtr<const BattleHit>(kBattleHitsAddr)->attributes &
       kBattleHitAttrMiss) != 0u) {
    return 1;
  }

  int exp = GetItemCostPerUse(bu->weapon) / 20 + 10;
  if ((CombinedAttributes(GbaPtr<const Unit>(bu.addr)) & kAttrPromoted) != 0u) {
    exp /= 2;
  }
  if (exp > 100) {
    exp = 100;
  }
  return exp;
}

void BattleApplyStealExpGain() {
  GbaPtr<BattleUnit> actor = BattleActorPtr();
  if ((static_cast<uint8_t>(actor->unit.id) & 0xC0u) != 0u) {
    return;
  }
  if (!CanBattleUnitGainExp(actor) || IsLinkArenaMode()) {
    return;
  }
  actor->exp_gain = 10;
  actor->unit.exp = static_cast<uint8_t>(actor->unit.exp + 10u);
  CheckBattleUnitLevelUp(actor);
}

void RegisterBattleExpFunctions() {
  RegisterNative(kAddrBattleApplyExpGains, ThunkBattleApplyExpGains);
  RegisterNative(kAddrGetStatIncrease, ThunkGetStatIncrease);
  RegisterNative(kAddrGetAutoleveledStatIncrease,
                 ThunkGetAutoleveledStatIncrease);
  RegisterNative(kAddrCanBattleUnitGainExp, ThunkCanBattleUnitGainExp);
  RegisterNative(kAddrCheckBattleUnitLevelUp, ThunkCheckBattleUnitLevelUp);
  RegisterNative(kAddrUnitPromote, ThunkUnitPromote);
  RegisterNative(kAddrGenerateBattleUnitStatGainsComparatively,
                 ThunkGenerateBattleUnitStatGainsComparatively);
  RegisterNative(kAddrCheckBattleUnitStatCaps, ThunkCheckBattleUnitStatCaps);
  RegisterNative(kAddrBattleApplyUnitUpdates, ThunkBattleApplyUnitUpdates);
  RegisterNative(kAddrBattleGetExpGainStub, ThunkBattleGetExpGainStub);
  RegisterNative(kAddrGetBattleUnitUpdatedWeaponExp,
                 ThunkGetBattleUnitUpdatedWeaponExp);
  RegisterNative(kAddrHasBattleUnitGainedWeaponLevel,
                 ThunkHasBattleUnitGainedWeaponLevel);
  RegisterNative(kAddrUpdateUnitFromBattle, ThunkUpdateUnitFromBattle);
  RegisterNative(kAddrUpdateUnitDuringBattle, ThunkUpdateUnitDuringBattle);
  RegisterNative(kAddrBattleApplyBallistaUpdates,
                 ThunkBattleApplyBallistaUpdates);
  RegisterNative(kAddrClearBattleExpScratch, ThunkClearBattleExpScratch);
  RegisterNative(kAddrGetUnitExpLevel, ThunkGetUnitExpLevel);
  RegisterNative(kAddrGetUnitRoundExp, ThunkGetUnitRoundExp);
  RegisterNative(kAddrGetUnitPowerLevel, ThunkGetUnitPowerLevel);
  RegisterNative(kAddrGetUnitClassKillExpBonus, ThunkGetUnitClassKillExpBonus);
  RegisterNative(kAddrGetUnitExpMultiplier, ThunkGetUnitExpMultiplier);
  RegisterNative(kAddrGetUnitKillExpBonus, ThunkGetUnitKillExpBonus);
  RegisterNative(kAddrGetBattleUnitExpGain, ThunkGetBattleUnitExpGain);
  RegisterNative(kAddrBattleApplyMiscActionExpGains,
                 ThunkBattleApplyMiscActionExpGains);
  RegisterNative(kAddrGetBattleUnitStaffExp, ThunkGetBattleUnitStaffExp);
  RegisterNative(kAddrBattleApplyStealExpGain, ThunkBattleApplyStealExpGain);
}

}  // namespace fe7
