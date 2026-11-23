#include "game/battle/battle_gen.h"

#include <cstdint>

#include "absl/base/nullability.h"
#include "game/battle.h"
#include "game/battle/battle.h"
#include "game/battle/battle_exp.h"
#include "game/battle/battle_sim.h"
#include "game/item/item.h"
#include "game/map/ballista.h"
#include "game/map/map.h"
#include "game/map/trap.h"
#include "game/unit.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

namespace {

inline constexpr uint32_t kArenaDataAddr = 0x0203A7F4u;
inline constexpr uint32_t kBattleActionDataAddr = 0x0203A85Cu;

inline int AbsDiff(int a, int b) { return (a >= b) ? (a - b) : (b - a); }

}  // namespace

// ---------------------------------------------------------------------------
// 0x080281C8: BattleGenerateSimulationInternal
// ---------------------------------------------------------------------------
void BattleGenerateSimulationInternal(GbaPtr<Unit> actor, GbaPtr<Unit> target,
                                      int x, int y, int item_slot) {
  GbaPtr<BattleUnit> bu_actor(kBattleActorAddr);
  GbaPtr<BattleUnit> bu_target(kBattleTargetAddr);
  GbaPtr<BattleStats> stats(kBattleStatsAddr);

  InitBattleUnit(bu_actor, GbaPtr<const Unit>(actor.addr));
  InitBattleUnit(bu_target, GbaPtr<const Unit>(target.addr));

  bu_actor->unit.x = static_cast<int8_t>(x);
  bu_actor->unit.y = static_cast<int8_t>(y);
  stats->range =
      static_cast<uint8_t>(AbsDiff(bu_actor->unit.x, bu_target->unit.x) +
                           AbsDiff(bu_actor->unit.y, bu_target->unit.y));

  if ((stats->config & kBattleConfigBallista) != 0u) {
    SetBattleUnitWeaponBallista(bu_actor);
  } else {
    SetBattleUnitWeapon(bu_actor, item_slot);
  }
  SetBattleUnitWeapon(bu_target, -1);

  BattleInitTargetCanCounter();
  BattleApplyWeaponTriangleEffect(bu_actor, bu_target);
  DisableAllLightRunes();

  SetBattleUnitTerrainBonusesAuto(bu_actor);
  SetBattleUnitTerrainBonusesAuto(bu_target);

  BattleGenerate(actor, target);
  ApplyLightRuneTerrainAlt();
}

// ---------------------------------------------------------------------------
// 0x08028298: BattleGenerateRealInternal
// ---------------------------------------------------------------------------
void BattleGenerateRealInternal(GbaPtr<Unit> actor, GbaPtr<Unit> target) {
  GbaPtr<BattleUnit> bu_actor(kBattleActorAddr);
  GbaPtr<BattleUnit> bu_target(kBattleTargetAddr);
  GbaPtr<BattleStats> stats(kBattleStatsAddr);

  InitBattleUnit(bu_actor, GbaPtr<const Unit>(actor.addr));
  InitBattleUnit(bu_target, GbaPtr<const Unit>(target.addr));

  stats->range =
      static_cast<uint8_t>(AbsDiff(bu_actor->unit.x, bu_target->unit.x) +
                           AbsDiff(bu_actor->unit.y, bu_target->unit.y));

  if ((stats->config & kBattleConfigBallista) != 0u) {
    SetBattleUnitWeaponBallista(bu_actor);
  } else {
    SetBattleUnitWeapon(bu_actor, -1);
  }
  SetBattleUnitWeapon(bu_target, -1);

  BattleInitTargetCanCounter();
  BattleApplyWeaponTriangleEffect(bu_actor, bu_target);
  DisableAllLightRunes();

  SetBattleUnitTerrainBonusesAuto(bu_actor);
  SetBattleUnitTerrainBonusesAuto(bu_target);

  BattleGenerate(actor, target);
  ApplyLightRuneTerrainAlt();

  BattleUnitTargetCheckHasRealWeapon(bu_target);
  BattleUnitTargetSetEquippedWeapon(bu_target);

  if (bu_target->unit.id != 0) {
    BattleApplyExpGains();
    GbaCall(kAddrRecordBattleStatsAfterCombat);
    GbaCall(kAddrRecordUnitBattleStats, actor.addr);
    GbaCall(kAddrRecordUnitBattleStats, target.addr);
  }
}

// ---------------------------------------------------------------------------
// 0x08028380: BattleApplyGameStateUpdates
// ---------------------------------------------------------------------------
void BattleApplyGameStateUpdates() {
  BattleApplyUnitUpdates();
  BattleApplyBallistaUpdates();
  GbaCall(kAddrBattleApplySupportBonusesAfter, kBattleActorAddr,
          kBattleTargetAddr);
  GbaCall(kAddrBattleApplyMiscUpdates);
}

// ---------------------------------------------------------------------------
// 0x080283A4: BattleGenerateSimulation
// ---------------------------------------------------------------------------
void BattleGenerateSimulation(GbaPtr<Unit> actor, GbaPtr<Unit> target, int x,
                              int y, int item_slot) {
  if (x < 0 && y < 0) {
    x = actor->x;
    y = actor->y;
  }
  GbaPtr<BattleStats>(kBattleStatsAddr)->config = kBattleConfigSimulate;
  BattleGenerateSimulationInternal(actor, target, x, y, item_slot);
}

// ---------------------------------------------------------------------------
// 0x080283DC: BattleGenerateReal
// ---------------------------------------------------------------------------
void BattleGenerateReal(GbaPtr<Unit> actor, GbaPtr<Unit> target) {
  GbaPtr<BattleStats>(kBattleStatsAddr)->config = kBattleConfigReal;
  BattleGenerateRealInternal(actor, target);
}

// ---------------------------------------------------------------------------
// 0x080283F0: BattleGenerateBallistaSimulation
// ---------------------------------------------------------------------------
void BattleGenerateBallistaSimulation(GbaPtr<Unit> actor, GbaPtr<Unit> target,
                                      int x, int y) {
  GbaPtr<BattleStats>(kBattleStatsAddr)->config =
      kBattleConfigSimulate | kBattleConfigBallista;
  BattleGenerateSimulationInternal(actor, target, x, y, 0);
}

// ---------------------------------------------------------------------------
// 0x08028410: BattleGenerateBallistaReal
// ---------------------------------------------------------------------------
void BattleGenerateBallistaReal(GbaPtr<Unit> actor, GbaPtr<Unit> target) {
  GbaPtr<BattleStats>(kBattleStatsAddr)->config =
      kBattleConfigReal | kBattleConfigBallista;
  BattleGenerateRealInternal(actor, target);
}

// ---------------------------------------------------------------------------
// 0x08028424: BattleGenerate
// ---------------------------------------------------------------------------
void BattleGenerate(GbaPtr<Unit> /*actor*/, GbaPtr<Unit> target) {
  GbaPtr<BattleUnit> bu_actor(kBattleActorAddr);
  GbaPtr<BattleUnit> bu_target(kBattleTargetAddr);
  GbaPtr<BattleStats> stats(kBattleStatsAddr);

  ComputeBattleUnitStats(bu_actor, bu_target);
  ComputeBattleUnitStats(bu_target, bu_actor);
  ComputeBattleUnitEffectiveStats(bu_actor, bu_target);
  ComputeBattleUnitEffectiveStats(bu_target, bu_actor);

  if (!target) {
    GbaCall(kAddrComputeBattleObstacleStats);
  }

  if ((stats->config & kBattleConfigReal) != 0u &&
      *GbaPtr<const uint32_t>(kBattleActionDataAddr + 0x18u) != 0u) {
    GbaCall(kAddrBattleUnwindScripted);
  } else {
    BattleUnwind();
  }
}

// ---------------------------------------------------------------------------
// 0x0802848C: BattleGenerateDisplayStats
// ---------------------------------------------------------------------------
void BattleGenerateDisplayStats(GbaPtr<Unit> unit, int8_t item_slot) {
  GbaPtr<BattleStats>(kBattleStatsAddr)->config = kBattleConfigBit2;

  GbaPtr<BattleUnit> bu_target(kBattleTargetAddr);
  bu_target->weapon = 0u;
  bu_target->weapon_attributes = 0u;
  bu_target->weapon_type = 0xFFu;
  bu_target->unit.jinfo = GbaPtr<const JInfo>(0);

  GbaPtr<BattleUnit> bu_actor(kBattleActorAddr);
  bu_actor->wtriangle_hit_bonus = 0;
  bu_actor->wtriangle_dmg_bonus = 0;

  if (static_cast<uint8_t>(item_slot) <= 4u) {
    // Equip into slot 0 on a temporary snapshot without mutating `unit`.
    uint16_t saved_items[kUnitItemCount];
    for (int i = 0; i < kUnitItemCount; ++i) {
      saved_items[i] = unit->items[i];
    }
    UnitEquipItemSlot(unit, item_slot);
    item_slot = 0;
    InitBattleUnit(bu_actor, GbaPtr<const Unit>(unit.addr));
    for (int i = 0; i < kUnitItemCount; ++i) {
      unit->items[i] = saved_items[i];
    }
  } else {
    InitBattleUnit(bu_actor, GbaPtr<const Unit>(unit.addr));
  }

  SetBattleUnitTerrainBonusesAuto(bu_actor);
  SetBattleUnitWeapon(bu_actor, static_cast<int>(item_slot));
  ComputeBattleUnitStats(bu_actor, bu_target);

  if (GetItemIid(bu_actor->weapon) == 0x11) {
    const int half_pow = bu_actor->unit.pow / 2;
    bu_actor->battle_attack =
        static_cast<int16_t>(bu_actor->battle_attack - half_pow);
    bu_actor->battle_crit_rate = 0;
    bu_actor->battle_effective_crit_rate = 0;
  }

  if (bu_actor->weapon == 0u) {
    bu_actor->battle_attack = 0xFF;
    bu_actor->battle_hit_rate = 0xFF;
    bu_actor->battle_crit_rate = 0xFF;
  }

  if (GetItemWeaponEffect(bu_actor->weapon) == 3) {
    bu_actor->battle_attack = 0xFF;
  }
}

// ---------------------------------------------------------------------------
// 0x08028728: SetBattleUnitTerrainBonusesAuto
// ---------------------------------------------------------------------------
void SetBattleUnitTerrainBonusesAuto(GbaPtr<BattleUnit> bu) {
  const GbaPtr<const GbaPtr<const uint8_t>> terrain_map =
      *GbaPtr<const GbaPtr<const GbaPtr<const uint8_t>>>(kMapTerrainAddr);
  const uint8_t terrain = terrain_map[bu->unit.y][bu->unit.x];
  bu->terrain_id = terrain;
  bu->terrain_avoid =
      static_cast<int8_t>(bu->unit.jinfo->avo_terrain_table[terrain]);
  bu->terrain_defense =
      static_cast<int8_t>(bu->unit.jinfo->def_terrain_table[terrain]);
  bu->terrain_resistance =
      static_cast<int8_t>(bu->unit.jinfo->res_terrain_table[terrain]);
}

// ---------------------------------------------------------------------------
// 0x0802877C: SetBattleUnitWeapon
// ---------------------------------------------------------------------------
void SetBattleUnitWeapon(GbaPtr<BattleUnit> bu, int item_slot) {
  if (item_slot == -1) {
    item_slot = GetUnitEquippedWeaponSlot(GbaPtr<const Unit>(bu.addr));
  }

  if ((bu->unit.flags & kUnitFlagInBallista) != 0u &&
      GetBallistaItemAtPos(bu->unit.x, bu->unit.y) != 0) {
    item_slot = 8;
  }

  bu->can_counter = 1;

  switch (static_cast<uint32_t>(item_slot)) {
    case 0u:
    case 1u:
    case 2u:
    case 3u:
    case 4u:
      bu->weapon_slot_index = static_cast<uint8_t>(item_slot);
      bu->weapon = bu->unit.items[item_slot];
      break;
    case 5u:
      bu->weapon_slot_index = 0xFFu;
      bu->weapon = *GbaPtr<const uint16_t>(kBmStatusAddr + 0x2Cu);
      break;
    case 6u:
      bu->weapon_slot_index = 0u;
      bu->weapon = *GbaPtr<const uint16_t>(kArenaDataAddr + 0x1Au);
      bu->can_counter = 0;
      break;
    case 7u:
      bu->weapon_slot_index = 0u;
      bu->weapon = *GbaPtr<const uint16_t>(kArenaDataAddr + 0x1Cu);
      bu->can_counter = 0;
      break;
    case 8u:
      bu->weapon_slot_index = 0xFFu;
      bu->weapon =
          static_cast<uint16_t>(GetBallistaItemAtPos(bu->unit.x, bu->unit.y));
      bu->can_counter = 0;
      break;
    default:
      bu->weapon_slot_index = 0xFFu;
      bu->weapon = 0u;
      bu->can_counter = 0;
      break;
  }

  bu->weapon_before = bu->weapon;
  bu->weapon_attributes = GetItemAttributes(bu->weapon);
  bu->weapon_type = static_cast<uint8_t>(GetItemKind(bu->weapon));

  const GbaPtr<const BattleStats> stats(kBattleStatsAddr);
  if ((stats->config & kBattleConfigBit2) == 0u) {
    if ((bu->weapon_attributes & 0x40u) != 0u) {
      const int iid = GetItemIid(bu->weapon);
      if (iid == 0x11) {
        bu->weapon_type = 7u;
      } else if (iid == 0x10) {
        if (stats->range == 2u) {
          bu->weapon_type = 6u;
        } else {
          bu->weapon_attributes &= ~0x40u;
        }
      } else if (iid == 0x99) {
        if (stats->range == 2u) {
          bu->weapon_type = 5u;
        } else {
          bu->weapon_attributes &= ~0x40u;
        }
      }
    }

    if (!CanItemReachDistance(bu->weapon, stats->range) ||
        bu->weapon_slot_index == 0xFFu) {
      bu->weapon = 0u;
      bu->can_counter = 0;
    }

    if (bu->unit.status == static_cast<uint8_t>(UnitStatus::kSleep)) {
      bu->weapon = 0u;
      bu->can_counter = 0;
    }
  }
}

// ---------------------------------------------------------------------------
// 0x0802896C: SetBattleUnitWeaponBallista
// ---------------------------------------------------------------------------
void SetBattleUnitWeaponBallista(GbaPtr<BattleUnit> bu) {
  const uint16_t item =
      static_cast<uint16_t>(GetBallistaItemAtPos(bu->unit.x, bu->unit.y));
  bu->weapon = item;
  bu->weapon_before = item;
  bu->weapon_attributes = GetItemAttributes(item);
  bu->weapon_type = static_cast<uint8_t>(GetItemKind(item));
  bu->can_counter = 0;
}

// ---------------------------------------------------------------------------
// Native dispatch thunks
// ---------------------------------------------------------------------------
namespace {

uint32_t ThunkBattleGenerateSimulationInternal(
    uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
    const uint32_t* absl_nullable stack_args) {
  const int slot = stack_args != nullptr ? static_cast<int>(stack_args[0]) : 0;
  BattleGenerateSimulationInternal(GbaPtr<Unit>(r0), GbaPtr<Unit>(r1),
                                   static_cast<int>(r2), static_cast<int>(r3),
                                   slot);
  return 0u;
}
uint32_t ThunkBattleGenerateRealInternal(uint32_t r0, uint32_t r1, uint32_t,
                                         uint32_t,
                                         const uint32_t* absl_nullable) {
  BattleGenerateRealInternal(GbaPtr<Unit>(r0), GbaPtr<Unit>(r1));
  return 0u;
}
uint32_t ThunkBattleApplyGameStateUpdates(uint32_t, uint32_t, uint32_t,
                                          uint32_t,
                                          const uint32_t* absl_nullable) {
  BattleApplyGameStateUpdates();
  return 0u;
}
uint32_t ThunkBattleGenerateSimulation(
    uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
    const uint32_t* absl_nullable stack_args) {
  const int slot = stack_args != nullptr ? static_cast<int>(stack_args[0]) : 0;
  BattleGenerateSimulation(GbaPtr<Unit>(r0), GbaPtr<Unit>(r1),
                           static_cast<int>(r2), static_cast<int>(r3), slot);
  return 0u;
}
uint32_t ThunkBattleGenerateReal(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                                 const uint32_t* absl_nullable) {
  BattleGenerateReal(GbaPtr<Unit>(r0), GbaPtr<Unit>(r1));
  return 0u;
}
uint32_t ThunkBattleGenerateBallistaSimulation(uint32_t r0, uint32_t r1,
                                               uint32_t r2, uint32_t r3,
                                               const uint32_t* absl_nullable) {
  BattleGenerateBallistaSimulation(GbaPtr<Unit>(r0), GbaPtr<Unit>(r1),
                                   static_cast<int>(r2), static_cast<int>(r3));
  return 0u;
}
uint32_t ThunkBattleGenerateBallistaReal(uint32_t r0, uint32_t r1, uint32_t,
                                         uint32_t,
                                         const uint32_t* absl_nullable) {
  BattleGenerateBallistaReal(GbaPtr<Unit>(r0), GbaPtr<Unit>(r1));
  return 0u;
}
uint32_t ThunkBattleGenerate(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                             const uint32_t* absl_nullable) {
  BattleGenerate(GbaPtr<Unit>(r0), GbaPtr<Unit>(r1));
  return 0u;
}
uint32_t ThunkBattleGenerateDisplayStats(uint32_t r0, uint32_t r1, uint32_t,
                                         uint32_t,
                                         const uint32_t* absl_nullable) {
  BattleGenerateDisplayStats(GbaPtr<Unit>(r0), static_cast<int8_t>(r1));
  return 0u;
}
uint32_t ThunkSetBattleUnitTerrainBonusesAuto(uint32_t r0, uint32_t, uint32_t,
                                              uint32_t,
                                              const uint32_t* absl_nullable) {
  SetBattleUnitTerrainBonusesAuto(GbaPtr<BattleUnit>(r0));
  return 0u;
}
uint32_t ThunkSetBattleUnitWeapon(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                                  const uint32_t* absl_nullable) {
  SetBattleUnitWeapon(GbaPtr<BattleUnit>(r0), static_cast<int32_t>(r1));
  return 0u;
}
uint32_t ThunkSetBattleUnitWeaponBallista(uint32_t r0, uint32_t, uint32_t,
                                          uint32_t,
                                          const uint32_t* absl_nullable) {
  SetBattleUnitWeaponBallista(GbaPtr<BattleUnit>(r0));
  return 0u;
}

}  // namespace

void RegisterBattleGenFunctions() {
  RegisterNative(kAddrBattleGenerateSimulationInternal,
                 &ThunkBattleGenerateSimulationInternal);
  RegisterNative(kAddrBattleGenerateRealInternal,
                 &ThunkBattleGenerateRealInternal);
  RegisterNative(kAddrBattleApplyGameStateUpdates,
                 &ThunkBattleApplyGameStateUpdates);
  RegisterNative(kAddrBattleGenerateSimulation, &ThunkBattleGenerateSimulation);
  RegisterNative(kAddrBattleGenerateReal, &ThunkBattleGenerateReal);
  RegisterNative(kAddrBattleGenerateBallistaSimulation,
                 &ThunkBattleGenerateBallistaSimulation);
  RegisterNative(kAddrBattleGenerateBallistaReal,
                 &ThunkBattleGenerateBallistaReal);
  RegisterNative(kAddrBattleGenerate, &ThunkBattleGenerate);
  RegisterNative(kAddrBattleGenerateDisplayStats,
                 &ThunkBattleGenerateDisplayStats);
  RegisterNative(kAddrSetBattleUnitTerrainBonusesAuto,
                 &ThunkSetBattleUnitTerrainBonusesAuto);
  RegisterNative(kAddrSetBattleUnitWeapon, &ThunkSetBattleUnitWeapon);
  RegisterNative(kAddrSetBattleUnitWeaponBallista,
                 &ThunkSetBattleUnitWeaponBallista);
}

}  // namespace fe7
