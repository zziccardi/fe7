#include "game/ai/ai_actions.h"

#include <cstdint>

#include "absl/base/nullability.h"
#include "game/ai/ai.h"
#include "game/ai/ai_decide.h"
#include "game/ai/ai_opcodes.h"
#include "game/ai/ai_script.h"
#include "game/engine/rng.h"
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

inline GbaPtr<GbaPtr<uint8_t>> MapOther() {
  return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kAiMapOtherAddr);
}

inline int ComputeScaledMovement(GbaPtr<const Unit> unit, uint8_t scale) {
  const int mov = static_cast<int>(unit->bonus_mov) +
                  static_cast<int>(static_cast<int8_t>(unit->jinfo->base_mov));
  return static_cast<int>(static_cast<uint16_t>(mov * scale) >> 4);
}

// GBA BIOS Div(num, denom) (truncating signed 32-bit integer division).
inline int BiosDiv(int num, int denom) {
  if (denom == 0) return 0;
  return num / denom;
}

// Internal native helper for AiGetNextPatrolWaypoint (avoids GBA stack).
bool GetNextPatrolWaypointImpl(AiPos* out_pos) {
  GbaPtr<Unit> unit = ActiveUnit();
  const uint32_t route_idx = (unit->ai_config & 0x1FC0u) >> 8;
  uint8_t step = unit->unk_46;

  const uint32_t table_ptr = *GbaPtr<const uint32_t>(kAiPatrolWaypointsPtrAddr);
  if (table_ptr == 0u) {
    return false;
  }
  const uint32_t route_ptr =
      *GbaPtr<const uint32_t>(table_ptr + route_idx * 4u);
  if (route_ptr == 0u) {
    return false;
  }

  uint32_t entry_ptr = route_ptr + static_cast<uint32_t>(step) * 4u;
  if (*GbaPtr<const int16_t>(entry_ptr) == -1) {
    step = 0;
    unit->unk_46 = 0;
    entry_ptr = route_ptr;
  }

  const int16_t wx = *GbaPtr<const int16_t>(entry_ptr);
  const int16_t wy = *GbaPtr<const int16_t>(entry_ptr + 2u);
  out_pos->x = wx;
  out_pos->y = wy;

  if (MapMovement()[wy][wx] != 0xFFu) {
    unit->unk_46 = static_cast<uint8_t>(step + 1u);
  }
  return true;
}

// Internal native helper for AiFindBestAdjacentStaffPos (avoids GBA stack).
bool FindBestAdjacentStaffPosImpl(int x, int y, AiPos* out_pos) {
  uint32_t best_score = 0;
  for (int i = 0; i < 4; ++i) {
    const int dx = *GbaPtr<const int32_t>(kAiAdjacentOffsetsAddr +
                                          static_cast<uint32_t>(i) * 8u);
    const int dy = *GbaPtr<const int32_t>(kAiAdjacentOffsetsAddr +
                                          static_cast<uint32_t>(i) * 8u + 4u);
    const int nx = x + dx;
    const int ny = y + dy;

    if (MapMovement()[ny][nx] > 0x77u) {
      continue;
    }
    const uint8_t uid = MapUnit()[ny][nx];
    if (uid != 0u && uid != ActiveUnitId()) {
      continue;
    }

    int score = AiGetTerrainCombatPositionScoreComponent(nx, ny);
    score += AiGetFriendZonePositionScoreComponent(nx, ny);
    score -= static_cast<int>(MapOther()[ny][nx] >> 3);
    const uint32_t uscore = static_cast<uint32_t>(score) + 0x7FFFFFFFu;
    if (best_score < uscore) {
      out_pos->x = static_cast<int16_t>(nx);
      out_pos->y = static_cast<int16_t>(ny);
      best_score = uscore;
    }
  }
  return best_score != 0u;
}

}  // namespace

// ---------------------------------------------------------------------------
// 0x08039F60: AiTryAttackFromPos
// ---------------------------------------------------------------------------
bool AiTryAttackFromPos(int x, int y) {
  const uint16_t weapon =
      static_cast<uint16_t>(GetUnitEquippedWeapon(ConstUnit(ActiveUnit())));
  if (weapon == 0u) {
    return false;
  }

  MapFill(MapMovement(), 0);

  const int16_t x16 = static_cast<int16_t>(x);
  const int16_t y16 = static_cast<int16_t>(y);
  const int16_t min_range = static_cast<int16_t>(GetItemMinRange(weapon));
  const int16_t max_range = static_cast<int16_t>(GetItemMaxRange(weapon));
  GbaCall(kAddrMapAddInBoundedRange, static_cast<uint32_t>(x16),
          static_cast<uint32_t>(y16), static_cast<uint32_t>(min_range),
          static_cast<uint32_t>(max_range));

  const MapSize sz = GetMapSize();
  for (int iy = sz.y - 1; iy >= 0; --iy) {
    for (int ix = sz.x - 1; ix >= 0; --ix) {
      if (static_cast<int8_t>(MapMovement()[iy][ix]) == 0) {
        continue;
      }
      const uint8_t uid = MapUnit()[iy][ix];
      if (uid == 0u) {
        continue;
      }
      if (AreUnitIdsAllied(ActiveUnitId(), uid)) {
        continue;
      }
      GbaPtr<Unit> target = GetUnit(uid);
      if (AiGetInRangeCombatPositionScoreComponent(x, y, ConstUnit(target)) !=
          0) {
        const uint8_t slot = static_cast<uint8_t>(
            GetUnitEquippedWeaponSlot(ConstUnit(ActiveUnit())));
        AiSetDecision(x16, y16, 1, static_cast<uint8_t>(target->id), slot, 0,
                      0);
        return true;
      }
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// 0x0803A090: AiIsTileOccupiedByEnemy
// ---------------------------------------------------------------------------
bool AiIsTileOccupiedByEnemy(int x, int y) {
  const uint8_t uid = MapUnit()[y][x];
  if (uid == 0u) {
    return false;
  }
  return (((ActiveUnitId() ^ uid) & 0x80u) != 0u);
}

// ---------------------------------------------------------------------------
// 0x0803A0C0: AiScriptActionCountEnemiesInRange
// ---------------------------------------------------------------------------
bool AiScriptActionCountEnemiesInRange(GbaPtr<const uint8_t> params) {
  uint8_t count = 0;
  GbaPtr<Unit> unit = ActiveUnit();
  const int scaled_mov = ComputeScaledMovement(ConstUnit(unit), params[0]);
  const uint16_t weapon =
      static_cast<uint16_t>(GetUnitEquippedWeapon(ConstUnit(unit)));

  const MapSize sz = GetMapSize();
  if (params[1] != 0u && weapon != 0u) {
    GbaCall(kAddrAiMakeRangeMapForUnitAndMov, unit.addr,
            static_cast<uint32_t>(scaled_mov), static_cast<uint32_t>(weapon));
    for (int iy = sz.y - 1; iy >= 0; --iy) {
      for (int ix = sz.x - 1; ix >= 0; --ix) {
        if (static_cast<int8_t>(MapRange()[iy][ix]) != 0 &&
            AiIsTileOccupiedByEnemy(ix, iy)) {
          count = static_cast<uint8_t>(count + 1u);
        }
      }
    }
  } else {
    SetWorkingMoveTable(GetUnitMovementCost(ConstUnit(unit)));
    GbaCall(kAddrSetWorkingBmMap, MapRange().addr);
    BeginMapFlood(unit->x, unit->y, scaled_mov, 0);
    for (int iy = sz.y - 1; iy >= 0; --iy) {
      for (int ix = sz.x - 1; ix >= 0; --ix) {
        if (MapRange()[iy][ix] <= 0x78u && AiIsTileOccupiedByEnemy(ix, iy)) {
          count = static_cast<uint8_t>(count + 1u);
        }
      }
    }
  }

  *GbaPtr<uint8_t>(kAiStateAddr + 0x86u + params[2]) = count;
  return false;
}

// ---------------------------------------------------------------------------
// 0x0803A204: AiScriptActionMoveOrAttackInRegion
// ---------------------------------------------------------------------------
bool AiScriptActionMoveOrAttackInRegion(GbaPtr<const uint8_t> params) {
  GbaPtr<Unit> unit = ActiveUnit();
  const int scaled_mov = ComputeScaledMovement(ConstUnit(unit), params[4]);
  const uint16_t weapon =
      static_cast<uint16_t>(GetUnitEquippedWeapon(ConstUnit(unit)));

  const uint32_t coord_idx = (unit->ai_config & 0x1FC0u) >> 8;
  const uint32_t coord_table = *GbaPtr<const uint32_t>(params.addr);
  const uint8_t tx = *GbaPtr<const uint8_t>(coord_table + coord_idx * 2u);
  const uint8_t ty = *GbaPtr<const uint8_t>(coord_table + coord_idx * 2u + 1u);

  const int8_t orig_x = unit->x;
  const int8_t orig_y = unit->y;
  unit->x = static_cast<int8_t>(tx);
  unit->y = static_cast<int8_t>(ty);

  bool in_region = false;
  if (params[5] != 0u && weapon != 0u) {
    GbaCall(kAddrAiMakeRangeMapForUnitAndMov, unit.addr,
            static_cast<uint32_t>(scaled_mov), static_cast<uint32_t>(weapon));
    in_region = (static_cast<int8_t>(MapRange()[ty][tx]) != 0);
  } else {
    SetWorkingMoveTable(GetUnitMovementCost(ConstUnit(unit)));
    GbaCall(kAddrSetWorkingBmMap, MapRange().addr);
    BeginMapFlood(unit->x, unit->y, scaled_mov, 0);
    in_region = (MapRange()[ty][tx] <= 0x78u);
  }

  unit->x = orig_x;
  unit->y = orig_y;

  if (!in_region) {
    AiMoveTowards(static_cast<int16_t>(tx), static_cast<int16_t>(ty), 0, 0xFFu,
                  1);
    return true;
  }

  MapFloodUnit(ConstUnit(unit));
  if (UnitKnowsMagic(ConstUnit(unit))) {
    GbaCall(kAddrMapClearOther, 0xFFFFFFFFu);
  }

  const MapSize sz = GetMapSize();
  for (int iy = sz.y - 1; iy >= 0; --iy) {
    for (int ix = sz.x - 1; ix >= 0; --ix) {
      const int8_t mov_val = static_cast<int8_t>(MapMovement()[iy][ix]);
      if (weapon != 0u) {
        if (mov_val > 0x77 || static_cast<int8_t>(MapRange()[iy][ix]) == 0) {
          MapMovement()[iy][ix] = 0xFFu;
        }
      } else {
        if (mov_val > 0x77 || static_cast<int8_t>(MapRange()[iy][ix]) > 0x77) {
          MapMovement()[iy][ix] = 0xFFu;
        }
      }
    }
  }

  GbaCall(kAddrAiPerformAttackInCurrentMovement, kAddrAiIsUnitEnemy | 1u);
  if (Decision()->action_performed != 1) {
    AiMoveTowards(static_cast<int16_t>(tx), static_cast<int16_t>(ty), 0, 0xFFu,
                  1);
  }
  return true;
}

// ---------------------------------------------------------------------------
// 0x0803A3D4: AiScriptActionLoadUnitCounter
// ---------------------------------------------------------------------------
bool AiScriptActionLoadUnitCounter(GbaPtr<const uint8_t> /*params*/) {
  *GbaPtr<uint8_t>(kAiStateAddr + 0x86u) = ActiveUnit()->unk_46;
  return false;
}

// ---------------------------------------------------------------------------
// 0x0803A3F0 / 0x0803A420: AiScriptActionClearAiStateFlagA/B
// ---------------------------------------------------------------------------
bool AiScriptActionClearAiStateFlagA(GbaPtr<const uint8_t> /*params*/) {
  const int base = static_cast<int>(GbaCall(kAddrGetFactionBaseUid));
  for (int uid = base + 1; uid < base + 0x80; ++uid) {
    (void)GetUnit(uid);
  }
  *GbaPtr<uint8_t>(kAiStateAddr + 0x79u) = 0u;
  return false;
}

bool AiScriptActionClearAiStateFlagB(GbaPtr<const uint8_t> /*params*/) {
  const int base = static_cast<int>(GbaCall(kAddrGetFactionBaseUid));
  for (int uid = base + 1; uid < base + 0x80; ++uid) {
    (void)GetUnit(uid);
  }
  *GbaPtr<uint8_t>(kAiStateAddr + 0x79u) = 0u;
  return false;
}

// ---------------------------------------------------------------------------
// 0x0803A450: AiGetNextPatrolWaypoint
// ---------------------------------------------------------------------------
bool AiGetNextPatrolWaypoint(GbaPtr<AiPos> out_pos) {
  AiPos pos{};
  if (!GetNextPatrolWaypointImpl(&pos)) {
    return false;
  }
  out_pos->x = pos.x;
  out_pos->y = pos.y;
  return true;
}

// ---------------------------------------------------------------------------
// 0x0803A4D8: AiScriptActionPatrolOrAttack
// ---------------------------------------------------------------------------
bool AiScriptActionPatrolOrAttack(GbaPtr<const uint8_t> /*params*/) {
  GbaPtr<Unit> unit = ActiveUnit();
  const uint16_t weapon =
      static_cast<uint16_t>(GetUnitEquippedWeapon(ConstUnit(unit)));
  if (weapon != 0u) {
    AiMakeMoveRangeMapsForUnitAndWeapon(ConstUnit(unit), weapon);
    const int count = AiCountEnemiesInRange();
    if (count != 0) {
      *GbaPtr<uint8_t>(kAiStateAddr + 0x86u) = static_cast<uint8_t>(count);
      return false;
    }
  } else {
    MapFloodUnit(ConstUnit(unit));
  }

  AiPos pos{};
  if (!GetNextPatrolWaypointImpl(&pos)) {
    return false;
  }
  AiMoveTowards(pos.x, pos.y, 0, 0xFFu, 1);
  return true;
}

// ---------------------------------------------------------------------------
// 0x0803A548: AiScriptActionIsActiveUnitInRect
// ---------------------------------------------------------------------------
bool AiScriptActionIsActiveUnitInRect(GbaPtr<const uint8_t> params) {
  GbaPtr<Unit> unit = ActiveUnit();
  const uint8_t ux = static_cast<uint8_t>(unit->x);
  const uint8_t uy = static_cast<uint8_t>(unit->y);
  const uint8_t x1 = params[0];
  const uint8_t y1 = params[1];
  const uint8_t x2 = params[2];
  const uint8_t y2 = params[3];

  const uint8_t inside =
      (x1 <= ux && ux <= x2 && y1 <= uy && uy <= y2) ? 1u : 0u;
  *GbaPtr<uint8_t>(kAiStateAddr + 0x86u) = inside;
  return false;
}

// ---------------------------------------------------------------------------
// 0x0803A58C: AiScriptActionTalk
// ---------------------------------------------------------------------------
bool AiScriptActionTalk(GbaPtr<const uint8_t> params) {
  const uint8_t uid0 = static_cast<uint8_t>(GetUnitByPid(params[0])->id);
  const uint8_t uid1 = static_cast<uint8_t>(GetUnitByPid(params[1])->id);
  AiUpdateDecision(8, 0, uid0, uid1, 0xFFu);
  return true;
}

// ---------------------------------------------------------------------------
// 0x0803A5BC: AiScriptActionProbeLeaderTarget
// ---------------------------------------------------------------------------
bool AiScriptActionProbeLeaderTarget(GbaPtr<const uint8_t> /*params*/) {
  const uint8_t saved_uid = ActiveUnitId();
  GbaPtr<Unit> saved_unit = ActiveUnit();

  const uint16_t leader_pid =
      static_cast<uint16_t>(GbaCall(kAddrGetUnitLeaderPid, saved_unit.addr));
  if (leader_pid == 0u) {
    return false;
  }

  GbaPtr<Unit> leader = GetUnitByPid(leader_pid);
  *GbaPtr<GbaPtr<Unit>>(kAiActiveUnitAddr) = leader;
  if (leader.addr == 0u) {
    *GbaPtr<GbaPtr<Unit>>(kAiActiveUnitAddr) = saved_unit;
    *GbaPtr<uint8_t>(kAiStateAddr + 0x87u) = 1u;
    return false;
  }

  *GbaPtr<uint8_t>(kAiActiveUnitIdAddr) = static_cast<uint8_t>(leader->id);
  const uint8_t saved_ai_a = leader->ai_a;
  const uint8_t saved_ai_a_pc = leader->ai_a_pc;

  int iters = 0;
  while (true) {
    if (AiTryExecScriptA()) {
      break;
    }
    ++iters;
    if (iters > 0xFF) {
      AiExecFallbackScriptA();
      break;
    }
  }

  if (Decision()->action_performed == 1 && Decision()->action_id == 1u) {
    *GbaPtr<uint8_t>(kAiStateAddr + 0x86u) = Decision()->unit_target_id;
  } else {
    *GbaPtr<uint8_t>(kAiStateAddr + 0x86u) = 0u;
  }

  AiClearDecision();
  GbaPtr<Unit> cur = ActiveUnit();
  cur->ai_a = saved_ai_a;
  cur->ai_a_pc = saved_ai_a_pc;
  *GbaPtr<uint8_t>(kAiActiveUnitIdAddr) = saved_uid;
  *GbaPtr<GbaPtr<Unit>>(kAiActiveUnitAddr) = saved_unit;
  return false;
}

// ---------------------------------------------------------------------------
// 0x0803A680: AiIsUnitMatchingTargetPid
// ---------------------------------------------------------------------------
bool AiIsUnitMatchingTargetPid(GbaPtr<const Unit> unit) {
  if (unit->pinfo->id != *GbaPtr<const uint8_t>(kAiTargetPidScratchAddr)) {
    return false;
  }
  return !AreUnitIdsAllied(ActiveUnit()->id, unit->id);
}

// ---------------------------------------------------------------------------
// 0x0803A6BC: AiScriptActionProbePidTarget
// ---------------------------------------------------------------------------
bool AiScriptActionProbePidTarget(GbaPtr<const uint8_t> params) {
  const uint8_t pid = params[0];
  *GbaPtr<uint8_t>(kAiTargetPidScratchAddr) = pid;
  if (!AiIsUnitAliveByPid(pid)) {
    *GbaPtr<uint8_t>(kAiStateAddr + 0x87u) = 1u;
    return false;
  }

  GbaCall(kAddrAiPerformAttack, kAddrAiIsUnitMatchingTargetPid | 1u);
  *GbaPtr<uint8_t>(kAiStateAddr + 0x86u) = 0u;
  if (Decision()->action_performed == 1 && Decision()->action_id == 1u) {
    *GbaPtr<uint8_t>(kAiStateAddr + 0x86u) = Decision()->unit_target_id;
  }
  AiClearDecision();
  return false;
}

// ---------------------------------------------------------------------------
// 0x0803A71C: AiIsUnitMatchingCachedUid
// ---------------------------------------------------------------------------
bool AiIsUnitMatchingCachedUid(GbaPtr<const Unit> unit) {
  if (static_cast<int>(unit->id) !=
      static_cast<int>(*GbaPtr<const uint8_t>(kAiStateAddr + 0x86u))) {
    return false;
  }
  return !AreUnitIdsAllied(ActiveUnit()->id, unit->id);
}

// ---------------------------------------------------------------------------
// 0x0803A754: AiIsUnitInDirectionOfCachedTarget
// ---------------------------------------------------------------------------
bool AiIsUnitInDirectionOfCachedTarget(GbaPtr<const Unit> unit) {
  GbaPtr<Unit> cached = GetUnit(*GbaPtr<const uint8_t>(kAiStateAddr + 0x86u));
  GbaPtr<Unit> active = ActiveUnit();

  const int dx_cached = cached->x - active->x;
  const int dy_cached = cached->y - active->y;
  const int dx_unit = unit->x - active->x;
  const int dy_unit = unit->y - active->y;

  if (AreUnitIdsAllied(active->id, unit->id)) {
    return false;
  }
  if (dx_cached * dx_unit < 0) {
    return false;
  }
  if (dy_cached * dy_unit < 0) {
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// 0x0803A7C8: AiScriptActionAttackOrChaseCachedTarget
// ---------------------------------------------------------------------------
bool AiScriptActionAttackOrChaseCachedTarget(GbaPtr<const uint8_t> /*params*/) {
  const uint8_t target_uid = *GbaPtr<const uint8_t>(kAiStateAddr + 0x86u);
  if (target_uid != 0u) {
    GbaPtr<Unit> target = GetUnit(target_uid);
    GbaCall(kAddrAiPerformAttack, kAddrAiIsUnitMatchingCachedUid | 1u);
    if (Decision()->action_performed != 1) {
      GbaCall(kAddrAiPerformAttack,
              kAddrAiIsUnitInDirectionOfCachedTarget | 1u);
      if (Decision()->action_performed != 1) {
        AiMoveTowards(target->x, target->y, 0, 0xFFu, 1);
      }
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// 0x0803A828: AiScriptActionStaffOrAttack
// ---------------------------------------------------------------------------
bool AiScriptActionStaffOrAttack(GbaPtr<const uint8_t> params) {
  const uint8_t roll = static_cast<uint8_t>(RandNext(100));
  *GbaPtr<uint8_t>(kAiStateAddr + 0x7Cu) = params[1];
  if (roll <= params[0]) {
    if (!AiTrySpecialAction(kAddrAiIsUnitEnemy | 1u)) {
      GbaCall(kAddrAiPerformAttack, kAddrAiIsUnitEnemy | 1u);
    }
  } else {
    *GbaPtr<uint8_t>(kAiStateAddr + 0x79u) = 4u;
  }
  return true;
}

// ---------------------------------------------------------------------------
// 0x0803A874: AiScriptActionLootOrAttack
// ---------------------------------------------------------------------------
bool AiScriptActionLootOrAttack(GbaPtr<const uint8_t> params) {
  const uint8_t roll = static_cast<uint8_t>(RandNext(100));
  if (roll <= params[0]) {
    const uint8_t looted =
        static_cast<uint8_t>(GbaCall(kAddrAiTryDoorOrChestAction));
    if (looted == 0u) {
      const uint8_t roll2 = static_cast<uint8_t>(RandNext(100));
      if (roll2 <= params[1]) {
        GbaCall(kAddrAiPerformAttack, kAddrAiIsUnitEnemy | 1u);
      }
    }
  } else {
    *GbaPtr<uint8_t>(kAiStateAddr + 0x79u) = 4u;
  }
  return true;
}

// ---------------------------------------------------------------------------
// 0x0803A8C4: AiScriptActionBallista
// ---------------------------------------------------------------------------
bool AiScriptActionBallista(GbaPtr<const uint8_t> /*params*/) {
  int16_t best_x = -1;
  int16_t best_y = -1;
  uint16_t ballista_count = 0;
  uint8_t best_cost = 0xFFu;

  GbaPtr<Unit> unit = ActiveUnit();
  if ((unit->flags & kUnitFlagInBallista) != 0u) {
    if (GbaCall(kAddrGetBallistaItemAt, static_cast<uint32_t>(unit->x),
                static_cast<uint32_t>(unit->y)) == 0u) {
      AiSetDecision(unit->x, unit->y, 10, 0, 0, 0, 0);
    }
    return true;
  }

  GbaCall(kAddrAiMapFloodUnitExtended, unit.addr);
  const MapSize sz = GetMapSize();
  for (int iy = sz.y - 1; iy >= 0; --iy) {
    for (int ix = sz.x - 1; ix >= 0; --ix) {
      const uint8_t mov = MapMovement()[iy][ix];
      if (mov > 0x78u) {
        continue;
      }
      if (GbaCall(kAddrGetBallistaItemAt, static_cast<uint32_t>(ix),
                  static_cast<uint32_t>(iy)) == 0u) {
        continue;
      }
      ballista_count = static_cast<uint16_t>(ballista_count + 1u);
      if (MapUnit()[iy][ix] != 0u) {
        continue;
      }
      if (mov <= best_cost) {
        best_cost = mov;
        best_x = static_cast<int16_t>(ix);
        best_y = static_cast<int16_t>(iy);
      }
    }
  }

  if (best_x >= 0) {
    AiMoveTowards(best_x, best_y, 0, 0xFFu, 1);
  }

  if (Decision()->action_performed == 1) {
    if (static_cast<int>(Decision()->x_move) == static_cast<int>(best_x) &&
        static_cast<int>(Decision()->y_move) == static_cast<int>(best_y)) {
      AiUpdateDecision(9, 0, 0, 0, 0);
    }
  } else {
    *GbaPtr<uint8_t>(kAiStateAddr + 0x86u) = (ballista_count != 0u) ? 7u : 6u;
  }
  return true;
}

// ---------------------------------------------------------------------------
// 0x0803AA40: AiScriptActionMoveToCoord
// ---------------------------------------------------------------------------
bool AiScriptActionMoveToCoord(GbaPtr<const uint8_t> params) {
  AiMoveTowards(static_cast<int16_t>(params[0]),
                static_cast<int16_t>(params[1]), 0, 0xFFu, 1);
  return true;
}

// ---------------------------------------------------------------------------
// 0x0803AA60: AiScriptActionWait
// ---------------------------------------------------------------------------
bool AiScriptActionWait(GbaPtr<const uint8_t> /*params*/) {
  *GbaPtr<uint8_t>(kAiStateAddr + 0x79u) = 4u;
  return true;
}

// ---------------------------------------------------------------------------
// 0x0803AA70: AiGetStaffActionIndex
// ---------------------------------------------------------------------------
int AiGetStaffActionIndex(uint16_t item) {
  if (!CanUnitUseStaff(ConstUnit(ActiveUnit()), item)) {
    return -1;
  }
  const uint16_t iid = static_cast<uint16_t>(GetItemIid(item));
  int idx = 0;
  while (true) {
    const uint32_t entry =
        kAiStaffActionTableAddr + static_cast<uint32_t>(idx) * 8u;
    const uint16_t entry_iid = *GbaPtr<const uint16_t>(entry);
    if (entry_iid == 0u) {
      break;
    }
    if (iid == entry_iid && *GbaPtr<const uint32_t>(entry + 4u) != 0u) {
      return idx;
    }
    ++idx;
  }
  return -1;
}

// ---------------------------------------------------------------------------
// 0x0803AAD8: AiTrySpecialAction
// ---------------------------------------------------------------------------
bool AiTrySpecialAction(uint32_t pred_addr) {
  GbaPtr<Unit> unit = ActiveUnit();
  if (unit->status == static_cast<uint8_t>(UnitStatus::kSilenced)) {
    return Decision()->action_performed != 0;
  }

  uint8_t best_req_exp = 0;
  for (int slot = 0; slot < 5; ++slot) {
    const uint16_t item = ActiveUnit()->items[slot];
    if (item == 0u) {
      break;
    }
    if ((GetItemAttributes(item) & 4u) == 0u) {
      continue;
    }
    if (GetItemRequiredExp(item) < static_cast<int>(best_req_exp)) {
      continue;
    }
    const int action_idx = AiGetStaffActionIndex(item);
    if (action_idx == -1) {
      continue;
    }
    const uint32_t fn = *GbaPtr<const uint32_t>(
        kAiStaffActionTableAddr + 4u + static_cast<uint32_t>(action_idx) * 8u);
    GbaCall(fn & ~1u, static_cast<uint32_t>(slot), pred_addr);
    if (Decision()->action_performed != 0) {
      best_req_exp = static_cast<uint8_t>(GetItemRequiredExp(item));
    }
  }
  return Decision()->action_performed != 0;
}

// ---------------------------------------------------------------------------
// 0x0803AB80: AiFindBestAdjacentStaffPos
// ---------------------------------------------------------------------------
bool AiFindBestAdjacentStaffPos(int x, int y, GbaPtr<AiPos> out_pos) {
  AiPos pos{};
  if (!FindBestAdjacentStaffPosImpl(x, y, &pos)) {
    return false;
  }
  out_pos->x = pos.x;
  out_pos->y = pos.y;
  return true;
}

// ---------------------------------------------------------------------------
// 0x0803AC50: AiTryHealStaff
// ---------------------------------------------------------------------------
void AiTryHealStaff(int slot, uint32_t pred_addr) {
  uint8_t min_hp_pct = 100u;
  int best_x = -1;
  int best_y = -1;
  uint8_t best_uid = 0;

  AiFloodMovementForUnit(ConstUnit(ActiveUnit()));
  GbaCall(kAddrMapClearOther, 0xFFFFFFFFu);
  GbaCall(kAddrMapFloodRangeExtend);

  const uint8_t cfg_pct = *GbaPtr<const uint8_t>(kAiStateAddr + 0x7Cu);
  if (cfg_pct != 0u) {
    min_hp_pct = cfg_pct;
  }

  const MapSize sz = GetMapSize();
  for (int iy = sz.y - 1; iy >= 0; --iy) {
    for (int ix = sz.x - 1; ix >= 0; --ix) {
      if (MapMovement()[iy][ix] > 0x78u) {
        continue;
      }
      const uint8_t uid = MapUnit()[iy][ix];
      if (uid == 0u || uid == ActiveUnitId()) {
        continue;
      }
      GbaPtr<Unit> target = GetUnit(uid);
      if ((*GbaPtr<const uint8_t>(kAiStateAddr + 0x7Bu) & 4u) == 0u &&
          pred_addr != 0u) {
        if (static_cast<int8_t>(GbaCall(pred_addr & ~1u, target.addr)) == 1) {
          continue;
        }
      }
      if (*GbaPtr<const uint8_t>(kAiStateAddr + 0x7Cu) == 0u &&
          (target->ai_flags & 1u) == 0u) {
        continue;
      }
      const uint8_t hp_pct = static_cast<uint8_t>(BiosDiv(
          GetUnitCurrentHp(target) * 100, GetUnitMaxHp(ConstUnit(target))));
      if (hp_pct > min_hp_pct) {
        continue;
      }
      AiPos pos{};
      if (!FindBestAdjacentStaffPosImpl(ix, iy, &pos)) {
        continue;
      }
      min_hp_pct = hp_pct;
      best_x = pos.x;
      best_y = pos.y;
      best_uid = MapUnit()[iy][ix];
    }
  }

  if (best_x != -1) {
    AiSetDecision(static_cast<int16_t>(best_x), static_cast<int16_t>(best_y), 5,
                  best_uid, static_cast<uint8_t>(slot), 0, 0);
  }
}

// ---------------------------------------------------------------------------
// 0x0803ADC8: AiTryPhysicStaff
// ---------------------------------------------------------------------------
void AiTryPhysicStaff(int slot, uint32_t pred_addr) {
  if ((*GbaPtr<const uint8_t>(kAiStateAddr + 0x7Bu) & 4u) != 0u) {
    return;
  }

  uint8_t min_hp_pct = 100u;
  int best_x = -1;
  int best_y = -1;
  uint8_t best_uid = 0;

  AiFloodMovementForUnit(ConstUnit(ActiveUnit()));
  GbaCall(kAddrMapClearOther, 0xFFFFFFFFu);

  const uint8_t cfg_pct = *GbaPtr<const uint8_t>(kAiStateAddr + 0x7Cu);
  if (cfg_pct != 0u) {
    min_hp_pct = cfg_pct;
  }

  // Use a temporary AiPos in unused EWRAM? No, compute via FindBestPosition
  // directly into a local helper — wait: AiFindBestPositionInRange takes
  // GbaPtr<AiPos>. Let's implement the exact scan inline here (same 12-line
  // loop as AiFindBestPositionInRange) to avoid GBA stack allocation!
  for (int uid = 1; uid <= 0xBF; ++uid) {
    GbaPtr<Unit> target = GetUnit(uid);
    if (!target || !target->pinfo) {
      continue;
    }
    if (MapUnit()[target->y][target->x] == ActiveUnitId()) {
      continue;
    }
    if ((target->flags & 0x00010005u) != 0u) {
      continue;
    }
    if ((*GbaPtr<const uint8_t>(kAiStateAddr + 0x7Bu) & 4u) == 0u &&
        pred_addr != 0u) {
      if (static_cast<int8_t>(GbaCall(pred_addr & ~1u, target.addr)) == 1) {
        continue;
      }
    }
    if (*GbaPtr<const uint8_t>(kAiStateAddr + 0x7Cu) == 0u &&
        (target->ai_flags & 1u) == 0u) {
      continue;
    }

    GbaPtr<Unit> active = ActiveUnit();
    const int mag_range = GetUnitMagRange(ConstUnit(active));
    const uint8_t max_dist = static_cast<uint8_t>(
        mag_range + static_cast<uint8_t>(active->bonus_mov) +
        active->jinfo->base_mov);
    if (!AiIsWithinManhattanDistance(
            active->x, active->y, static_cast<uint8_t>(target->x),
            static_cast<uint8_t>(target->y), max_dist)) {
      continue;
    }

    MapFill(MapRange(), 0);
    MapAddInRange(target->x, target->y, GetUnitMagRange(ConstUnit(active)), 1);

    // Inline AiFindBestPositionInRange (0x08037380) without GBA stack scratch.
    AiPos pos{};
    uint8_t best_pos_score = 0xFFu;
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
        const uint8_t danger = MapOther()[iy][ix];
        if (danger <= best_pos_score) {
          best_pos_score = danger;
          pos.x = static_cast<int16_t>(ix);
          pos.y = static_cast<int16_t>(iy);
        }
      }
    }
    if (best_pos_score == 0xFFu) {
      continue;
    }

    const uint8_t hp_pct = static_cast<uint8_t>(BiosDiv(
        GetUnitCurrentHp(target) * 100, GetUnitMaxHp(ConstUnit(target))));
    if (hp_pct > min_hp_pct) {
      continue;
    }
    min_hp_pct = hp_pct;
    best_x = pos.x;
    best_y = pos.y;
    best_uid = MapUnit()[target->y][target->x];
  }

  if (best_x != -1) {
    AiSetDecision(static_cast<int16_t>(best_x), static_cast<int16_t>(best_y), 5,
                  best_uid, static_cast<uint8_t>(slot), 0, 0);
  }
}

// ---------------------------------------------------------------------------
// 0x0803AF98: AiTryFortifyStaff
// ---------------------------------------------------------------------------
void AiTryFortifyStaff(int slot) {
  if ((*GbaPtr<const uint8_t>(kAiStateAddr + 0x7Bu) & 4u) != 0u) {
    return;
  }
  if (AiCountAlliesInRecoveryMode() <= 2) {
    return;
  }

  AiFloodMovementForUnit(ConstUnit(ActiveUnit()));
  GbaCall(kAddrMapClearOther, 0xFFFFFFFFu);

  int best_count = 0;
  int best_x = 0;
  int best_y = 0;

  const MapSize sz = GetMapSize();
  for (int iy = sz.y - 1; iy >= 0; --iy) {
    for (int ix = sz.x - 1; ix >= 0; --ix) {
      if (MapMovement()[iy][ix] > 0x78u) {
        continue;
      }
      const uint8_t uid = MapUnit()[iy][ix];
      if (uid != 0u && uid != ActiveUnitId()) {
        continue;
      }
      MapFill(MapRange(), 0);
      MapAddInRange(ix, iy, GetUnitMagRange(ConstUnit(ActiveUnit())), 1);
      const int count = AiCountAlliesInRangeNeedingRecovery();
      if (count > best_count) {
        best_count = count;
        best_x = ix;
        best_y = iy;
      }
    }
  }

  if (best_count > 1) {
    AiSetDecision(static_cast<int16_t>(best_x), static_cast<int16_t>(best_y), 5,
                  0, static_cast<uint8_t>(slot), 0, 0);
  }
}

// ---------------------------------------------------------------------------
// Native dispatch thunks
// ---------------------------------------------------------------------------
namespace {

uint32_t ThunkAiTryAttackFromPos(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                                 const uint32_t* absl_nullable) {
  return AiTryAttackFromPos(static_cast<int>(r0), static_cast<int>(r1)) ? 1u
                                                                        : 0u;
}
uint32_t ThunkAiIsTileOccupiedByEnemy(uint32_t r0, uint32_t r1, uint32_t,
                                      uint32_t, const uint32_t* absl_nullable) {
  return AiIsTileOccupiedByEnemy(static_cast<int>(r0), static_cast<int>(r1))
             ? 1u
             : 0u;
}
uint32_t ThunkAiScriptActionCountEnemiesInRange(uint32_t r0, uint32_t, uint32_t,
                                                uint32_t,
                                                const uint32_t* absl_nullable) {
  return AiScriptActionCountEnemiesInRange(GbaPtr<const uint8_t>(r0)) ? 1u : 0u;
}
uint32_t ThunkAiScriptActionMoveOrAttackInRegion(
    uint32_t r0, uint32_t, uint32_t, uint32_t, const uint32_t* absl_nullable) {
  return AiScriptActionMoveOrAttackInRegion(GbaPtr<const uint8_t>(r0)) ? 1u
                                                                       : 0u;
}
uint32_t ThunkAiScriptActionLoadUnitCounter(uint32_t r0, uint32_t, uint32_t,
                                            uint32_t,
                                            const uint32_t* absl_nullable) {
  return AiScriptActionLoadUnitCounter(GbaPtr<const uint8_t>(r0)) ? 1u : 0u;
}
uint32_t ThunkAiScriptActionClearAiStateFlagA(uint32_t r0, uint32_t, uint32_t,
                                              uint32_t,
                                              const uint32_t* absl_nullable) {
  return AiScriptActionClearAiStateFlagA(GbaPtr<const uint8_t>(r0)) ? 1u : 0u;
}
uint32_t ThunkAiScriptActionClearAiStateFlagB(uint32_t r0, uint32_t, uint32_t,
                                              uint32_t,
                                              const uint32_t* absl_nullable) {
  return AiScriptActionClearAiStateFlagB(GbaPtr<const uint8_t>(r0)) ? 1u : 0u;
}
uint32_t ThunkAiGetNextPatrolWaypoint(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                      const uint32_t* absl_nullable) {
  return AiGetNextPatrolWaypoint(GbaPtr<AiPos>(r0)) ? 1u : 0u;
}
uint32_t ThunkAiScriptActionPatrolOrAttack(uint32_t r0, uint32_t, uint32_t,
                                           uint32_t,
                                           const uint32_t* absl_nullable) {
  return AiScriptActionPatrolOrAttack(GbaPtr<const uint8_t>(r0)) ? 1u : 0u;
}
uint32_t ThunkAiScriptActionIsActiveUnitInRect(uint32_t r0, uint32_t, uint32_t,
                                               uint32_t,
                                               const uint32_t* absl_nullable) {
  return AiScriptActionIsActiveUnitInRect(GbaPtr<const uint8_t>(r0)) ? 1u : 0u;
}
uint32_t ThunkAiScriptActionTalk(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                 const uint32_t* absl_nullable) {
  return AiScriptActionTalk(GbaPtr<const uint8_t>(r0)) ? 1u : 0u;
}
uint32_t ThunkAiScriptActionProbeLeaderTarget(uint32_t r0, uint32_t, uint32_t,
                                              uint32_t,
                                              const uint32_t* absl_nullable) {
  return AiScriptActionProbeLeaderTarget(GbaPtr<const uint8_t>(r0)) ? 1u : 0u;
}
uint32_t ThunkAiIsUnitMatchingTargetPid(uint32_t r0, uint32_t, uint32_t,
                                        uint32_t,
                                        const uint32_t* absl_nullable) {
  return AiIsUnitMatchingTargetPid(GbaPtr<const Unit>(r0)) ? 1u : 0u;
}
uint32_t ThunkAiScriptActionProbePidTarget(uint32_t r0, uint32_t, uint32_t,
                                           uint32_t,
                                           const uint32_t* absl_nullable) {
  return AiScriptActionProbePidTarget(GbaPtr<const uint8_t>(r0)) ? 1u : 0u;
}
uint32_t ThunkAiIsUnitMatchingCachedUid(uint32_t r0, uint32_t, uint32_t,
                                        uint32_t,
                                        const uint32_t* absl_nullable) {
  return AiIsUnitMatchingCachedUid(GbaPtr<const Unit>(r0)) ? 1u : 0u;
}
uint32_t ThunkAiIsUnitInDirectionOfCachedTarget(uint32_t r0, uint32_t, uint32_t,
                                                uint32_t,
                                                const uint32_t* absl_nullable) {
  return AiIsUnitInDirectionOfCachedTarget(GbaPtr<const Unit>(r0)) ? 1u : 0u;
}
uint32_t ThunkAiScriptActionAttackOrChaseCachedTarget(
    uint32_t r0, uint32_t, uint32_t, uint32_t, const uint32_t* absl_nullable) {
  return AiScriptActionAttackOrChaseCachedTarget(GbaPtr<const uint8_t>(r0))
             ? 1u
             : 0u;
}
uint32_t ThunkAiScriptActionStaffOrAttack(uint32_t r0, uint32_t, uint32_t,
                                          uint32_t,
                                          const uint32_t* absl_nullable) {
  return AiScriptActionStaffOrAttack(GbaPtr<const uint8_t>(r0)) ? 1u : 0u;
}
uint32_t ThunkAiScriptActionLootOrAttack(uint32_t r0, uint32_t, uint32_t,
                                         uint32_t,
                                         const uint32_t* absl_nullable) {
  return AiScriptActionLootOrAttack(GbaPtr<const uint8_t>(r0)) ? 1u : 0u;
}
uint32_t ThunkAiScriptActionBallista(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                     const uint32_t* absl_nullable) {
  return AiScriptActionBallista(GbaPtr<const uint8_t>(r0)) ? 1u : 0u;
}
uint32_t ThunkAiScriptActionMoveToCoord(uint32_t r0, uint32_t, uint32_t,
                                        uint32_t,
                                        const uint32_t* absl_nullable) {
  return AiScriptActionMoveToCoord(GbaPtr<const uint8_t>(r0)) ? 1u : 0u;
}
uint32_t ThunkAiScriptActionWait(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                 const uint32_t* absl_nullable) {
  return AiScriptActionWait(GbaPtr<const uint8_t>(r0)) ? 1u : 0u;
}
uint32_t ThunkAiGetStaffActionIndex(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                    const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(
      AiGetStaffActionIndex(static_cast<uint16_t>(r0)));
}
uint32_t ThunkAiTrySpecialAction(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                 const uint32_t* absl_nullable) {
  return AiTrySpecialAction(r0) ? 1u : 0u;
}
uint32_t ThunkAiFindBestAdjacentStaffPos(uint32_t r0, uint32_t r1, uint32_t r2,
                                         uint32_t,
                                         const uint32_t* absl_nullable) {
  return AiFindBestAdjacentStaffPos(static_cast<int>(r0), static_cast<int>(r1),
                                    GbaPtr<AiPos>(r2))
             ? 1u
             : 0u;
}
uint32_t ThunkAiTryHealStaff(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                             const uint32_t* absl_nullable) {
  AiTryHealStaff(static_cast<int>(r0), r1);
  return 0u;
}
uint32_t ThunkAiTryPhysicStaff(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                               const uint32_t* absl_nullable) {
  AiTryPhysicStaff(static_cast<int>(r0), r1);
  return 0u;
}
uint32_t ThunkAiTryFortifyStaff(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                const uint32_t* absl_nullable) {
  AiTryFortifyStaff(static_cast<int>(r0));
  return 0u;
}

}  // namespace

void RegisterAiActionFunctions() {
  RegisterNative(kAddrAiTryAttackFromPos, &ThunkAiTryAttackFromPos);
  RegisterNative(kAddrAiIsTileOccupiedByEnemy, &ThunkAiIsTileOccupiedByEnemy);
  RegisterNative(kAddrAiScriptActionCountEnemiesInRange,
                 &ThunkAiScriptActionCountEnemiesInRange);
  RegisterNative(kAddrAiScriptActionMoveOrAttackInRegion,
                 &ThunkAiScriptActionMoveOrAttackInRegion);
  RegisterNative(kAddrAiScriptActionLoadUnitCounter,
                 &ThunkAiScriptActionLoadUnitCounter);
  RegisterNative(kAddrAiScriptActionClearAiStateFlagA,
                 &ThunkAiScriptActionClearAiStateFlagA);
  RegisterNative(kAddrAiScriptActionClearAiStateFlagB,
                 &ThunkAiScriptActionClearAiStateFlagB);
  RegisterNative(kAddrAiGetNextPatrolWaypoint, &ThunkAiGetNextPatrolWaypoint);
  RegisterNative(kAddrAiScriptActionPatrolOrAttack,
                 &ThunkAiScriptActionPatrolOrAttack);
  RegisterNative(kAddrAiScriptActionIsActiveUnitInRect,
                 &ThunkAiScriptActionIsActiveUnitInRect);
  RegisterNative(kAddrAiScriptActionTalk, &ThunkAiScriptActionTalk);
  RegisterNative(kAddrAiScriptActionProbeLeaderTarget,
                 &ThunkAiScriptActionProbeLeaderTarget);
  RegisterNative(kAddrAiIsUnitMatchingTargetPid,
                 &ThunkAiIsUnitMatchingTargetPid);
  RegisterNative(kAddrAiScriptActionProbePidTarget,
                 &ThunkAiScriptActionProbePidTarget);
  RegisterNative(kAddrAiIsUnitMatchingCachedUid,
                 &ThunkAiIsUnitMatchingCachedUid);
  RegisterNative(kAddrAiIsUnitInDirectionOfCachedTarget,
                 &ThunkAiIsUnitInDirectionOfCachedTarget);
  RegisterNative(kAddrAiScriptActionAttackOrChaseCachedTarget,
                 &ThunkAiScriptActionAttackOrChaseCachedTarget);
  RegisterNative(kAddrAiScriptActionStaffOrAttack,
                 &ThunkAiScriptActionStaffOrAttack);
  RegisterNative(kAddrAiScriptActionLootOrAttack,
                 &ThunkAiScriptActionLootOrAttack);
  RegisterNative(kAddrAiScriptActionBallista, &ThunkAiScriptActionBallista);
  RegisterNative(kAddrAiScriptActionMoveToCoord,
                 &ThunkAiScriptActionMoveToCoord);
  RegisterNative(kAddrAiScriptActionWait, &ThunkAiScriptActionWait);
  RegisterNative(kAddrAiGetStaffActionIndex, &ThunkAiGetStaffActionIndex);
  RegisterNative(kAddrAiTrySpecialAction, &ThunkAiTrySpecialAction);
  RegisterNative(kAddrAiFindBestAdjacentStaffPos,
                 &ThunkAiFindBestAdjacentStaffPos);
  RegisterNative(kAddrAiTryHealStaff, &ThunkAiTryHealStaff);
  RegisterNative(kAddrAiTryPhysicStaff, &ThunkAiTryPhysicStaff);
  RegisterNative(kAddrAiTryFortifyStaff, &ThunkAiTryFortifyStaff);
}

}  // namespace fe7
