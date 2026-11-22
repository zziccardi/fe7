#include "game/ai/ai_combat.h"

#include <cstdint>

#include "absl/base/nullability.h"
#include "game/ai/ai.h"
#include "game/ai/ai_actions.h"
#include "game/ai/ai_decide.h"
#include "game/ai/ai_opcodes.h"
#include "game/ai/ai_script.h"
#include "game/item/item.h"
#include "game/map/map.h"
#include "game/phase/phase.h"
#include "game/unit.h"
#include "game/unit/unit.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

namespace {

inline GbaPtr<Unit> ActiveUnit() {
  return *GbaPtr<GbaPtr<Unit>>(kAiActiveUnitAddr);
}

inline GbaPtr<const Unit> ConstUnit(GbaPtr<Unit> u) {
  return GbaPtr<const Unit>(u.addr);
}

inline uint8_t ActiveUnitId() { return *GbaPtr<uint8_t>(kAiActiveUnitIdAddr); }

inline GbaPtr<AiDecision> Decision() {
  return GbaPtr<AiDecision>(kAiDecisionAddr);
}

inline MapSize GetMapSize() { return *GbaPtr<MapSize>(kMapSizeAddr); }

inline GbaPtr<GbaPtr<uint8_t>> MapMovement() {
  return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr);
}

inline GbaPtr<GbaPtr<uint8_t>> MapRange() {
  return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapRangeAddr);
}

inline GbaPtr<GbaPtr<uint8_t>> MapUnit() {
  return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr);
}

inline GbaPtr<GbaPtr<uint8_t>> MapTerrain() {
  return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr);
}

inline GbaPtr<GbaPtr<uint8_t>> MapOther() {
  return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kAiMapOtherAddr);
}

// Computes the combat position score for `(x, y)` against `target_id`.
uint32_t ComputeCombatPositionScore(int x, int y, uint8_t target_id) {
  GbaPtr<Unit> target = GetUnit(target_id);
  int score = AiGetInRangeCombatPositionScoreComponent(x, y, ConstUnit(target));
  score += AiGetTerrainCombatPositionScoreComponent(x, y);
  score += AiGetFriendZonePositionScoreComponent(x, y);
  score -= static_cast<int>(static_cast<int8_t>(MapMovement()[y][x]));
  score -= static_cast<int>(MapOther()[y][x] >> 3);
  return static_cast<uint32_t>(score) + 0x7FFFFFFFu;
}

// Native-memory version of AiSimulateBattleAndComputeScore (avoids GBA stack).
bool SimulateBattleAndComputeScoreImpl(AiCombatSimulationSt* st) {
  GbaPtr<Unit> target = GetUnit(st->target_id);
  if (st->item_slot != 0xFFFFu) {
    const uint32_t slot_arg = st->item_slot;
    GbaCall(kAddrBattleGenerateSimulation, ActiveUnit().addr, target.addr,
            st->x_move, st->y_move, &slot_arg, 1);
  } else {
    GbaCall(kAddrBattleGenerateBallistaSimulation, ActiveUnit().addr,
            target.addr, st->x_move, st->y_move);
  }

  // Matches AiIsCombatSimInvalid (always false) + AiComputeCombatScore.
  // AiComputeCombatScore writes `st->score` from gBattleActor / gBattleTarget.
  // Compute into a temporary GbaPtr? No — AiComputeCombatScore only reads
  // gBattleActor/gBattleTarget and writes `st->score`! Compute inline using
  // the exact AiGet*ScoreComponent() functions from game/ai/ai.h.
  int score = AiGetDamageDealtCombatScoreComponent();
  score += AiGetOpponentLowHpScoreComponent();
  score += AiGetFriendZoneCombatScoreComponent();
  score += AiGetTargetClassCombatScoreComponent();
  score += AiGetTurnCombatScoreComponent();
  score -= AiGetDamageTakenScoreComponent();
  score -= AiGetDangerScoreComponent();
  score -= AiGetLowHpScoreComponent();
  if (score < 0) {
    score = 0;
  }
  st->score = static_cast<uint32_t>(score);
  return true;
}

bool SimulateBestBattleAgainstTargetImpl(AiCombatSimulationSt* st) {
  uint32_t best_pos_score = 0;
  const MapSize sz = GetMapSize();
  for (int iy = sz.y - 1; iy >= 0; --iy) {
    for (int ix = sz.x - 1; ix >= 0; --ix) {
      if (MapMovement()[iy][ix] > 0x78u) {
        continue;
      }
      if (static_cast<int8_t>(MapRange()[iy][ix]) == 0) {
        continue;
      }
      const uint8_t occ = MapUnit()[iy][ix];
      if (occ != 0u && occ != ActiveUnitId()) {
        continue;
      }
      const uint32_t pos_score =
          ComputeCombatPositionScore(ix, iy, st->target_id);
      if (pos_score > best_pos_score) {
        st->x_move = static_cast<uint8_t>(ix);
        st->y_move = static_cast<uint8_t>(iy);
        best_pos_score = pos_score;
      }
    }
  }
  if (best_pos_score == 0u) {
    return false;
  }
  return SimulateBattleAndComputeScoreImpl(st);
}

bool SimulateBestBallistaBattleAgainstTargetImpl(AiCombatSimulationSt* st,
                                                 uint16_t item) {
  uint32_t best_pos_score = 0;
  const MapSize sz = GetMapSize();
  const uint8_t item_lo = static_cast<uint8_t>(item & 0xFFu);
  for (int iy = sz.y - 1; iy >= 0; --iy) {
    for (int ix = sz.x - 1; ix >= 0; --ix) {
      if (MapMovement()[iy][ix] > 0x78u) {
        continue;
      }
      if (static_cast<int>(static_cast<int8_t>(MapMovement()[iy][ix])) !=
          static_cast<int>(item_lo)) {
        continue;
      }
      if (static_cast<int8_t>(MapRange()[iy][ix]) == 0) {
        continue;
      }
      const uint8_t occ = MapUnit()[iy][ix];
      if (occ != 0u && occ != ActiveUnitId()) {
        continue;
      }
      const uint32_t pos_score =
          ComputeCombatPositionScore(ix, iy, st->target_id);
      if (pos_score > best_pos_score) {
        st->x_move = static_cast<uint8_t>(ix);
        st->y_move = static_cast<uint8_t>(iy);
        best_pos_score = pos_score;
      }
    }
  }
  if (best_pos_score == 0u) {
    return false;
  }
  return SimulateBattleAndComputeScoreImpl(st);
}

bool SimulateBallistaAttackImpl(uint32_t pred_addr, AiCombatSimulationSt* out) {
  uint8_t ballista_iids[3] = {
      *GbaPtr<const uint8_t>(kAiBallistaItemIdsAddr + 0u),
      *GbaPtr<const uint8_t>(kAiBallistaItemIdsAddr + 1u),
      *GbaPtr<const uint8_t>(kAiBallistaItemIdsAddr + 2u),
  };

  int found_count = 0;
  const MapSize sz = GetMapSize();
  for (int iy = sz.y - 1; iy >= 0; --iy) {
    for (int ix = sz.x - 1; ix >= 0; --ix) {
      if (MapMovement()[iy][ix] > 0x78u) {
        continue;
      }
      const uint16_t item = static_cast<uint16_t>(
          GbaCall(kAddrGetEmptyBallistaItemAt, static_cast<uint32_t>(ix),
                  static_cast<uint32_t>(iy)));
      if (item != 0u) {
        ++found_count;
        MapMovement()[iy][ix] = static_cast<uint8_t>(item);
      } else {
        MapMovement()[iy][ix] = 0xFFu;
      }
    }
  }

  if (found_count == 0) {
    return false;
  }

  out->target_id = 0;
  out->score = 0;
  AiCombatSimulationSt sim{};

  for (int b = 0; b < 3; ++b) {
    const uint8_t item = ballista_iids[b];
    out->item_slot = 0xFFFFu;
    for (int uid = 1; uid <= 0xBF; ++uid) {
      GbaPtr<Unit> target = GetUnit(uid);
      if (!target || !target->pinfo) {
        continue;
      }
      if ((target->flags & 0x00010025u) != 0u) {
        continue;
      }
      if ((GbaCall(pred_addr & ~1u, target.addr) & 0xFFu) == 0u) {
        continue;
      }
      if (!AiCouldReachByBirdsEyeDistance(ConstUnit(ActiveUnit()),
                                          ConstUnit(target), item)) {
        continue;
      }
      AiFillTargetWeaponRangeMap(ConstUnit(target), item);
      sim.target_id = static_cast<uint8_t>(target->id);
      sim.item_slot = 0xFFFFu;
      if (!SimulateBestBallistaBattleAgainstTargetImpl(&sim, item)) {
        continue;
      }
      if (sim.score >= out->score) {
        out->x_move = sim.x_move;
        out->y_move = sim.y_move;
        out->target_id = sim.target_id;
        out->score = sim.score;
      }
    }
  }

  return (out->score != 0u || out->target_id != 0u);
}

// Shared tail of AiPerformAttack & AiPerformAttackInCurrentMovement.
void PerformAttackWeaponAndBallistaLoop(uint32_t pred_addr,
                                        bool skip_weapon_loop) {
  AiCombatSimulationSt best{};
  AiCombatSimulationSt sim{};
  best.target_id = 0;
  best.score = 0;

  if (!skip_weapon_loop) {
    GbaCall(kAddrSetWorkingBmMap, MapRange().addr);
    for (int slot = 0; slot < 5; ++slot) {
      const uint16_t item = ActiveUnit()->items[slot];
      if (item == 0u) {
        break;
      }
      if (!CanUnitUseWeapon(ConstUnit(ActiveUnit()), item)) {
        continue;
      }
      sim.item_slot = static_cast<uint16_t>(slot);
      for (int uid = 1; uid <= 0xBF; ++uid) {
        GbaPtr<Unit> target = GetUnit(uid);
        if (!target || !target->pinfo) {
          continue;
        }
        if ((target->flags & 0x00010025u) != 0u) {
          continue;
        }
        if ((GbaCall(pred_addr & ~1u, target.addr) & 0xFFu) == 0u) {
          continue;
        }
        if (!AiCouldReachByBirdsEyeDistance(ConstUnit(ActiveUnit()),
                                            ConstUnit(target), item)) {
          continue;
        }
        AiFillTargetWeaponRangeMap(ConstUnit(target), item);
        sim.target_id = static_cast<uint8_t>(target->id);
        if (!SimulateBestBattleAgainstTargetImpl(&sim)) {
          continue;
        }
        if (sim.score >= best.score) {
          best = sim;
          best.item_slot = static_cast<uint16_t>(slot);
        }
      }
    }
  }

  if ((UnitAttributes(*ActiveUnit()) & kUnitAttrBallista) != 0u) {
    if (SimulateBallistaAttackImpl(pred_addr, &sim) &&
        sim.score >= best.score) {
      best = sim;
    }
  }

  if (best.score != 0u || best.target_id != 0u) {
    AiSetDecision(best.x_move, best.y_move, 1, best.target_id,
                  static_cast<uint8_t>(best.item_slot), 0, 0);
    if (static_cast<int8_t>(best.item_slot) != -1) {
      GbaCall(kAddrRemoveUnitFromBallista, ActiveUnit().addr);
    }
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// 0x08038558: AiPerformAttack
// ---------------------------------------------------------------------------
void AiPerformAttack(uint32_t pred_addr) {
  GbaPtr<Unit> unit = ActiveUnit();
  bool skip_weapon_loop = false;

  if ((unit->flags & kUnitFlagInBallista) != 0u) {
    MapFill(MapMovement(), 0xFFu);
    MapMovement()[unit->y][unit->x] = 0u;
    if (GbaCall(kAddrGetBallistaItemAt, static_cast<uint32_t>(unit->x),
                static_cast<uint32_t>(unit->y)) != 0u) {
      skip_weapon_loop = true;
    } else {
      GbaCall(kAddrRemoveUnitFromBallista, unit.addr);
    }
  } else {
    if ((UnitAttributes(*unit) & kUnitAttrSteal) != 0u &&
        GetUnitItemCount(ConstUnit(unit)) <= 4) {
      MapFloodUnit(ConstUnit(unit));
      GbaCall(kAddrMapFloodRangeExtend);
      if (AiTryStealAction()) {
        return;
      }
    }
    if ((*GbaPtr<const uint8_t>(kAiStateAddr + 0x7Bu) & 2u) != 0u) {
      MapFill(MapMovement(), 0xFFu);
      MapMovement()[unit->y][unit->x] = 0u;
    } else {
      MapFloodUnit(ConstUnit(unit));
    }
    if (UnitKnowsMagic(ConstUnit(unit))) {
      GbaCall(kAddrMapClearOther, 0xFFFFFFFFu);
    }
  }

  PerformAttackWeaponAndBallistaLoop(pred_addr, skip_weapon_loop);
}

// ---------------------------------------------------------------------------
// 0x080387B0: AiPerformAttackInCurrentMovement
// ---------------------------------------------------------------------------
void AiPerformAttackInCurrentMovement(uint32_t pred_addr) {
  GbaPtr<Unit> unit = ActiveUnit();
  bool skip_weapon_loop = false;

  if ((unit->flags & kUnitFlagInBallista) != 0u) {
    MapFill(MapMovement(), 0xFFu);
    MapMovement()[unit->y][unit->x] = 0u;
    if (GbaCall(kAddrGetBallistaItemAt, static_cast<uint32_t>(unit->x),
                static_cast<uint32_t>(unit->y)) != 0u) {
      skip_weapon_loop = true;
    } else {
      GbaCall(kAddrRemoveUnitFromBallista, unit.addr);
    }
  }

  PerformAttackWeaponAndBallistaLoop(pred_addr, skip_weapon_loop);
}

// ---------------------------------------------------------------------------
// 0x08038964: AiFillTargetWeaponRangeMap
// ---------------------------------------------------------------------------
void AiFillTargetWeaponRangeMap(GbaPtr<const Unit> target, uint16_t item) {
  MapFill(MapRange(), 0);
  const int16_t min_r = static_cast<int16_t>(GetItemMinRange(item));
  const int16_t max_r = static_cast<int16_t>(GetItemMaxRange(item));
  GbaCall(kAddrMapAddInBoundedRange, static_cast<uint32_t>(target->x),
          static_cast<uint32_t>(target->y), static_cast<uint32_t>(min_r),
          static_cast<uint32_t>(max_r));
}

// ---------------------------------------------------------------------------
// 0x080389B8: AiMakeRangeMapForUnitAndMov
// ---------------------------------------------------------------------------
void AiMakeRangeMapForUnitAndMov(GbaPtr<const Unit> unit, uint16_t mov,
                                 uint16_t item) {
  SetWorkingMoveTable(GetUnitMovementCost(unit));
  GbaCall(kAddrSetWorkingBmMap, MapMovement().addr);
  BeginMapFlood(unit->x, unit->y, mov, unit->id);
  MapFill(MapRange(), 0);

  const MapSize sz = GetMapSize();
  for (int iy = sz.y - 1; iy >= 0; --iy) {
    for (int ix = sz.x - 1; ix >= 0; --ix) {
      if (MapMovement()[iy][ix] > 0x78u) {
        continue;
      }
      const int16_t min_r = static_cast<int16_t>(GetItemMinRange(item));
      const int16_t max_r = static_cast<int16_t>(GetItemMaxRange(item));
      GbaCall(kAddrMapAddInBoundedRange,
              static_cast<uint32_t>(static_cast<int16_t>(ix)),
              static_cast<uint32_t>(static_cast<int16_t>(iy)),
              static_cast<uint32_t>(min_r), static_cast<uint32_t>(max_r));
    }
  }
}

// ---------------------------------------------------------------------------
// 0x08038A7C: AiSimulateBallistaAttack
// ---------------------------------------------------------------------------
bool AiSimulateBallistaAttack(uint32_t pred_addr,
                              GbaPtr<AiCombatSimulationSt> out) {
  AiCombatSimulationSt tmp = *out;
  const bool ok = SimulateBallistaAttackImpl(pred_addr, &tmp);
  *out = tmp;
  return ok;
}

// ---------------------------------------------------------------------------
// 0x08038BEC: AiGetStealPositionScore
// ---------------------------------------------------------------------------
uint8_t AiGetStealPositionScore(int x, int y) {
  if (static_cast<int8_t>(MapMovement()[y][x]) > 0x77) {
    return 0xFFu;
  }
  const uint8_t occ = MapUnit()[y][x];
  if (occ != 0u && occ != ActiveUnitId()) {
    return 0xFFu;
  }
  return MapMovement()[y][x];
}

// ---------------------------------------------------------------------------
// 0x08038C44: AiTryStealAction
// ---------------------------------------------------------------------------
bool AiTryStealAction() {
  uint8_t best_slot = 0;
  uint8_t best_prio = 0xFFu;
  uint8_t best_uid = 0;
  int16_t best_x = 0;
  int16_t best_y = 0;

  const MapSize sz = GetMapSize();
  for (int iy = sz.y - 1; iy >= 0; --iy) {
    for (int ix = sz.x - 1; ix >= 0; --ix) {
      if (MapMovement()[iy][ix] > 0x78u) {
        continue;
      }
      const uint8_t uid = MapUnit()[iy][ix];
      if (uid == 0u || AreUnitIdsAllied(ActiveUnitId(), uid)) {
        continue;
      }
      // Inline AiFindBestAdjacentPosition(ix, iy, AiGetStealPositionScore) to
      // avoid GBA stack scratch.
      uint8_t adj_best = 0xFFu;
      int16_t adj_x = 0;
      int16_t adj_y = 0;
      for (int d = 0; d < 4; ++d) {
        const int nx =
            ix + *GbaPtr<const int8_t>(kAiCardinalOffsetsAddr +
                                       static_cast<uint32_t>(d) * 2u);
        const int ny =
            iy + *GbaPtr<const int8_t>(kAiCardinalOffsetsAddr +
                                       static_cast<uint32_t>(d) * 2u + 1u);
        if (nx < 0 || ny < 0 || nx >= sz.x || ny >= sz.y) {
          continue;
        }
        const uint8_t s = AiGetStealPositionScore(nx, ny);
        if (s < adj_best) {
          adj_best = s;
          adj_x = static_cast<int16_t>(nx);
          adj_y = static_cast<int16_t>(ny);
        }
      }
      if (adj_best == 0xFFu) {
        continue;
      }

      GbaPtr<Unit> target = GetUnit(uid);
      if (ActiveUnit()->spd < target->spd) {
        continue;
      }
      const int8_t slot =
          static_cast<int8_t>(AiGetBestStealableItemSlot(ConstUnit(target)));
      if (slot < 0) {
        continue;
      }
      const uint8_t iid = static_cast<uint8_t>(target->items[slot] & 0xFFu);
      const uint8_t prio = static_cast<uint8_t>(AiGetStealItemPriority(iid));
      if (prio <= best_prio) {
        best_prio = prio;
        best_x = adj_x;
        best_y = adj_y;
        best_uid = MapUnit()[iy][ix];
        best_slot = static_cast<uint8_t>(slot);
      }
    }
  }

  if (best_prio == 0xFFu) {
    return false;
  }

  ActiveUnit()->unk_46 = static_cast<uint8_t>(ActiveUnit()->unk_46 + 1u);
  AiSetDecision(best_x, best_y, 3, best_uid, best_slot, 0, 0);
  return true;
}

// ---------------------------------------------------------------------------
// 0x08038DA4: AiSimulateBestBattleAgainstTarget
// ---------------------------------------------------------------------------
bool AiSimulateBestBattleAgainstTarget(GbaPtr<AiCombatSimulationSt> st) {
  AiCombatSimulationSt tmp = *st;
  const bool ok = SimulateBestBattleAgainstTargetImpl(&tmp);
  *st = tmp;
  return ok;
}

// ---------------------------------------------------------------------------
// 0x08038E64: AiSimulateBestBallistaBattleAgainstTarget
// ---------------------------------------------------------------------------
bool AiSimulateBestBallistaBattleAgainstTarget(GbaPtr<AiCombatSimulationSt> st,
                                               uint16_t item) {
  AiCombatSimulationSt tmp = *st;
  const bool ok = SimulateBestBallistaBattleAgainstTargetImpl(&tmp, item);
  *st = tmp;
  return ok;
}

// ---------------------------------------------------------------------------
// 0x08038F38: AiGetCombatPositionScore
// ---------------------------------------------------------------------------
uint32_t AiGetCombatPositionScore(int x, int y,
                                  GbaPtr<const AiCombatSimulationSt> st) {
  return ComputeCombatPositionScore(x, y, st->target_id);
}

// ---------------------------------------------------------------------------
// 0x08038FA0: AiIsCombatSimInvalid
// ---------------------------------------------------------------------------
bool AiIsCombatSimInvalid(GbaPtr<const AiCombatSimulationSt> /*st*/) {
  return false;
}

// ---------------------------------------------------------------------------
// 0x08038FA4: AiSimulateBattleAndComputeScore
// ---------------------------------------------------------------------------
bool AiSimulateBattleAndComputeScore(GbaPtr<AiCombatSimulationSt> st) {
  AiCombatSimulationSt tmp = *st;
  const bool ok = SimulateBattleAndComputeScoreImpl(&tmp);
  *st = tmp;
  return ok;
}

// ---------------------------------------------------------------------------
// 0x08039534: AiFindBestRecoveryTile
// ---------------------------------------------------------------------------
bool AiFindBestRecoveryTile(GbaPtr<AiPos> out_pos) {
  int best_enemies = 10000;
  uint8_t best_cost = 0xFFu;

  GbaPtr<Unit> active = ActiveUnit();
  if ((active->ai_config & 0x2000u) != 0u) {
    return false;
  }
  if ((UnitAttributes(*active) & 0x2000u) != 0u) {
    return false;
  }

  MapFloodUnitMovement(ConstUnit(active), 0x7C);

  const MapSize sz = GetMapSize();
  for (int iy = sz.y - 1; iy >= 0; --iy) {
    for (int ix = sz.x - 1; ix >= 0; --ix) {
      const uint8_t mov = MapMovement()[iy][ix];
      if (mov > 0x78u) {
        continue;
      }

      const uint8_t tid = MapTerrain()[iy][ix];
      const bool is_heal_terrain =
          AiIsInByteList(GbaPtr<const uint8_t>(kAiRecoveryTerrainsAddr), tid);
      const uint8_t uid = MapUnit()[iy][ix];

      if (!is_heal_terrain) {
        if (uid == 0u || !AreUnitIdsAllied(ActiveUnitId(), uid)) {
          continue;
        }
        GbaPtr<Unit> occ = GetUnit(uid);
        if ((occ->ai_flags & 4u) == 0u) {
          continue;
        }
      } else {
        if (uid != 0u) {
          if (!AreUnitIdsAllied(ActiveUnitId(), uid)) {
            continue;
          }
          GbaPtr<Unit> occ = GetUnit(uid);
          if ((occ->ai_config & 0x2000u) != 0u && (occ->ai_flags & 4u) == 0u) {
            continue;
          }
        }
      }

      const int enemies = AiCountEnemiesNearby(static_cast<int16_t>(ix),
                                               static_cast<int16_t>(iy));
      if (enemies > best_enemies) {
        continue;
      }
      if (static_cast<int>(static_cast<int8_t>(mov)) >
          static_cast<int>(best_cost)) {
        continue;
      }
      best_enemies = enemies;
      best_cost = mov;
      out_pos->x = static_cast<int16_t>(ix);
      out_pos->y = static_cast<int16_t>(iy);
    }
  }

  if (best_cost == 0xFFu) {
    return false;
  }

  const uint8_t final_uid = MapUnit()[out_pos->y][out_pos->x];
  if (final_uid != 0u && final_uid != ActiveUnitId()) {
    GbaPtr<Unit> occ = GetUnit(final_uid);
    occ->ai_flags = static_cast<uint8_t>(occ->ai_flags | 2u);
  }
  return true;
}

// ---------------------------------------------------------------------------
// 0x080397DC: AiTryUseHealingItem
// ---------------------------------------------------------------------------
bool AiTryUseHealingItem() {
  for (int slot = 0; slot < 5; ++slot) {
    const uint16_t item = ActiveUnit()->items[slot];
    if (item == 0u) {
      break;
    }
    const int iid = GetItemIid(item);
    if (iid != 0x6B && iid != 0x6C) {
      continue;
    }
    GbaPtr<Unit> active = ActiveUnit();
    if ((*GbaPtr<const uint8_t>(kAiStateAddr + 0x7Bu) & 2u) == 0u &&
        (active->ai_config & 0x2000u) == 0u) {
      // Inline AiFindSafestReachableLocation(active, &pos) check using a
      // temporary EWRAM? No, compute via MapFloodUnit + scan matching
      // 0x08036900! Wait: AiFindSafestReachableLocation is simple — let's check
      // if we have a scratch slot or inline the exact 10-line scan from
      // ai_script.cc.
      MapFloodUnit(ConstUnit(active));
      GbaCall(kAddrMapClearOther, 0xFFFFFFFFu);
      uint8_t min_danger = 0xFFu;
      int16_t safe_x = 0;
      int16_t safe_y = 0;
      const MapSize sz = GetMapSize();
      for (int iy = sz.y - 1; iy >= 0; --iy) {
        for (int ix = sz.x - 1; ix >= 0; --ix) {
          if (MapMovement()[iy][ix] > 0x78u) {
            continue;
          }
          const uint8_t occ = MapUnit()[iy][ix];
          if (occ != 0u && occ != ActiveUnitId()) {
            continue;
          }
          const uint8_t danger = MapOther()[iy][ix];
          if (danger < min_danger) {
            min_danger = danger;
            safe_x = static_cast<int16_t>(ix);
            safe_y = static_cast<int16_t>(iy);
          }
        }
      }
      if (min_danger == 0xFFu) {
        continue;
      }
      AiSetDecision(safe_x, safe_y, 6, 0, static_cast<uint8_t>(slot), 0, 0);
      return true;
    }
    AiSetDecision(active->x, active->y, 6, 0, static_cast<uint8_t>(slot), 0, 0);
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// 0x08039894: AiPerformEscape
// ---------------------------------------------------------------------------
bool AiPerformEscape() {
  GbaPtr<Unit> active = ActiveUnit();
  MapFloodUnitMovement(ConstUnit(active), 0x7C);
  GbaPtr<const uint8_t> pt = AiGetClosestEscapePoint();
  if (!pt) {
    return false;
  }

  const uint8_t ex = pt[0];
  const uint8_t ey = pt[1];
  const int8_t mov_cost = static_cast<int8_t>(MapMovement()[ey][ex]);
  const int unit_mov =
      static_cast<int>(active->bonus_mov) +
      static_cast<int>(static_cast<int8_t>(active->jinfo->base_mov));
  if (mov_cost <= unit_mov) {
    AiMoveTowards(ex, ey, 0, 0xFFu, 1);
    AiSetDecision(Decision()->x_move, Decision()->y_move, 2, ex, ey, pt[2], 0);
    return true;
  }

  AiMoveTowards(ex, ey, 0, 0xFFu, 0);
  return Decision()->action_performed != 0;
}

// ---------------------------------------------------------------------------
// 0x08039D74: AiTryPostMoveSpecialAction
// ---------------------------------------------------------------------------
void AiTryPostMoveSpecialAction() {
  if (Decision()->action_id == 2u) {
    return;
  }
  if (AiTryDanceOrPlay(Decision()->x_move, Decision()->y_move)) {
    return;
  }
  AiTryStealFromPos(Decision()->x_move, Decision()->y_move);
}

// ---------------------------------------------------------------------------
// 0x08039DA0: AiTryPostMoveActionOrAttack
// ---------------------------------------------------------------------------
void AiTryPostMoveActionOrAttack() {
  if (AiTryDanceOrPlay(Decision()->x_move, Decision()->y_move)) {
    return;
  }
  if (AiTryStealFromPos(Decision()->x_move, Decision()->y_move)) {
    return;
  }
  AiTryAttackFromPos(Decision()->x_move, Decision()->y_move);
}

// ---------------------------------------------------------------------------
// 0x08039EFC: AiTryStealFromPos
// ---------------------------------------------------------------------------
bool AiTryStealFromPos(int x, int y) {
  if ((UnitAttributes(*ActiveUnit()) & kUnitAttrSteal) == 0u) {
    return false;
  }
  MapFill(MapMovement(), 0xFFu);
  MapMovement()[y][x] = 0u;
  MapAddInRange(x, y, 1, 0x78);
  const int8_t res = AiTryStealAction() ? 1 : 0;
  return res != -1;
}

// ---------------------------------------------------------------------------
// Native dispatch thunks
// ---------------------------------------------------------------------------
namespace {

uint32_t ThunkAiPerformAttack(uint32_t r0, uint32_t, uint32_t, uint32_t,
                              const uint32_t* absl_nullable) {
  AiPerformAttack(r0);
  return 0u;
}
uint32_t ThunkAiPerformAttackInCurrentMovement(uint32_t r0, uint32_t, uint32_t,
                                               uint32_t,
                                               const uint32_t* absl_nullable) {
  AiPerformAttackInCurrentMovement(r0);
  return 0u;
}
uint32_t ThunkAiFillTargetWeaponRangeMap(uint32_t r0, uint32_t r1, uint32_t,
                                         uint32_t,
                                         const uint32_t* absl_nullable) {
  AiFillTargetWeaponRangeMap(GbaPtr<const Unit>(r0), static_cast<uint16_t>(r1));
  return 0u;
}
uint32_t ThunkAiMakeRangeMapForUnitAndMov(uint32_t r0, uint32_t r1, uint32_t r2,
                                          uint32_t,
                                          const uint32_t* absl_nullable) {
  AiMakeRangeMapForUnitAndMov(GbaPtr<const Unit>(r0), static_cast<uint16_t>(r1),
                              static_cast<uint16_t>(r2));
  return 0u;
}
uint32_t ThunkAiSimulateBallistaAttack(uint32_t r0, uint32_t r1, uint32_t,
                                       uint32_t,
                                       const uint32_t* absl_nullable) {
  return AiSimulateBallistaAttack(r0, GbaPtr<AiCombatSimulationSt>(r1)) ? 1u
                                                                        : 0u;
}
uint32_t ThunkAiGetStealPositionScore(uint32_t r0, uint32_t r1, uint32_t,
                                      uint32_t, const uint32_t* absl_nullable) {
  return AiGetStealPositionScore(static_cast<int>(r0), static_cast<int>(r1));
}
uint32_t ThunkAiTryStealAction(uint32_t, uint32_t, uint32_t, uint32_t,
                               const uint32_t* absl_nullable) {
  return AiTryStealAction() ? 1u : 0u;
}
uint32_t ThunkAiSimulateBestBattleAgainstTarget(uint32_t r0, uint32_t, uint32_t,
                                                uint32_t,
                                                const uint32_t* absl_nullable) {
  return AiSimulateBestBattleAgainstTarget(GbaPtr<AiCombatSimulationSt>(r0))
             ? 1u
             : 0u;
}
uint32_t ThunkAiSimulateBestBallistaBattleAgainstTarget(
    uint32_t r0, uint32_t r1, uint32_t, uint32_t,
    const uint32_t* absl_nullable) {
  return AiSimulateBestBallistaBattleAgainstTarget(
             GbaPtr<AiCombatSimulationSt>(r0), static_cast<uint16_t>(r1))
             ? 1u
             : 0u;
}
uint32_t ThunkAiGetCombatPositionScore(uint32_t r0, uint32_t r1, uint32_t r2,
                                       uint32_t,
                                       const uint32_t* absl_nullable) {
  return AiGetCombatPositionScore(static_cast<int>(r0), static_cast<int>(r1),
                                  GbaPtr<const AiCombatSimulationSt>(r2));
}
uint32_t ThunkAiIsCombatSimInvalid(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                   const uint32_t* absl_nullable) {
  return AiIsCombatSimInvalid(GbaPtr<const AiCombatSimulationSt>(r0)) ? 1u : 0u;
}
uint32_t ThunkAiSimulateBattleAndComputeScore(uint32_t r0, uint32_t, uint32_t,
                                              uint32_t,
                                              const uint32_t* absl_nullable) {
  return AiSimulateBattleAndComputeScore(GbaPtr<AiCombatSimulationSt>(r0)) ? 1u
                                                                           : 0u;
}
uint32_t ThunkAiFindBestRecoveryTile(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                     const uint32_t* absl_nullable) {
  return AiFindBestRecoveryTile(GbaPtr<AiPos>(r0)) ? 1u : 0u;
}
uint32_t ThunkAiTryUseHealingItem(uint32_t, uint32_t, uint32_t, uint32_t,
                                  const uint32_t* absl_nullable) {
  return AiTryUseHealingItem() ? 1u : 0u;
}
uint32_t ThunkAiPerformEscape(uint32_t, uint32_t, uint32_t, uint32_t,
                              const uint32_t* absl_nullable) {
  return AiPerformEscape() ? 1u : 0u;
}
uint32_t ThunkAiTryPostMoveSpecialAction(uint32_t, uint32_t, uint32_t, uint32_t,
                                         const uint32_t* absl_nullable) {
  AiTryPostMoveSpecialAction();
  return 0u;
}
uint32_t ThunkAiTryPostMoveActionOrAttack(uint32_t, uint32_t, uint32_t,
                                          uint32_t,
                                          const uint32_t* absl_nullable) {
  AiTryPostMoveActionOrAttack();
  return 0u;
}
uint32_t ThunkAiTryStealFromPos(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                                const uint32_t* absl_nullable) {
  return AiTryStealFromPos(static_cast<int>(r0), static_cast<int>(r1)) ? 1u
                                                                       : 0u;
}

}  // namespace

void RegisterAiCombatFunctions() {
  RegisterNative(kAddrAiPerformAttack, &ThunkAiPerformAttack);
  RegisterNative(kAddrAiPerformAttackInCurrentMovement,
                 &ThunkAiPerformAttackInCurrentMovement);
  RegisterNative(kAddrAiFillTargetWeaponRangeMap,
                 &ThunkAiFillTargetWeaponRangeMap);
  RegisterNative(kAddrAiMakeRangeMapForUnitAndMov,
                 &ThunkAiMakeRangeMapForUnitAndMov);
  RegisterNative(kAddrAiSimulateBallistaAttack, &ThunkAiSimulateBallistaAttack);
  RegisterNative(kAddrAiGetStealPositionScore, &ThunkAiGetStealPositionScore);
  RegisterNative(kAddrAiTryStealAction, &ThunkAiTryStealAction);
  RegisterNative(kAddrAiSimulateBestBattleAgainstTarget,
                 &ThunkAiSimulateBestBattleAgainstTarget);
  RegisterNative(kAddrAiSimulateBestBallistaBattleAgainstTarget,
                 &ThunkAiSimulateBestBallistaBattleAgainstTarget);
  RegisterNative(kAddrAiGetCombatPositionScore, &ThunkAiGetCombatPositionScore);
  RegisterNative(kAddrAiIsCombatSimInvalid, &ThunkAiIsCombatSimInvalid);
  RegisterNative(kAddrAiSimulateBattleAndComputeScore,
                 &ThunkAiSimulateBattleAndComputeScore);
  RegisterNative(kAddrAiFindBestRecoveryTile, &ThunkAiFindBestRecoveryTile);
  RegisterNative(kAddrAiTryUseHealingItem, &ThunkAiTryUseHealingItem);
  RegisterNative(kAddrAiPerformEscape, &ThunkAiPerformEscape);
  RegisterNative(kAddrAiTryPostMoveSpecialAction,
                 &ThunkAiTryPostMoveSpecialAction);
  RegisterNative(kAddrAiTryPostMoveActionOrAttack,
                 &ThunkAiTryPostMoveActionOrAttack);
  RegisterNative(kAddrAiTryStealFromPos, &ThunkAiTryStealFromPos);
}

}  // namespace fe7
