// FE7 US (AE7E) battle simulation, hit generation, and combat unwinding.

#include "game/battle/battle_sim.h"

#include <cstdint>
#include <cstring>

#include "absl/base/nullability.h"
#include "game/battle.h"
#include "game/engine/rng.h"
#include "game/item.h"
#include "game/item/item.h"
#include "game/map/map.h"
#include "game/unit.h"
#include "game/unit/unit.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

constexpr uint32_t kAttrTriangleAttackMask = 0x00600000u;
constexpr uint32_t kAttrConsumeOnMissMask = kItemAttrMagic | 0x80u;
constexpr uint8_t kUnitStatusSleep = 2u;
constexpr uint8_t kUnitStatusBerserk = 4u;

constexpr int kWeaponEffectPoison = 1;
constexpr int kWeaponEffectHpSteal = 2;
constexpr int kWeaponEffectHalfHp = 3;
constexpr int kWeaponEffectDevil = 4;

GbaPtr<BattleStats> BattleStatsPtr() {
  return GbaPtr<BattleStats>(kBattleStatsAddr);
}

GbaPtr<BattleUnit> BattleActorPtr() {
  return GbaPtr<BattleUnit>(kBattleActorAddr);
}

GbaPtr<BattleUnit> BattleTargetPtr() {
  return GbaPtr<BattleUnit>(kBattleTargetAddr);
}

GbaPtr<GbaPtr<BattleHit>> BattleHitItPtr() {
  return GbaPtr<GbaPtr<BattleHit>>(kBattleHitItAddr);
}

// --- Dispatch thunks ---

uint32_t ThunkBattleRoll1RN(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                            const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(static_cast<int32_t>(
      BattleRoll1RN(static_cast<uint16_t>(r0), static_cast<uint8_t>(r1) != 0)));
}

uint32_t ThunkBattleRoll2RN(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                            const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(static_cast<int32_t>(
      BattleRoll2RN(static_cast<uint16_t>(r0), static_cast<uint8_t>(r1) != 0)));
}

uint32_t ThunkInitBattleUnit(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                             const uint32_t* absl_nullable) {
  InitBattleUnit(GbaPtr<BattleUnit>(r0), GbaPtr<const Unit>(r1));
  return 0;
}

uint32_t ThunkInitBattleUnitWithoutBonuses(uint32_t r0, uint32_t r1, uint32_t,
                                           uint32_t,
                                           const uint32_t* absl_nullable) {
  InitBattleUnitWithoutBonuses(GbaPtr<BattleUnit>(r0), GbaPtr<const Unit>(r1));
  return 0;
}

uint32_t ThunkClearBattleHits(uint32_t, uint32_t, uint32_t, uint32_t,
                              const uint32_t* absl_nullable) {
  ClearBattleHits();
  return 0;
}

uint32_t ThunkBattleUnwind(uint32_t, uint32_t, uint32_t, uint32_t,
                           const uint32_t* absl_nullable) {
  BattleUnwind();
  return 0;
}

uint32_t ThunkBattleGetBattleUnitOrder(uint32_t r0, uint32_t r1, uint32_t,
                                       uint32_t,
                                       const uint32_t* absl_nullable) {
  BattleGetBattleUnitOrder(GbaPtr<GbaPtr<BattleUnit>>(r0),
                           GbaPtr<GbaPtr<BattleUnit>>(r1));
  return 0;
}

uint32_t ThunkBattleGetFollowUpOrder(uint32_t r0, uint32_t r1, uint32_t,
                                     uint32_t, const uint32_t* absl_nullable) {
  return BattleGetFollowUpOrder(GbaPtr<GbaPtr<BattleUnit>>(r0),
                                GbaPtr<GbaPtr<BattleUnit>>(r1))
             ? 1u
             : 0u;
}

uint32_t ThunkBattleGenerateRoundHits(uint32_t r0, uint32_t r1, uint32_t,
                                      uint32_t, const uint32_t* absl_nullable) {
  return BattleGenerateRoundHits(GbaPtr<BattleUnit>(r0), GbaPtr<BattleUnit>(r1))
             ? 1u
             : 0u;
}

uint32_t ThunkBattleGetEffectiveHitCount(uint32_t r0, uint32_t, uint32_t,
                                         uint32_t,
                                         const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(
      BattleGetEffectiveHitCount(GbaPtr<BattleUnit>(r0)));
}

uint32_t ThunkBattleCheckBrave(uint32_t r0, uint32_t, uint32_t, uint32_t,
                               const uint32_t* absl_nullable) {
  return BattleCheckBrave(GbaPtr<BattleUnit>(r0)) ? 1u : 0u;
}

uint32_t ThunkBattleCheckTriangleAttackOrder(uint32_t r0, uint32_t r1, uint32_t,
                                             uint32_t,
                                             const uint32_t* absl_nullable) {
  return BattleCheckTriangleAttackOrder(GbaPtr<BattleUnit>(r0),
                                        GbaPtr<BattleUnit>(r1))
             ? 1u
             : 0u;
}

uint32_t ThunkBattleUpdateBattleStats(uint32_t r0, uint32_t r1, uint32_t,
                                      uint32_t, const uint32_t* absl_nullable) {
  BattleUpdateBattleStats(GbaPtr<BattleUnit>(r0), GbaPtr<BattleUnit>(r1));
  return 0;
}

uint32_t ThunkBattleGenerateHitAttributes(uint32_t r0, uint32_t, uint32_t,
                                          uint32_t,
                                          const uint32_t* absl_nullable) {
  BattleGenerateHitAttributes(GbaPtr<BattleUnit>(r0));
  return 0;
}

uint32_t ThunkBattleGenerateHitTriangleAttack(uint32_t r0, uint32_t r1,
                                              uint32_t, uint32_t,
                                              const uint32_t* absl_nullable) {
  BattleGenerateHitTriangleAttack(GbaPtr<BattleUnit>(r0),
                                  GbaPtr<BattleUnit>(r1));
  return 0;
}

uint32_t ThunkBattleGenerateHitEffects(uint32_t r0, uint32_t r1, uint32_t,
                                       uint32_t,
                                       const uint32_t* absl_nullable) {
  BattleGenerateHitEffects(GbaPtr<BattleUnit>(r0), GbaPtr<BattleUnit>(r1));
  return 0;
}

uint32_t ThunkBattleGenerateHit(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                                const uint32_t* absl_nullable) {
  return BattleGenerateHit(GbaPtr<BattleUnit>(r0), GbaPtr<BattleUnit>(r1)) ? 1u
                                                                           : 0u;
}

uint32_t ThunkBattleUnitTargetSetEquippedWeapon(uint32_t r0, uint32_t, uint32_t,
                                                uint32_t,
                                                const uint32_t* absl_nullable) {
  BattleUnitTargetSetEquippedWeapon(GbaPtr<BattleUnit>(r0));
  return 0;
}

uint32_t ThunkBattleUnitTargetCheckHasRealWeapon(
    uint32_t r0, uint32_t, uint32_t, uint32_t, const uint32_t* absl_nullable) {
  BattleUnitTargetCheckHasRealWeapon(GbaPtr<BattleUnit>(r0));
  return 0;
}

uint32_t ThunkBattleInitTargetCanCounter(uint32_t, uint32_t, uint32_t, uint32_t,
                                         const uint32_t* absl_nullable) {
  BattleInitTargetCanCounter();
  return 0;
}

uint32_t ThunkBattleHitAdvance(uint32_t, uint32_t, uint32_t, uint32_t,
                               const uint32_t* absl_nullable) {
  BattleHitAdvance();
  return 0;
}

uint32_t ThunkBattleHitTerminate(uint32_t, uint32_t, uint32_t, uint32_t,
                                 const uint32_t* absl_nullable) {
  BattleHitTerminate();
  return 0;
}

}  // namespace

int8_t BattleRoll1RN(uint16_t threshold, bool sim_result) {
  if ((BattleStatsPtr()->config & kBattleConfigSimulate) == 0u) {
    return static_cast<int8_t>(RandRoll(static_cast<int>(threshold)));
  }
  return sim_result ? 1 : 0;
}

int8_t BattleRoll2RN(uint16_t threshold, bool sim_result) {
  if ((BattleStatsPtr()->config & kBattleConfigSimulate) == 0u) {
    return static_cast<int8_t>(RandRoll2Rn(static_cast<int>(threshold)));
  }
  return sim_result ? 1 : 0;
}

void InitBattleUnit(GbaPtr<BattleUnit> bu, GbaPtr<const Unit> unit) {
  if (!unit) {
    return;
  }

  std::memcpy(&bu->unit, unit.get(), sizeof(Unit));
  bu->unit.max_hp = static_cast<int8_t>(GetUnitMaxHp(unit));
  bu->unit.pow = static_cast<int8_t>(GetUnitPower(unit));
  bu->unit.skl = static_cast<int8_t>(GetUnitSkill(unit));
  bu->unit.spd = static_cast<int8_t>(GetUnitSpeed(unit));
  bu->unit.def = static_cast<int8_t>(GetUnitDefense(unit));
  bu->unit.lck = static_cast<int8_t>(GetUnitLuck(unit));
  bu->unit.res = static_cast<int8_t>(GetUnitResistance(unit));
  bu->unit.bonus_con = static_cast<int8_t>(
      unit->jinfo->base_con + unit->pinfo->bonus_con + unit->bonus_con);
  bu->unit.bonus_mov =
      static_cast<int8_t>(unit->bonus_mov + unit->jinfo->base_mov);

  bu->level_previous = bu->unit.level;
  bu->exp_previous = static_cast<int8_t>(bu->unit.exp);
  bu->hp_initial = bu->unit.hp;
  bu->status_out = -1;

  bu->change_hp = 0;
  bu->change_pow = 0;
  bu->change_skl = 0;
  bu->change_spd = 0;
  bu->change_def = 0;
  bu->change_res = 0;
  bu->change_lck = 0;
  bu->change_con = 0;

  // Match FE7 US: resets WEXP multiplier, weapon_broke, and exp_gain on both
  // global combatants in addition to bu's triangle/damage flags.
  BattleActorPtr()->wexp_multiplier = 0;
  BattleTargetPtr()->wexp_multiplier = 0;

  bu->wtriangle_hit_bonus = 0;
  bu->wtriangle_dmg_bonus = 0;
  bu->non_zero_damage = 0;

  BattleActorPtr()->weapon_broke = 0;
  BattleTargetPtr()->weapon_broke = 0;
  BattleActorPtr()->exp_gain = 0;
  BattleTargetPtr()->exp_gain = 0;
}

void InitBattleUnitWithoutBonuses(GbaPtr<BattleUnit> bu,
                                  GbaPtr<const Unit> unit) {
  InitBattleUnit(bu, unit);
  bu->unit.max_hp = unit->max_hp;
  bu->unit.pow = unit->pow;
  bu->unit.skl = unit->skl;
  bu->unit.spd = unit->spd;
  bu->unit.def = unit->def;
  bu->unit.lck = unit->lck;
  bu->unit.res = unit->res;
  bu->unit.bonus_con =
      static_cast<int8_t>(unit->jinfo->base_con + unit->pinfo->bonus_con);
}

void ClearBattleHits() {
  GbaPtr<BattleHit> hits(kBattleHitsAddr);
  for (int i = 0; i < kBattleHitMaxCount; ++i) {
    hits[i].attributes = 0;
    hits[i].info = 0;
    hits[i].hp_change = 0;
  }
  *BattleHitItPtr() = hits;
}

void BattleGetBattleUnitOrder(GbaPtr<GbaPtr<BattleUnit>> out_attacker,
                              GbaPtr<GbaPtr<BattleUnit>> out_defender) {
  *out_attacker = BattleActorPtr();
  *out_defender = BattleTargetPtr();
}

bool BattleGetFollowUpOrder(GbaPtr<GbaPtr<BattleUnit>> out_attacker,
                            GbaPtr<GbaPtr<BattleUnit>> out_defender) {
  GbaPtr<BattleUnit> target = BattleTargetPtr();
  GbaPtr<BattleUnit> actor = BattleActorPtr();
  if (target->battle_speed > 250) {
    return false;
  }

  const int diff = static_cast<int>(actor->battle_speed) -
                   static_cast<int>(target->battle_speed);
  if (diff >= 0 ? (diff <= 3) : (-diff <= 3)) {
    return false;
  }

  if (actor->battle_speed > target->battle_speed) {
    *out_attacker = actor;
    *out_defender = target;
  } else {
    *out_attacker = target;
    *out_defender = actor;
  }

  if (GetItemWeaponEffect((*out_attacker)->weapon_before) ==
      kWeaponEffectHalfHp) {
    return false;
  }
  return true;
}

void BattleUnwind() {
  ClearBattleHits();

  GbaPtr<BattleUnit> attacker = BattleActorPtr();
  GbaPtr<BattleUnit> defender = BattleTargetPtr();

  (*BattleHitItPtr())->info |= kBattleHitInfoBegin;

  if (!BattleGenerateRoundHits(attacker, defender)) {
    (*BattleHitItPtr())->attributes |= kBattleHitAttrRetaliation;
    if (!BattleGenerateRoundHits(defender, attacker)) {
      // Check follow-up order directly without needing GBA stack storage.
      GbaPtr<BattleUnit> target = BattleTargetPtr();
      GbaPtr<BattleUnit> actor = BattleActorPtr();
      bool can_follow_up = false;
      if (target->battle_speed <= 250) {
        const int diff = static_cast<int>(actor->battle_speed) -
                         static_cast<int>(target->battle_speed);
        if (diff > 3 || diff < -3) {
          if (actor->battle_speed > target->battle_speed) {
            attacker = actor;
            defender = target;
          } else {
            attacker = target;
            defender = actor;
          }
          if (GetItemWeaponEffect(attacker->weapon_before) !=
              kWeaponEffectHalfHp) {
            can_follow_up = true;
          }
        }
      }
      if (can_follow_up) {
        // Matches FE7 US `strh #4, [r1]` (overwrites attributes, not OR).
        (*BattleHitItPtr())->attributes = kBattleHitAttrFollowUp;
        BattleGenerateRoundHits(attacker, defender);
      }
    }
  }

  (*BattleHitItPtr())->info |= kBattleHitInfoEnd;
}

bool BattleGenerateRoundHits(GbaPtr<BattleUnit> attacker,
                             GbaPtr<BattleUnit> defender) {
  if (attacker->weapon == 0) {
    return false;
  }

  const uint16_t attrs = (*BattleHitItPtr())->attributes;
  const int count = BattleGetEffectiveHitCount(attacker);
  for (int i = 0; i < count; ++i) {
    (*BattleHitItPtr())->attributes |= attrs;
    if (BattleGenerateHit(attacker, defender)) {
      return true;
    }
  }
  return false;
}

int BattleGetEffectiveHitCount(GbaPtr<BattleUnit> attacker) {
  int count = 1;
  count <<= (BattleCheckBrave(attacker) ? 1 : 0);
  return count;
}

bool BattleCheckBrave(GbaPtr<BattleUnit> attacker) {
  if ((attacker->weapon_attributes & kItemAttrBrave) != 0u) {
    (*BattleHitItPtr())->attributes |= kBattleHitAttrBrave;
    return true;
  }
  return false;
}

bool BattleCheckTriangleAttackOrder(GbaPtr<BattleUnit> attacker,
                                    GbaPtr<BattleUnit> defender) {
  static constexpr int8_t kOffsets[4][2] = {
      {-1, 0},
      {0, -1},
      {1, 0},
      {0, 1},
  };

  const uint32_t ta_mask =
      (attacker->unit.pinfo->attributes | attacker->unit.jinfo->attributes) &
      kAttrTriangleAttackMask;
  const int def_x = static_cast<int>(defender->unit.x);
  const int def_y = static_cast<int>(defender->unit.y);
  const uint8_t faction = static_cast<uint8_t>(attacker->unit.id) & 0xC0u;

  GbaPtr<BattleStats> stats = BattleStatsPtr();
  stats->ta_unit_a = GbaPtr<Unit>(0);
  stats->ta_unit_b = GbaPtr<Unit>(0);

  GbaPtr<GbaPtr<uint8_t>> unit_map =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr);
  int count = 0;
  for (int i = 0; i < 4; ++i) {
    const int nx = def_x + kOffsets[i][0];
    const int ny = def_y + kOffsets[i][1];
    const uint8_t uid = unit_map[ny][nx];
    if (uid == 0) {
      continue;
    }
    GbaPtr<Unit> ally = GetUnit(uid);
    if ((uid & 0xC0u) != faction) {
      continue;
    }
    if ((ally->status & 0x0Fu) == kUnitStatusSleep) {
      continue;
    }
    if (((ally->pinfo->attributes | ally->jinfo->attributes) & ta_mask) == 0u) {
      continue;
    }
    ++count;
    if (!stats->ta_unit_a) {
      stats->ta_unit_a = ally;
    } else if (!stats->ta_unit_b) {
      stats->ta_unit_b = ally;
    }
  }
  return count > 1;
}

void BattleUpdateBattleStats(GbaPtr<BattleUnit> attacker,
                             GbaPtr<BattleUnit> defender) {
  GbaPtr<BattleStats> stats = BattleStatsPtr();
  stats->attack = attacker->battle_attack;
  stats->defense = defender->battle_defense;
  stats->hit_rate = attacker->battle_effective_hit_rate;
  stats->crit_rate = attacker->battle_effective_crit_rate;
  stats->silencer_rate = attacker->battle_silencer_rate;
}

void BattleGenerateHitAttributes(GbaPtr<BattleUnit> attacker) {
  GbaPtr<BattleStats> stats = BattleStatsPtr();
  stats->damage = 0;

  if (BattleRoll2RN(static_cast<uint16_t>(stats->hit_rate), true) == 0) {
    (*BattleHitItPtr())->attributes |= kBattleHitAttrMiss;
    return;
  }

  stats->damage = static_cast<int16_t>(stats->attack - stats->defense);

  if (BattleRoll1RN(static_cast<uint16_t>(stats->crit_rate), false) == 1) {
    if (BattleRoll1RN(static_cast<uint16_t>(stats->silencer_rate), false) ==
        1) {
      (*BattleHitItPtr())->attributes |= kBattleHitAttrSilencer;
      stats->damage = 127;
    } else {
      (*BattleHitItPtr())->attributes |= kBattleHitAttrCrit;
      stats->damage = static_cast<int16_t>(stats->damage * 3);
    }
  }

  if (stats->damage > 127) {
    stats->damage = 127;
  }
  if (stats->damage < 0) {
    stats->damage = 0;
  }
  if (stats->damage != 0) {
    attacker->non_zero_damage = 1;
  }
}

void BattleGenerateHitTriangleAttack(GbaPtr<BattleUnit> attacker,
                                     GbaPtr<BattleUnit> defender) {
  if (((attacker->unit.pinfo->attributes | attacker->unit.jinfo->attributes) &
       kAttrTriangleAttackMask) == 0u) {
    return;
  }
  GbaPtr<BattleStats> stats = BattleStatsPtr();
  if (stats->range != 1) {
    return;
  }
  if (((*BattleHitItPtr())->info & kBattleHitInfoBegin) == 0u) {
    return;
  }
  if ((attacker->unit.status & 0x0Fu) == kUnitStatusBerserk) {
    return;
  }
  if ((stats->config & kBattleConfigArena) != 0u) {
    return;
  }
  if (!BattleCheckTriangleAttackOrder(attacker, defender)) {
    return;
  }

  (*BattleHitItPtr())->attributes |= kBattleHitAttrTAttack;
  stats->crit_rate = 100;
  stats->hit_rate = 100;
}

void BattleGenerateHitEffects(GbaPtr<BattleUnit> attacker,
                              GbaPtr<BattleUnit> defender) {
  attacker->wexp_multiplier =
      static_cast<int8_t>(attacker->wexp_multiplier + 1);

  GbaPtr<BattleHit> hit = *BattleHitItPtr();
  GbaPtr<BattleStats> stats = BattleStatsPtr();

  if ((hit->attributes & kBattleHitAttrMiss) == 0u) {
    const int effect = GetItemWeaponEffect(attacker->weapon);
    if (effect == kWeaponEffectPoison) {
      defender->status_out = 1;
      hit->attributes |= kBattleHitAttrPoison;
    } else if (effect == kWeaponEffectHalfHp) {
      hit->attributes |= kBattleHitAttrHalfHp;
    }

    if (GetItemWeaponEffect(attacker->weapon) == kWeaponEffectDevil &&
        BattleRoll1RN(static_cast<uint16_t>(31 - attacker->unit.lck), false) !=
            0) {
      hit->attributes |= kBattleHitAttrDevil;
      const int8_t new_hp =
          static_cast<int8_t>(attacker->unit.hp - stats->damage);
      attacker->unit.hp = new_hp < 0 ? 0 : new_hp;
    } else {
      if (stats->damage > defender->unit.hp) {
        stats->damage = defender->unit.hp;
      }
      const int8_t new_hp =
          static_cast<int8_t>(defender->unit.hp - stats->damage);
      defender->unit.hp = new_hp < 0 ? 0 : new_hp;
    }

    if (GetItemWeaponEffect(attacker->weapon) == kWeaponEffectHpSteal) {
      const int8_t healed =
          static_cast<int8_t>(attacker->unit.hp + stats->damage);
      attacker->unit.hp =
          healed > attacker->unit.max_hp ? attacker->unit.max_hp : healed;
      hit->attributes |= kBattleHitAttrHpSteal;
    }
  }

  hit->hp_change = static_cast<int8_t>(stats->damage);

  if ((hit->attributes & kBattleHitAttrMiss) == 0u ||
      (attacker->weapon_attributes & kAttrConsumeOnMissMask) != 0u) {
    attacker->weapon = GetItemAfterUse(attacker->weapon);
    if (attacker->weapon == 0) {
      attacker->weapon_broke = 1;
    }
  }
}

bool BattleGenerateHit(GbaPtr<BattleUnit> attacker,
                       GbaPtr<BattleUnit> defender) {
  if (attacker.addr == kBattleTargetAddr) {
    (*BattleHitItPtr())->info |= kBattleHitInfoRetaliation;
  }

  BattleUpdateBattleStats(attacker, defender);
  BattleGenerateHitTriangleAttack(attacker, defender);
  BattleGenerateHitAttributes(attacker);
  BattleGenerateHitEffects(attacker, defender);

  if (attacker->unit.hp == 0 || defender->unit.hp == 0) {
    attacker->wexp_multiplier =
        static_cast<int8_t>(attacker->wexp_multiplier + 1);
    (*BattleHitItPtr())->info |= kBattleHitInfoKills;
    if (BattleTargetPtr()->unit.hp == 0) {
      (*BattleHitItPtr())->info |= kBattleHitInfoKillsTarget;
    }
    BattleHitAdvance();
    return true;
  }

  BattleHitAdvance();
  return false;
}

void BattleUnitTargetSetEquippedWeapon(GbaPtr<BattleUnit> bu) {
  if (bu->weapon_before != 0) {
    return;
  }
  GbaPtr<const Unit> unit_ptr(bu.addr);
  bu->weapon_before = static_cast<uint16_t>(GetUnitEquippedWeapon(unit_ptr));
  if (bu->weapon_before != 0) {
    return;
  }
  if (!UnitKnowsMagic(unit_ptr)) {
    return;
  }
  for (int i = 0; i < kUnitItemCount; ++i) {
    if (CanUnitUseStaff(unit_ptr, bu->unit.items[i])) {
      bu->weapon_before = bu->unit.items[i];
      return;
    }
  }
}

void BattleUnitTargetCheckHasRealWeapon(GbaPtr<BattleUnit> bu) {
  if (bu->can_counter == 0) {
    bu->battle_attack = 0xFF;
    bu->battle_hit_rate = 0xFF;
    bu->battle_effective_hit_rate = 0xFF;
    bu->battle_crit_rate = 0xFF;
    bu->battle_effective_crit_rate = 0xFF;
  }
}

void BattleInitTargetCanCounter() {
  GbaPtr<BattleUnit> actor = BattleActorPtr();
  GbaPtr<BattleUnit> target = BattleTargetPtr();

  // Uncounterable weapons (ballistae, etc.) prevent defender retaliation.
  if (((actor->weapon_attributes | target->weapon_attributes) & 0x80u) != 0u) {
    target->weapon = 0;
    target->can_counter = 0;
  }

  // Berserk player units attacking another player unit are not countered.
  if ((actor->unit.status & 0x0Fu) == kUnitStatusBerserk &&
      (static_cast<uint8_t>(actor->unit.id) & 0xC0u) == 0u &&
      (static_cast<uint8_t>(target->unit.id) & 0xC0u) == 0u) {
    target->weapon = 0;
    target->can_counter = 0;
  }
}

void BattleHitAdvance() {
  GbaPtr<GbaPtr<BattleHit>> it = BattleHitItPtr();
  *it = GbaPtr<BattleHit>(it->addr + sizeof(BattleHit));
}

void BattleHitTerminate() {
  BattleHitAdvance();
  (*BattleHitItPtr())->info = kBattleHitInfoEnd;
}

void RegisterBattleSimFunctions() {
  RegisterNative(kAddrBattleRoll1RN, ThunkBattleRoll1RN);
  RegisterNative(kAddrBattleRoll2RN, ThunkBattleRoll2RN);
  RegisterNative(kAddrInitBattleUnit, ThunkInitBattleUnit);
  RegisterNative(kAddrInitBattleUnitWithoutBonuses,
                 ThunkInitBattleUnitWithoutBonuses);
  RegisterNative(kAddrClearBattleHits, ThunkClearBattleHits);
  RegisterNative(kAddrBattleUnwind, ThunkBattleUnwind);
  RegisterNative(kAddrBattleGetBattleUnitOrder, ThunkBattleGetBattleUnitOrder);
  RegisterNative(kAddrBattleGetFollowUpOrder, ThunkBattleGetFollowUpOrder);
  RegisterNative(kAddrBattleGenerateRoundHits, ThunkBattleGenerateRoundHits);
  RegisterNative(kAddrBattleGetEffectiveHitCount,
                 ThunkBattleGetEffectiveHitCount);
  RegisterNative(kAddrBattleCheckBrave, ThunkBattleCheckBrave);
  RegisterNative(kAddrBattleCheckTriangleAttackOrder,
                 ThunkBattleCheckTriangleAttackOrder);
  RegisterNative(kAddrBattleUpdateBattleStats, ThunkBattleUpdateBattleStats);
  RegisterNative(kAddrBattleGenerateHitAttributes,
                 ThunkBattleGenerateHitAttributes);
  RegisterNative(kAddrBattleGenerateHitTriangleAttack,
                 ThunkBattleGenerateHitTriangleAttack);
  RegisterNative(kAddrBattleGenerateHitEffects, ThunkBattleGenerateHitEffects);
  RegisterNative(kAddrBattleGenerateHit, ThunkBattleGenerateHit);
  RegisterNative(kAddrBattleUnitTargetSetEquippedWeapon,
                 ThunkBattleUnitTargetSetEquippedWeapon);
  RegisterNative(kAddrBattleUnitTargetCheckHasRealWeapon,
                 ThunkBattleUnitTargetCheckHasRealWeapon);
  RegisterNative(kAddrBattleInitTargetCanCounter,
                 ThunkBattleInitTargetCanCounter);
  RegisterNative(kAddrBattleHitAdvance, ThunkBattleHitAdvance);
  RegisterNative(kAddrBattleHitTerminate, ThunkBattleHitTerminate);
}

}  // namespace fe7
