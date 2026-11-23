#include "game/battle/battle_gen.h"

#include <cstdint>

#include "absl/base/nullability.h"
#include "game/battle.h"
#include "game/battle/battle.h"
#include "game/battle/battle_exp.h"
#include "game/battle/battle_sim.h"
#include "game/item/item.h"
#include "game/map/trap.h"
#include "game/unit.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

namespace {

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
    GbaCall(kAddrSetBattleUnitWeaponBallista, bu_actor.addr);
  } else {
    GbaCall(kAddrSetBattleUnitWeapon, bu_actor.addr,
            static_cast<uint32_t>(item_slot));
  }
  GbaCall(kAddrSetBattleUnitWeapon, bu_target.addr, 0xFFFFFFFFu);

  BattleInitTargetCanCounter();
  BattleApplyWeaponTriangleEffect(bu_actor, bu_target);
  GbaCall(kAddrDisableAllLightRunes);

  GbaCall(kAddrSetBattleUnitTerrainBonusesAuto, bu_actor.addr);
  GbaCall(kAddrSetBattleUnitTerrainBonusesAuto, bu_target.addr);

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
    GbaCall(kAddrSetBattleUnitWeaponBallista, bu_actor.addr);
  } else {
    GbaCall(kAddrSetBattleUnitWeapon, bu_actor.addr, 0xFFFFFFFFu);
  }
  GbaCall(kAddrSetBattleUnitWeapon, bu_target.addr, 0xFFFFFFFFu);

  BattleInitTargetCanCounter();
  BattleApplyWeaponTriangleEffect(bu_actor, bu_target);
  GbaCall(kAddrDisableAllLightRunes);

  GbaCall(kAddrSetBattleUnitTerrainBonusesAuto, bu_actor.addr);
  GbaCall(kAddrSetBattleUnitTerrainBonusesAuto, bu_target.addr);

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

  GbaCall(kAddrSetBattleUnitTerrainBonusesAuto, bu_actor.addr);
  GbaCall(kAddrSetBattleUnitWeapon, bu_actor.addr,
          static_cast<uint32_t>(static_cast<int32_t>(item_slot)));
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
}

}  // namespace fe7
