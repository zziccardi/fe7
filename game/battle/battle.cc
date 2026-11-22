#include "game/battle/battle.h"

#include <cstdint>

#include "absl/base/nullability.h"
#include "game/battle.h"
#include "game/item.h"
#include "game/item/item.h"
#include "game/support/support.h"
#include "game/unit.h"
#include "game/unit/unit.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

// Returns the FE7 US Tactician star bonus (0..10) from `gPlayStatus+0x2C`.
int ComputeTacticianStarBonus() {
  const uint16_t raw = *GbaPtr<const uint16_t>(kPlayStatusAddr + 0x2Cu);
  // Extract 9-bit tactician star field (`(raw << 19) >> 23` in Thumb).
  const uint32_t stars = (static_cast<uint32_t>(raw) << 19) >> 23;
  const int bonus = static_cast<int>(stars) / 12;
  return bonus > 10 ? 10 : bonus;
}

// Returns true when FE7 Tactician combat bonuses are active for `unit`.
bool IsTacticianBonusEligible(GbaPtr<const BattleUnit> bu) {
  const uint8_t tact_byte = *GbaPtr<const uint8_t>(kPlayStatusAddr + 0x2Bu);
  if ((tact_byte & 1u) == 0u) {
    return false;
  }
  // Disabled in chapter mode 1 (Lyn mode) and when link-arena/battle flag 0x40
  // is set in gBmStatus+0x04, and only applies to player (blue) units.
  if (*GbaPtr<const uint8_t>(kPlayStatusAddr + 0x1Bu) == 1u) {
    return false;
  }
  if ((*GbaPtr<const uint8_t>(kBmStatusAddr + 0x04u) & 0x40u) != 0u) {
    return false;
  }
  return (static_cast<uint8_t>(bu->unit.id) & 0xC0u) == 0u;
}

uint32_t ThunkComputeBattleUnitStats(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                                     uint32_t /*r3*/,
                                     const uint32_t* absl_nullable /*stack*/) {
  ComputeBattleUnitStats(GbaPtr<BattleUnit>(r0), GbaPtr<BattleUnit>(r1));
  return 0u;
}

uint32_t ThunkComputeBattleUnitEffectiveStats(
    uint32_t r0, uint32_t r1, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  ComputeBattleUnitEffectiveStats(GbaPtr<BattleUnit>(r0),
                                  GbaPtr<BattleUnit>(r1));
  return 0u;
}

uint32_t ThunkComputeBattleUnitDefense(
    uint32_t r0, uint32_t r1, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  ComputeBattleUnitDefense(GbaPtr<BattleUnit>(r0),
                           GbaPtr<const BattleUnit>(r1));
  return 0u;
}

uint32_t ThunkComputeBattleUnitBaseDefense(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  ComputeBattleUnitBaseDefense(GbaPtr<BattleUnit>(r0));
  return 0u;
}

uint32_t ThunkComputeBattleUnitAttack(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                                      uint32_t /*r3*/,
                                      const uint32_t* absl_nullable /*stack*/) {
  ComputeBattleUnitAttack(GbaPtr<BattleUnit>(r0), GbaPtr<const BattleUnit>(r1));
  return 0u;
}

uint32_t ThunkComputeBattleUnitSpeed(uint32_t r0, uint32_t /*r1*/,
                                     uint32_t /*r2*/, uint32_t /*r3*/,
                                     const uint32_t* absl_nullable /*stack*/) {
  ComputeBattleUnitSpeed(GbaPtr<BattleUnit>(r0));
  return 0u;
}

uint32_t ThunkComputeBattleUnitHitRate(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  ComputeBattleUnitHitRate(GbaPtr<BattleUnit>(r0));
  return 0u;
}

uint32_t ThunkComputeBattleUnitAvoidRate(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  ComputeBattleUnitAvoidRate(GbaPtr<BattleUnit>(r0));
  return 0u;
}

uint32_t ThunkComputeBattleUnitCritRate(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  ComputeBattleUnitCritRate(GbaPtr<BattleUnit>(r0));
  return 0u;
}

uint32_t ThunkComputeBattleUnitDodgeRate(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  ComputeBattleUnitDodgeRate(GbaPtr<BattleUnit>(r0));
  return 0u;
}

uint32_t ThunkComputeBattleUnitEffectiveHitRate(
    uint32_t r0, uint32_t r1, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  ComputeBattleUnitEffectiveHitRate(GbaPtr<BattleUnit>(r0),
                                    GbaPtr<const BattleUnit>(r1));
  return 0u;
}

uint32_t ThunkComputeBattleUnitEffectiveCritRate(
    uint32_t r0, uint32_t r1, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  ComputeBattleUnitEffectiveCritRate(GbaPtr<BattleUnit>(r0),
                                     GbaPtr<const BattleUnit>(r1));
  return 0u;
}

uint32_t ThunkComputeBattleUnitSilencerRate(
    uint32_t r0, uint32_t r1, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  ComputeBattleUnitSilencerRate(GbaPtr<BattleUnit>(r0),
                                GbaPtr<const BattleUnit>(r1));
  return 0u;
}

uint32_t ThunkComputeBattleUnitWeaponRankBonuses(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  ComputeBattleUnitWeaponRankBonuses(GbaPtr<BattleUnit>(r0));
  return 0u;
}

uint32_t ThunkComputeBattleUnitStatusBonuses(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  ComputeBattleUnitStatusBonuses(GbaPtr<BattleUnit>(r0));
  return 0u;
}

uint32_t ThunkComputeBattleUnitSpecialWeaponStats(
    uint32_t r0, uint32_t r1, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  ComputeBattleUnitSpecialWeaponStats(GbaPtr<BattleUnit>(r0),
                                      GbaPtr<BattleUnit>(r1));
  return 0u;
}

uint32_t ThunkBattleApplyReaverEffect(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                                      uint32_t /*r3*/,
                                      const uint32_t* absl_nullable /*stack*/) {
  BattleApplyReaverEffect(GbaPtr<BattleUnit>(r0), GbaPtr<BattleUnit>(r1));
  return 0u;
}

uint32_t ThunkBattleApplyWeaponTriangleEffect(
    uint32_t r0, uint32_t r1, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  BattleApplyWeaponTriangleEffect(GbaPtr<BattleUnit>(r0),
                                  GbaPtr<BattleUnit>(r1));
  return 0u;
}

}  // namespace

bool HasTacticianAfaBonus(GbaPtr<const Unit> unit) {
  const int count = GetUnitItemCount(unit);
  for (int i = 0; i < count; ++i) {
    if (GetItemIid(unit->items[i]) == 0x8A) {
      return true;
    }
  }
  return false;
}

void ComputeBattleUnitSupportBonuses(GbaPtr<BattleUnit> attacker,
                                     GbaPtr<BattleUnit> /*defender*/) {
  const uint16_t config = *GbaPtr<const uint16_t>(kBattleStatsAddr);
  if ((config & 0x20u) != 0u &&
      *GbaPtr<const uint8_t>(kPlayStatusAddr + 0x15u) == 0u) {
    return;
  }
  const SupportBonuses b =
      ComputeUnitSupportBonuses(GbaPtr<const Unit>(attacker.addr));
  attacker->battle_attack =
      static_cast<int16_t>(attacker->battle_attack + b.bonus_attack);
  attacker->battle_defense =
      static_cast<int16_t>(attacker->battle_defense + b.bonus_defense);
  attacker->battle_hit_rate =
      static_cast<int16_t>(attacker->battle_hit_rate + b.bonus_hit);
  attacker->battle_avoid_rate =
      static_cast<int16_t>(attacker->battle_avoid_rate + b.bonus_avoid);
  attacker->battle_crit_rate =
      static_cast<int16_t>(attacker->battle_crit_rate + b.bonus_crit);
  attacker->battle_dodge_rate =
      static_cast<int16_t>(attacker->battle_dodge_rate + b.bonus_dodge);
}

void ComputeBattleUnitStats(GbaPtr<BattleUnit> attacker,
                            GbaPtr<BattleUnit> defender) {
  const GbaPtr<const BattleUnit> def_c(defender.addr);
  ComputeBattleUnitDefense(attacker, def_c);
  ComputeBattleUnitAttack(attacker, def_c);
  ComputeBattleUnitSpeed(attacker);
  ComputeBattleUnitHitRate(attacker);
  ComputeBattleUnitAvoidRate(attacker);
  ComputeBattleUnitCritRate(attacker);
  ComputeBattleUnitDodgeRate(attacker);
  ComputeBattleUnitSupportBonuses(attacker, defender);
  ComputeBattleUnitWeaponRankBonuses(attacker);
  ComputeBattleUnitStatusBonuses(attacker);
}

void ComputeBattleUnitEffectiveStats(GbaPtr<BattleUnit> attacker,
                                     GbaPtr<BattleUnit> defender) {
  const GbaPtr<const BattleUnit> def_c(defender.addr);
  ComputeBattleUnitEffectiveHitRate(attacker, def_c);
  ComputeBattleUnitEffectiveCritRate(attacker, def_c);
  ComputeBattleUnitSilencerRate(attacker, def_c);
  ComputeBattleUnitSpecialWeaponStats(attacker, defender);
}

void ComputeBattleUnitDefense(GbaPtr<BattleUnit> attacker,
                              GbaPtr<const BattleUnit> defender) {
  const uint32_t def_attrs = GetItemAttributes(defender->weapon);
  if ((def_attrs & (kItemAttrMagicDamage | kItemAttrMagic)) != 0u) {
    attacker->battle_defense =
        static_cast<int16_t>(attacker->terrain_resistance + attacker->unit.res);
  } else {
    attacker->battle_defense =
        static_cast<int16_t>(attacker->terrain_defense + attacker->unit.def);
  }
}

void ComputeBattleUnitBaseDefense(GbaPtr<BattleUnit> bu) {
  bu->battle_defense = static_cast<int16_t>(bu->terrain_defense + bu->unit.def);
}

void ComputeBattleUnitAttack(GbaPtr<BattleUnit> attacker,
                             GbaPtr<const BattleUnit> defender) {
  int16_t atk = static_cast<int16_t>(GetItemMight(attacker->weapon) +
                                     attacker->wtriangle_dmg_bonus);
  if (IsItemEffectiveAgainst(attacker->weapon,
                             GbaPtr<const Unit>(defender.addr))) {
    // FE7 US uses a 2x effective weapon might multiplier (unlike 3x in FE7J).
    atk = static_cast<int16_t>(atk * 2);
  }
  attacker->battle_attack = static_cast<int16_t>(atk + attacker->unit.pow);
}

void ComputeBattleUnitSpeed(GbaPtr<BattleUnit> bu) {
  int penalty = GetItemWeight(bu->weapon_before) - bu->unit.bonus_con;
  if (penalty < 0) {
    penalty = 0;
  }
  int16_t speed = static_cast<int16_t>(bu->unit.spd - penalty);
  if (speed < 0) {
    speed = 0;
  }
  bu->battle_speed = speed;
}

void ComputeBattleUnitHitRate(GbaPtr<BattleUnit> bu) {
  const int16_t base_hit =
      static_cast<int16_t>(GetItemHit(bu->weapon) + bu->unit.skl * 2 +
                           bu->unit.lck / 2 + bu->wtriangle_hit_bonus);
  bu->battle_hit_rate = base_hit;

  if (IsTacticianBonusEligible(GbaPtr<const BattleUnit>(bu.addr))) {
    const uint8_t tact_byte = *GbaPtr<const uint8_t>(kPlayStatusAddr + 0x2Bu);
    const uint32_t tact_affinity =
        GbaPtr<const uint32_t>(kTacticianAffinityTableAddr)[tact_byte >> 4];
    if (tact_affinity == bu->unit.pinfo->affinity) {
      bu->battle_hit_rate =
          static_cast<int16_t>(base_hit + ComputeTacticianStarBonus());
    }
    if (HasTacticianAfaBonus(GbaPtr<const Unit>(bu.addr))) {
      bu->battle_hit_rate = static_cast<int16_t>(bu->battle_hit_rate + 10);
    }
  }
}

void ComputeBattleUnitAvoidRate(GbaPtr<BattleUnit> bu) {
  const int16_t base_avo = static_cast<int16_t>(
      bu->battle_speed * 2 + bu->terrain_avoid + bu->unit.lck);
  bu->battle_avoid_rate = base_avo;

  if (IsTacticianBonusEligible(GbaPtr<const BattleUnit>(bu.addr))) {
    const uint8_t tact_byte = *GbaPtr<const uint8_t>(kPlayStatusAddr + 0x2Bu);
    const uint32_t tact_affinity =
        GbaPtr<const uint32_t>(kTacticianAffinityTableAddr)[tact_byte >> 4];
    if (tact_affinity == bu->unit.pinfo->affinity) {
      bu->battle_avoid_rate =
          static_cast<int16_t>(base_avo + ComputeTacticianStarBonus());
    }
    if (HasTacticianAfaBonus(GbaPtr<const Unit>(bu.addr))) {
      bu->battle_avoid_rate = static_cast<int16_t>(bu->battle_avoid_rate + 10);
    }
  }

  if (bu->battle_avoid_rate < 0) {
    bu->battle_avoid_rate = 0;
  }
}

void ComputeBattleUnitCritRate(GbaPtr<BattleUnit> bu) {
  int16_t crit =
      static_cast<int16_t>(GetItemCrit(bu->weapon) + bu->unit.skl / 2);
  if ((UnitAttributes(bu->unit) & kUnitAttrCritBonus) != 0u) {
    crit = static_cast<int16_t>(crit + 15);
  }
  bu->battle_crit_rate = crit;
}

void ComputeBattleUnitDodgeRate(GbaPtr<BattleUnit> bu) {
  bu->battle_dodge_rate = bu->unit.lck;
}

void ComputeBattleUnitEffectiveHitRate(GbaPtr<BattleUnit> attacker,
                                       GbaPtr<const BattleUnit> defender) {
  int16_t eff = static_cast<int16_t>(attacker->battle_hit_rate -
                                     defender->battle_avoid_rate);
  if (eff > 100) {
    eff = 100;
  }
  if (eff < 0) {
    eff = 0;
  }
  attacker->battle_effective_hit_rate = eff;
}

void ComputeBattleUnitEffectiveCritRate(GbaPtr<BattleUnit> attacker,
                                        GbaPtr<const BattleUnit> defender) {
  int16_t eff = static_cast<int16_t>(attacker->battle_crit_rate -
                                     defender->battle_dodge_rate);
  attacker->battle_effective_crit_rate = eff;

  if (IsTacticianBonusEligible(defender)) {
    attacker->battle_effective_crit_rate =
        static_cast<int16_t>(eff - ComputeTacticianStarBonus());
  }

  if (attacker->battle_effective_crit_rate < 0) {
    attacker->battle_effective_crit_rate = 0;
  }

  // Iron Rune (`kItemAttrNegateCrit`) forces incoming effective crit to 0.
  for (int i = 0; i < kUnitItemCount; ++i) {
    const uint16_t item = defender->unit.items[i];
    if (item == 0u) {
      break;
    }
    if ((GetItemAttributes(item) & kItemAttrNegateCrit) != 0u) {
      attacker->battle_effective_crit_rate = 0;
      break;
    }
  }
}

void ComputeBattleUnitSilencerRate(GbaPtr<BattleUnit> attacker,
                                   GbaPtr<const BattleUnit> defender) {
  if ((UnitAttributes(attacker->unit) & kUnitAttrAssassin) == 0u) {
    attacker->battle_silencer_rate = 0;
    return;
  }
  int16_t rate = 50;
  const uint32_t def_attrs = UnitAttributes(defender->unit);
  if ((def_attrs & kUnitAttrBoss) != 0u) {
    rate = 25;
  }
  if ((def_attrs & kUnitAttrFinalBoss) != 0u) {
    rate = 0;
  }
  attacker->battle_silencer_rate = rate;
}

void ComputeBattleUnitWeaponRankBonuses(GbaPtr<BattleUnit> bu) {
  if (bu->weapon == 0u) {
    return;
  }
  const int kind = GetItemKind(bu->weapon);
  if (kind <= 7 && bu->unit.wexp[kind] > 250u) {
    bu->battle_hit_rate = static_cast<int16_t>(bu->battle_hit_rate + 5);
    bu->battle_crit_rate = static_cast<int16_t>(bu->battle_crit_rate + 5);
  }
}

void ComputeBattleUnitStatusBonuses(GbaPtr<BattleUnit> bu) {
  switch (static_cast<UnitStatus>(bu->unit.status)) {
    case UnitStatus::kAttack:
      bu->battle_attack = static_cast<int16_t>(bu->battle_attack + 10);
      break;
    case UnitStatus::kDefense:
      bu->battle_defense = static_cast<int16_t>(bu->battle_defense + 10);
      break;
    case UnitStatus::kCrit:
      bu->battle_crit_rate = static_cast<int16_t>(bu->battle_crit_rate + 10);
      break;
    case UnitStatus::kAvoid:
      bu->battle_avoid_rate = static_cast<int16_t>(bu->battle_avoid_rate + 10);
      break;
    default:
      break;
  }
}

void ComputeBattleUnitSpecialWeaponStats(GbaPtr<BattleUnit> attacker,
                                         GbaPtr<BattleUnit> defender) {
  if ((attacker->weapon_attributes & kItemAttrMagicDamage) != 0u) {
    const int iid = GetItemIid(attacker->weapon);
    // Light Brand (0x10), Runesword (0x11), and Wind Sword (0x99) halve Str
    // contribution and disable crits when attacking at range.
    if (iid == 0x10 || iid == 0x11 || iid == 0x99) {
      attacker->battle_attack = static_cast<int16_t>(attacker->battle_attack -
                                                     attacker->unit.pow / 2);
      attacker->battle_crit_rate = 0;
      attacker->battle_effective_crit_rate = 0;
    }
    return;
  }

  // Eclipse (`weapon_effect == 3`): halves target HP (minimum 1) and ignores
  // defense and crits.
  if (GetItemWeaponEffect(attacker->weapon_before) == 3) {
    int16_t dmg = static_cast<int16_t>((defender->unit.hp + 1) >> 1);
    if (dmg == 0) {
      dmg = 1;
    }
    attacker->battle_attack = dmg;
    defender->battle_defense = 0;
    attacker->battle_crit_rate = 0;
    attacker->battle_effective_crit_rate = 0;
  }

  // Luna (`kItemAttrNegateDefense`): zeroes defender resistance/defense.
  if ((attacker->weapon_attributes & kItemAttrNegateDefense) != 0u) {
    defender->battle_defense = 0;
  }
}

void BattleApplyReaverEffect(GbaPtr<BattleUnit> attacker,
                             GbaPtr<BattleUnit> defender) {
  if ((attacker->weapon_attributes & kItemAttrReverseTriangle) != 0u &&
      (defender->weapon_attributes & kItemAttrReverseTriangle) != 0u) {
    return;
  }
  attacker->wtriangle_hit_bonus =
      static_cast<int8_t>(-(attacker->wtriangle_hit_bonus * 2));
  attacker->wtriangle_dmg_bonus =
      static_cast<int8_t>(-(attacker->wtriangle_dmg_bonus * 2));
  defender->wtriangle_hit_bonus =
      static_cast<int8_t>(-(defender->wtriangle_hit_bonus * 2));
  defender->wtriangle_dmg_bonus =
      static_cast<int8_t>(-(defender->wtriangle_dmg_bonus * 2));
}

void BattleApplyWeaponTriangleEffect(GbaPtr<BattleUnit> attacker,
                                     GbaPtr<BattleUnit> defender) {
  const GbaPtr<const WeaponTriangleRule> rules(kWeaponTriangleTableAddr);
  for (ptrdiff_t i = 0; rules[i].attacker_kind >= 0; ++i) {
    if (attacker->weapon_type == static_cast<uint8_t>(rules[i].attacker_kind) &&
        defender->weapon_type == static_cast<uint8_t>(rules[i].defender_kind)) {
      attacker->wtriangle_hit_bonus = rules[i].hit_bonus;
      attacker->wtriangle_dmg_bonus = rules[i].dmg_bonus;
      defender->wtriangle_hit_bonus = static_cast<int8_t>(-rules[i].hit_bonus);
      defender->wtriangle_dmg_bonus = static_cast<int8_t>(-rules[i].dmg_bonus);
      break;
    }
  }

  if ((attacker->weapon_attributes & kItemAttrReverseTriangle) != 0u) {
    BattleApplyReaverEffect(attacker, defender);
  }
  if ((defender->weapon_attributes & kItemAttrReverseTriangle) != 0u) {
    BattleApplyReaverEffect(attacker, defender);
  }
}

void RegisterBattleFunctions() {
  RegisterNative(kAddrComputeBattleUnitStats, &ThunkComputeBattleUnitStats);
  RegisterNative(kAddrComputeBattleUnitEffectiveStats,
                 &ThunkComputeBattleUnitEffectiveStats);
  RegisterNative(kAddrComputeBattleUnitDefense, &ThunkComputeBattleUnitDefense);
  RegisterNative(kAddrComputeBattleUnitBaseDefense,
                 &ThunkComputeBattleUnitBaseDefense);
  RegisterNative(kAddrComputeBattleUnitAttack, &ThunkComputeBattleUnitAttack);
  RegisterNative(kAddrComputeBattleUnitSpeed, &ThunkComputeBattleUnitSpeed);
  RegisterNative(kAddrComputeBattleUnitHitRate, &ThunkComputeBattleUnitHitRate);
  RegisterNative(kAddrComputeBattleUnitAvoidRate,
                 &ThunkComputeBattleUnitAvoidRate);
  RegisterNative(kAddrComputeBattleUnitCritRate,
                 &ThunkComputeBattleUnitCritRate);
  RegisterNative(kAddrComputeBattleUnitDodgeRate,
                 &ThunkComputeBattleUnitDodgeRate);
  RegisterNative(kAddrComputeBattleUnitEffectiveHitRate,
                 &ThunkComputeBattleUnitEffectiveHitRate);
  RegisterNative(kAddrComputeBattleUnitEffectiveCritRate,
                 &ThunkComputeBattleUnitEffectiveCritRate);
  RegisterNative(kAddrComputeBattleUnitSilencerRate,
                 &ThunkComputeBattleUnitSilencerRate);
  RegisterNative(kAddrComputeBattleUnitWeaponRankBonuses,
                 &ThunkComputeBattleUnitWeaponRankBonuses);
  RegisterNative(kAddrComputeBattleUnitStatusBonuses,
                 &ThunkComputeBattleUnitStatusBonuses);
  RegisterNative(kAddrComputeBattleUnitSpecialWeaponStats,
                 &ThunkComputeBattleUnitSpecialWeaponStats);
  RegisterNative(kAddrBattleApplyReaverEffect, &ThunkBattleApplyReaverEffect);
  RegisterNative(kAddrHasTacticianAfaBonus,
                 [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return HasTacticianAfaBonus(GbaPtr<const Unit>(r0)) ? 1u
                                                                       : 0u;
                 });
  RegisterNative(kAddrComputeBattleUnitSupportBonuses,
                 [](uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   ComputeBattleUnitSupportBonuses(GbaPtr<BattleUnit>(r0),
                                                   GbaPtr<BattleUnit>(r1));
                   return 0u;
                 });
  RegisterNative(kAddrBattleApplyWeaponTriangleEffect,
                 &ThunkBattleApplyWeaponTriangleEffect);
}

}  // namespace fe7
