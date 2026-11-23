#include "game/ai/ai_opcodes.h"

#include <cstdint>

#include "absl/base/nullability.h"
#include "game/ai/ai.h"
#include "game/ai/ai_decide.h"
#include "game/ai/ai_script.h"
#include "game/engine/rng.h"
#include "game/item/item.h"
#include "game/map/map.h"
#include "game/map/trap.h"
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

inline MapSize GetMapSize() { return *GbaPtr<MapSize>(kMapSizeAddr); }

inline GbaPtr<GbaPtr<uint8_t>> MapTerrain() {
  return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr);
}

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

inline GbaPtr<const AiScriptCmd> CurCmd() {
  return *GbaPtr<const GbaPtr<const AiScriptCmd>>(kAiScriptCurAddr);
}

// Host-pointer implementations shared between public GbaPtr entry points and
// script opcode handlers (avoiding GBA stack scratch).

bool FindBestAdjacentPositionImpl(int x, int y, uint32_t score_fn_addr,
                                  AiPos* absl_nonnull out_pos) {
  uint32_t best_cost = 0xFFu;
  GbaPtr<const int8_t> offsets(kAiCardinalOffsetsAddr);
  for (int i = 0; i < 4; ++i) {
    const int tx = x + static_cast<int>(offsets[i * 2]);
    const int ty = y + static_cast<int>(offsets[i * 2 + 1]);
    const uint32_t cost =
        GbaCall(score_fn_addr & ~1u, static_cast<uint32_t>(tx),
                static_cast<uint32_t>(ty)) &
        0xFFu;
    if (cost != 0xFFu && cost < best_cost) {
      best_cost = cost;
      out_pos->x = static_cast<int16_t>(tx);
      out_pos->y = static_cast<int16_t>(ty);
    }
  }
  return best_cost != 0xFFu;
}

bool TryFindUnitByPidImpl(int pid, AiPos* absl_nonnull out_pos) {
  GbaPtr<const Unit> active = ConstUnit(ActiveUnit());
  MapFloodRangeUnitless(active->x, active->y, GetUnitMovementCost(active));
  GbaCall(kAddrMapFloodPostProcess);

  out_pos->x = -1;
  GbaPtr<GbaPtr<uint8_t>> range = MapRange();

  for (int uid = 1; uid <= 0xBF; ++uid) {
    GbaPtr<Unit> unit = GetUnit(uid);
    if (!unit || !unit->pinfo) {
      continue;
    }
    if (range[unit->y][unit->x] > 0x78u) {
      continue;
    }
    if (unit->pinfo->id != pid) {
      continue;
    }
    if ((unit->flags & kUnitFlagDead) != 0u) {
      *GbaPtr<uint8_t>(kAiStateAddr + 0x86u) = 1u;
      return false;
    }
    if ((unit->flags & kUnitFlagRescued) != 0u) {
      *GbaPtr<uint8_t>(kAiStateAddr + 0x86u) = 3u;
    }
    out_pos->x = unit->x;
    out_pos->y = unit->y;
  }

  if (out_pos->x >= 0) {
    return true;
  }

  GbaPtr<Unit> target = GetUnitByPid(pid);
  if ((target->flags & 0x0001000Cu) != 0u) {
    *GbaPtr<uint8_t>(kAiStateAddr + 0x86u) = 1u;
  } else {
    *GbaPtr<uint8_t>(kAiStateAddr + 0x86u) = 4u;
  }
  return false;
}

bool TryFindClosestUnitMatchingImpl(uint32_t pred_addr, bool no_doors,
                                    AiPos* absl_nonnull out_pos) {
  GbaPtr<const Unit> active = ConstUnit(ActiveUnit());
  GbaPtr<const int8_t> mov_cost = GetUnitMovementCost(active);
  if (no_doors) {
    GbaCall(kAddrAiMapFloodNoDoors, static_cast<uint32_t>(active->x),
            static_cast<uint32_t>(active->y), mov_cost.addr);
  } else {
    MapFloodRangeUnitless(active->x, active->y, mov_cost);
  }

  uint32_t min_cost = 0xFFu;
  int16_t best_x = -1;
  int16_t best_y = 0;

  const MapSize size = GetMapSize();
  GbaPtr<GbaPtr<uint8_t>> range = MapRange();
  GbaPtr<GbaPtr<uint8_t>> unit_map = MapUnit();
  const uint8_t active_id = ActiveUnitId();

  for (int16_t y = static_cast<int16_t>(size.y - 1); y >= 0; --y) {
    for (int16_t x = static_cast<int16_t>(size.x - 1); x >= 0; --x) {
      if (range[y][x] > 0x78u) {
        continue;
      }
      const uint8_t uid = unit_map[y][x];
      if (uid == 0u || uid == active_id) {
        continue;
      }
      GbaPtr<Unit> u = GetUnit(uid);
      if ((GbaCall(pred_addr & ~1u, u.addr) & 0xFFu) == 0u) {
        continue;
      }
      const uint8_t cost = range[y][x];
      if (cost <= min_cost) {
        min_cost = cost;
        best_x = x;
        best_y = y;
      }
    }
  }

  if (best_x < 0) {
    return false;
  }
  out_pos->x = best_x;
  out_pos->y = best_y;
  return true;
}

bool FindNearestTerrainInListImpl(GbaPtr<const uint8_t> terrain_list,
                                  uint32_t flags, AiPos* absl_nonnull out_pos) {
  int min_cost = 0xFF;
  const MapSize size = GetMapSize();
  GbaPtr<GbaPtr<uint8_t>> range = MapRange();
  GbaPtr<GbaPtr<uint8_t>> terrain = MapTerrain();
  GbaPtr<GbaPtr<uint8_t>> unit_map = MapUnit();
  GbaPtr<const Unit> active = ConstUnit(ActiveUnit());

  for (int y = size.y - 1; y >= 0; --y) {
    for (int x = size.x - 1; x >= 0; --x) {
      if (range[y][x] > 0x78u) {
        continue;
      }
      if (!AiIsInByteList(terrain_list, terrain[y][x])) {
        continue;
      }
      if ((flags & 1u) != 0u) {
        const uint8_t uid = unit_map[y][x];
        if (uid != 0u && !AreUnitIdsAllied(active->id, uid)) {
          continue;
        }
      }
      if ((flags & 2u) != 0u) {
        if (AiCountEnemiesNearby(static_cast<int16_t>(x),
                                 static_cast<int16_t>(y)) != 0) {
          continue;
        }
      }
      const int cost = static_cast<int8_t>(range[y][x]);
      if (min_cost > cost) {
        out_pos->x = static_cast<int16_t>(x);
        out_pos->y = static_cast<int16_t>(y);
        min_cost = range[y][x];
      }
    }
  }
  return min_cost != 0xFF;
}

bool FindNearestLootableTileImpl(AiPos* absl_nonnull out_pos,
                                 uint8_t* absl_nonnull out_key_slot) {
  GbaPtr<const Unit> active = ConstUnit(ActiveUnit());
  SetWorkingMoveTable(GetUnitMovementCost(active));
  SetWorkingMap(MapRange());
  BeginMapFlood(active->x, active->y, 0x7C, active->id);

  // Use a local EWRAM-free check matching AiFindChestKeySlot.
  *out_key_slot = 0u;
  bool has_key = false;
  GbaPtr<Unit> mactive = ActiveUnit();
  if (GetUnitItemCount(active) == 5) {
    mactive->ai_flags = static_cast<uint8_t>(mactive->ai_flags | 8u);
  } else {
    for (int slot = 0; slot < 5; ++slot) {
      const uint16_t item = active->items[slot];
      if (item == 0u) {
        break;
      }
      *out_key_slot = static_cast<uint8_t>(slot);
      const int iid = GetItemIid(item);
      if (iid == 0x68 ||
          (iid == 0x6A && (UnitAttributes(*active) & 4u) != 0u)) {
        has_key = true;
        break;
      }
    }
  }

  GbaPtr<const uint8_t> tlist(has_key ? kAiLootTerrainsWithKeyAddr
                                      : kAiLootTerrainsNoKeyAddr);
  if (FindNearestTerrainInListImpl(tlist, 1u, out_pos)) {
    return true;
  }
  MapFloodRangeUnitless(active->x, active->y, GetUnitMovementCost(active));
  return FindNearestTerrainInListImpl(tlist, 0u, out_pos);
}

bool FindBestObstacleAttackPosImpl(int target_x, int target_y,
                                   AiPos* absl_nonnull out_pos,
                                   uint8_t* absl_nonnull out_slot) {
  int best_x = -1;
  int best_y = -1;
  uint32_t best_score = 0u;

  GbaPtr<const Unit> active = ConstUnit(ActiveUnit());
  MapFloodUnit(active);

  const MapSize size = GetMapSize();
  GbaPtr<GbaPtr<uint8_t>> mov = MapMovement();
  GbaPtr<GbaPtr<uint8_t>> range = MapRange();
  GbaPtr<GbaPtr<uint8_t>> unit_map = MapUnit();
  const uint8_t active_id = ActiveUnitId();

  for (int slot = 0; slot < 5; ++slot) {
    const uint16_t item = active->items[slot];
    if (item == 0u) {
      break;
    }
    if (!CanUnitUseWeapon(active, item)) {
      continue;
    }
    MapFill(range, 0);
    MapSetInRange(static_cast<int16_t>(target_x),
                  static_cast<int16_t>(target_y),
                  static_cast<int16_t>(GetItemMinRange(item)),
                  static_cast<int16_t>(GetItemMaxRange(item)));

    for (int y = size.y - 1; y >= 0; --y) {
      for (int x = size.x - 1; x >= 0; --x) {
        if (mov[y][x] > 0x78u || static_cast<int8_t>(range[y][x]) == 0) {
          continue;
        }
        const uint8_t uid = unit_map[y][x];
        if (uid != 0u && uid != active_id) {
          continue;
        }
        const uint32_t score = static_cast<uint32_t>(GetItemMight(item)) +
                               AiGetObstacleAttackPosScore(x, y);
        if (score > best_score) {
          best_x = x;
          best_y = y;
          best_score = score;
          *out_slot = static_cast<uint8_t>(slot);
        }
      }
    }
  }

  if (best_score == 0u) {
    return false;
  }
  out_pos->x = static_cast<int16_t>(best_x);
  out_pos->y = static_cast<int16_t>(best_y);
  return true;
}

void MoveTowardsImpl(int16_t x, int16_t y, uint8_t action_id,
                     uint8_t danger_threshold, uint8_t unitless_flood,
                     bool no_doors) {
  GbaPtr<const Unit> active = ConstUnit(ActiveUnit());
  if (active->x == x && active->y == y) {
    AiSetDecision(active->x, active->y, action_id, 0u, 0u, 0u, 0u);
    return;
  }

  if (unitless_flood != 0u) {
    GbaPtr<const int8_t> mov_cost = GetUnitMovementCost(active);
    if (no_doors) {
      GbaCall(kAddrAiMapFloodNoDoors, static_cast<uint32_t>(x),
              static_cast<uint32_t>(y), mov_cost.addr);
    } else {
      MapFloodRangeUnitless(x, y, mov_cost);
    }
  } else {
    GbaCall(
        no_doors ? kAddrAiMapFloodNoDoorsUnit : kAddrAiMapFloodRangeWithUnit,
        static_cast<uint32_t>(x), static_cast<uint32_t>(y), active.addr);
  }

  MapFloodUnit(active);

  GbaPtr<GbaPtr<uint8_t>> mov = MapMovement();
  GbaPtr<GbaPtr<uint8_t>> range = MapRange();
  GbaPtr<GbaPtr<uint8_t>> unit_map = MapUnit();
  GbaPtr<GbaPtr<uint8_t>> other = MapOther();
  const uint8_t active_id = ActiveUnitId();
  const MapSize size = GetMapSize();

  uint8_t min_dist = range[active->y][active->x];
  int16_t best_x = -1;
  int16_t best_y = 0;

  for (int16_t ty = static_cast<int16_t>(size.y - 1); ty >= 0; --ty) {
    for (int16_t tx = static_cast<int16_t>(size.x - 1); tx >= 0; --tx) {
      if (mov[ty][tx] > 0x78u) {
        continue;
      }
      const uint8_t uid = unit_map[ty][tx];
      if (uid != 0u && uid != active_id) {
        continue;
      }
      if (danger_threshold == 0u) {
        const int active_mov =
            static_cast<int>(active->bonus_mov) +
            static_cast<int>(static_cast<int8_t>(active->jinfo->base_mov));
        const int max_blue_mov =
            static_cast<int>(*GbaPtr<const uint8_t>(kAiStateAddr + 0x85u));
        if (active_mov < max_blue_mov && other[ty][tx] != 0u) {
          continue;
        }
      }
      if (!AiIsWithinDangerThreshold(tx, ty, danger_threshold)) {
        continue;
      }
      const uint8_t dist = range[ty][tx];
      if (dist <= min_dist) {
        min_dist = dist;
        best_x = tx;
        best_y = ty;
      }
    }
  }

  if (best_x >= 0) {
    AiSetDecision(best_x, best_y, action_id, 0u, 0u, 0u, 0u);
  }
}

// Dispatch thunks ------------------------------------------------------------

uint32_t ThunkAiTryFindUnitByPid(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                                 const uint32_t* absl_nullable) {
  return AiTryFindUnitByPid(static_cast<int32_t>(r0), GbaPtr<AiPos>(r1)) ? 1u
                                                                         : 0u;
}

uint32_t ThunkAiTryFindClosestUnitMatching(uint32_t r0, uint32_t r1, uint32_t,
                                           uint32_t,
                                           const uint32_t* absl_nullable) {
  return AiTryFindClosestUnitMatching(r0, GbaPtr<AiPos>(r1)) ? 1u : 0u;
}

uint32_t ThunkAiTryFindClosestUnitMatchingNoDoors(
    uint32_t r0, uint32_t r1, uint32_t, uint32_t,
    const uint32_t* absl_nullable) {
  return AiTryFindClosestUnitMatchingNoDoors(r0, GbaPtr<AiPos>(r1)) ? 1u : 0u;
}

uint32_t ThunkAiFindNearestTerrainInList(uint32_t r0, uint32_t r1, uint32_t r2,
                                         uint32_t,
                                         const uint32_t* absl_nullable) {
  return AiFindNearestTerrainInList(GbaPtr<const uint8_t>(r0), r1,
                                    GbaPtr<AiPos>(r2))
             ? 1u
             : 0u;
}

uint32_t ThunkAiFindNearestAdjacentToTerrainInList(
    uint32_t r0, uint32_t r1, uint32_t r2, uint32_t,
    const uint32_t* absl_nullable) {
  return AiFindNearestAdjacentToTerrainInList(GbaPtr<const uint8_t>(r0), r1,
                                              GbaPtr<AiPos>(r2))
             ? 1u
             : 0u;
}

uint32_t ThunkAiFindNearestDoorOrChest(uint32_t r0, uint32_t r1, uint32_t,
                                       uint32_t,
                                       const uint32_t* absl_nullable) {
  return AiFindNearestDoorOrChest(r0, GbaPtr<AiPos>(r1)) ? 1u : 0u;
}

uint32_t ThunkAiFindBestAdjacentPosition(uint32_t r0, uint32_t r1, uint32_t r2,
                                         uint32_t r3,
                                         const uint32_t* absl_nullable) {
  return AiFindBestAdjacentPosition(static_cast<int32_t>(r0),
                                    static_cast<int32_t>(r1), r2,
                                    GbaPtr<AiPos>(r3))
             ? 1u
             : 0u;
}

uint32_t ThunkAiFindNearestLootableTile(uint32_t r0, uint32_t r1, uint32_t,
                                        uint32_t,
                                        const uint32_t* absl_nullable) {
  return AiFindNearestLootableTile(GbaPtr<AiPos>(r0), GbaPtr<uint8_t>(r1)) ? 1u
                                                                           : 0u;
}

uint32_t ThunkAiMoveTowards(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                            const uint32_t* absl_nullable stack_args) {
  const uint8_t unitless =
      stack_args != nullptr ? static_cast<uint8_t>(stack_args[0]) : 0u;
  AiMoveTowards(static_cast<int16_t>(r0), static_cast<int16_t>(r1),
                static_cast<uint8_t>(r2), static_cast<uint8_t>(r3), unitless);
  return 0u;
}

uint32_t ThunkAiMoveTowardsNoDoors(uint32_t r0, uint32_t r1, uint32_t r2,
                                   uint32_t r3,
                                   const uint32_t* absl_nullable stack_args) {
  const uint8_t unitless =
      stack_args != nullptr ? static_cast<uint8_t>(stack_args[0]) : 0u;
  AiMoveTowardsNoDoors(static_cast<int16_t>(r0), static_cast<int16_t>(r1),
                       static_cast<uint8_t>(r2), static_cast<uint8_t>(r3),
                       unitless);
  return 0u;
}

uint32_t ThunkAiScriptCmdAttackPid(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                   const uint32_t* absl_nullable) {
  AiScriptCmdAttackPid(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdAttack(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                const uint32_t* absl_nullable) {
  AiScriptCmdAttack(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdNop06(uint32_t r0, uint32_t, uint32_t, uint32_t,
                               const uint32_t* absl_nullable) {
  AiScriptCmdNop06(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdAttackInPlace(uint32_t r0, uint32_t, uint32_t,
                                       uint32_t,
                                       const uint32_t* absl_nullable) {
  AiScriptCmdAttackInPlace(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdAttackJid(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                   const uint32_t* absl_nullable) {
  AiScriptCmdAttackJid(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdAction09(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                  const uint32_t* absl_nullable) {
  AiScriptCmdAction09(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdAction0A(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                  const uint32_t* absl_nullable) {
  AiScriptCmdAction0A(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdAction0B(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                  const uint32_t* absl_nullable) {
  AiScriptCmdAction0B(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdMoveToCoord(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                     const uint32_t* absl_nullable) {
  AiScriptCmdMoveToCoord(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdMoveToPid(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                   const uint32_t* absl_nullable) {
  AiScriptCmdMoveToPid(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdNop0E(uint32_t r0, uint32_t, uint32_t, uint32_t,
                               const uint32_t* absl_nullable) {
  AiScriptCmdNop0E(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdMoveToJid(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                   const uint32_t* absl_nullable) {
  AiScriptCmdMoveToJid(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdLoot(uint32_t r0, uint32_t, uint32_t, uint32_t,
                              const uint32_t* absl_nullable) {
  AiScriptCmdLoot(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdMoveToSafe(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                    const uint32_t* absl_nullable) {
  AiScriptCmdMoveToSafe(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdMoveToEnemy(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                     const uint32_t* absl_nullable) {
  AiScriptCmdMoveToEnemy(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdMoveToEnemyNoDoors(uint32_t r0, uint32_t, uint32_t,
                                            uint32_t,
                                            const uint32_t* absl_nullable) {
  AiScriptCmdMoveToEnemyNoDoors(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdNop14(uint32_t r0, uint32_t, uint32_t, uint32_t,
                               const uint32_t* absl_nullable) {
  AiScriptCmdNop14(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdNop15(uint32_t r0, uint32_t, uint32_t, uint32_t,
                               const uint32_t* absl_nullable) {
  AiScriptCmdNop15(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdMoveRandom(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                    const uint32_t* absl_nullable) {
  AiScriptCmdMoveRandom(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdEscape(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                const uint32_t* absl_nullable) {
  AiScriptCmdEscape(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiGetObstacleAttackPosScore(uint32_t r0, uint32_t r1, uint32_t,
                                          uint32_t,
                                          const uint32_t* absl_nullable) {
  return AiGetObstacleAttackPosScore(static_cast<int32_t>(r0),
                                     static_cast<int32_t>(r1));
}

uint32_t ThunkAiFindBestObstacleAttackPos(uint32_t r0, uint32_t r1, uint32_t r2,
                                          uint32_t r3,
                                          const uint32_t* absl_nullable) {
  return AiFindBestObstacleAttackPos(static_cast<int32_t>(r0),
                                     static_cast<int32_t>(r1),
                                     GbaPtr<AiPos>(r2), GbaPtr<uint8_t>(r3))
             ? 1u
             : 0u;
}

uint32_t ThunkAiFindNearestObstacleTerrain(uint32_t r0, uint32_t r1,
                                           uint32_t r2, uint32_t,
                                           const uint32_t* absl_nullable) {
  return AiFindNearestObstacleTerrain(GbaPtr<const uint8_t>(r0), r1,
                                      GbaPtr<AiPos>(r2))
             ? 1u
             : 0u;
}

uint32_t ThunkAiScriptCmdAttackObstacle(uint32_t r0, uint32_t, uint32_t,
                                        uint32_t,
                                        const uint32_t* absl_nullable) {
  AiScriptCmdAttackObstacle(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdMoveToTerrain(uint32_t r0, uint32_t, uint32_t,
                                       uint32_t,
                                       const uint32_t* absl_nullable) {
  AiScriptCmdMoveToTerrain(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdMoveToTerrainList(uint32_t r0, uint32_t, uint32_t,
                                           uint32_t,
                                           const uint32_t* absl_nullable) {
  AiScriptCmdMoveToTerrainList(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdLabel(uint32_t r0, uint32_t, uint32_t, uint32_t,
                               const uint32_t* absl_nullable) {
  AiScriptCmdLabel(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiBerserkAction(uint32_t, uint32_t, uint32_t, uint32_t,
                              const uint32_t* absl_nullable) {
  AiBerserkAction();
  return 0u;
}

uint32_t ThunkAiBerserkMove(uint32_t, uint32_t, uint32_t, uint32_t,
                            const uint32_t* absl_nullable) {
  AiBerserkMove();
  return 0u;
}

uint32_t ThunkAiReturnTrue(uint32_t, uint32_t, uint32_t, uint32_t,
                           const uint32_t* absl_nullable) {
  return AiReturnTrue() ? 1u : 0u;
}

uint32_t ThunkAiCheckJobRankAndReturnTrue(uint32_t r0, uint32_t, uint32_t,
                                          uint32_t,
                                          const uint32_t* absl_nullable) {
  return AiCheckJobRankAndReturnTrue(GbaPtr<const uint8_t>(r0)) ? 1u : 0u;
}

}  // namespace

bool AiTryFindUnitByPid(int pid, GbaPtr<AiPos> out_pos) {
  return TryFindUnitByPidImpl(pid, out_pos.get());
}

bool AiTryFindClosestUnitMatching(uint32_t pred_addr, GbaPtr<AiPos> out_pos) {
  return TryFindClosestUnitMatchingImpl(pred_addr, /*no_doors=*/false,
                                        out_pos.get());
}

bool AiTryFindClosestUnitMatchingNoDoors(uint32_t pred_addr,
                                         GbaPtr<AiPos> out_pos) {
  return TryFindClosestUnitMatchingImpl(pred_addr, /*no_doors=*/true,
                                        out_pos.get());
}

bool AiFindNearestTerrainInList(GbaPtr<const uint8_t> terrain_list,
                                uint32_t flags, GbaPtr<AiPos> out_pos) {
  return FindNearestTerrainInListImpl(terrain_list, flags, out_pos.get());
}

bool AiFindNearestAdjacentToTerrainInListOut(GbaPtr<const uint8_t> terrain_list,
                                             uint32_t flags, AiPos* out_pos) {
  int min_cost = 0xFF;
  const MapSize size = GetMapSize();
  GbaPtr<GbaPtr<uint8_t>> range = MapRange();
  GbaPtr<GbaPtr<uint8_t>> terrain = MapTerrain();
  GbaPtr<GbaPtr<uint8_t>> unit_map = MapUnit();
  GbaPtr<const Unit> active = ConstUnit(ActiveUnit());

  for (int y = size.y - 1; y >= 0; --y) {
    for (int x = size.x - 1; x >= 0; --x) {
      if (range[y][x] > 0x78u) {
        continue;
      }
      if (!AiIsInByteList(terrain_list, terrain[y][x])) {
        continue;
      }
      if ((flags & 1u) != 0u) {
        const uint8_t uid = unit_map[y][x];
        if (uid != 0u && !AreUnitIdsAllied(active->id, uid)) {
          continue;
        }
      }
      if ((flags & 2u) != 0u) {
        if (AiCountEnemiesNearby(static_cast<int16_t>(x),
                                 static_cast<int16_t>(y)) != 0) {
          continue;
        }
      }
      AiPos adj{};
      if (!FindBestAdjacentPositionImpl(x, y, kAddrAiGetMoveCostAt | 1u,
                                        &adj)) {
        continue;
      }
      const int cost = static_cast<int8_t>(range[adj.y][adj.x]);
      if (min_cost > cost) {
        out_pos->x = adj.x;
        out_pos->y = adj.y;
        min_cost = range[adj.y][adj.x];
      }
    }
  }
  return min_cost != 0xFF;
}

bool AiFindNearestAdjacentToTerrainInList(GbaPtr<const uint8_t> terrain_list,
                                          uint32_t flags,
                                          GbaPtr<AiPos> out_pos) {
  return AiFindNearestAdjacentToTerrainInListOut(terrain_list, flags,
                                                 out_pos.get());
}

bool AiFindNearestDoorOrChestOut(uint32_t flags, AiPos* out_pos) {
  int min_cost = 0xFF;
  uint8_t target_count = 0u;
  const MapSize size = GetMapSize();
  GbaPtr<GbaPtr<uint8_t>> range = MapRange();
  GbaPtr<GbaPtr<uint8_t>> terrain = MapTerrain();
  GbaPtr<GbaPtr<uint8_t>> mov = MapMovement();
  GbaPtr<GbaPtr<uint8_t>> unit_map = MapUnit();
  GbaPtr<const Unit> active = ConstUnit(ActiveUnit());

  for (int y = size.y - 1; y >= 0; --y) {
    for (int x = size.x - 1; x >= 0; --x) {
      if (range[y][x] > 0x78u) {
        continue;
      }
      const uint8_t t = terrain[y][x];
      AiPos cand{};
      if (t == 0x1Eu) {
        target_count = static_cast<uint8_t>(target_count + 1u);
        if ((flags & 8u) != 0u) {
          continue;
        }
        if (!FindBestAdjacentPositionImpl(x, y, kAddrAiGetMoveCostAt | 1u,
                                          &cand)) {
          continue;
        }
      } else if (t == 0x21u) {
        target_count = static_cast<uint8_t>(target_count + 1u);
        if ((flags & 4u) != 0u) {
          continue;
        }
        cand.x = static_cast<int16_t>(x);
        cand.y = static_cast<int16_t>(y);
        const int active_mov =
            static_cast<int>(active->bonus_mov) +
            static_cast<int>(static_cast<int8_t>(active->jinfo->base_mov));
        if (static_cast<int>(mov[y][x]) <= active_mov) {
          if ((flags & 1u) != 0u) {
            const uint8_t uid = unit_map[cand.y][cand.x];
            if (uid != 0u && !AreUnitIdsAllied(active->id, uid)) {
              continue;
            }
          }
          if ((flags & 2u) != 0u && AiCountEnemiesNearby(cand.x, cand.y) != 0) {
            continue;
          }
          out_pos->x = cand.x;
          out_pos->y = cand.y;
          return true;
        }
      } else {
        continue;
      }

      if ((flags & 1u) != 0u) {
        const uint8_t uid = unit_map[cand.y][cand.x];
        if (uid != 0u && !AreUnitIdsAllied(active->id, uid)) {
          continue;
        }
      }
      if ((flags & 2u) != 0u && AiCountEnemiesNearby(cand.x, cand.y) != 0) {
        continue;
      }
      const int cost = static_cast<int8_t>(range[cand.y][cand.x]);
      if (min_cost > cost) {
        out_pos->x = cand.x;
        out_pos->y = cand.y;
        min_cost = range[cand.y][cand.x];
      }
    }
  }

  *GbaPtr<uint8_t>(kAiStateAddr + 0x87u) = 1u;
  if (target_count == 0u) {
    *GbaPtr<uint8_t>(kAiStateAddr + 0x86u) = 5u;
  }
  return min_cost != 0xFF;
}

bool AiFindNearestDoorOrChest(uint32_t flags, GbaPtr<AiPos> out_pos) {
  return AiFindNearestDoorOrChestOut(flags, out_pos.get());
}

bool AiFindBestAdjacentPosition(int x, int y, uint32_t score_fn_addr,
                                GbaPtr<AiPos> out_pos) {
  return FindBestAdjacentPositionImpl(x, y, score_fn_addr, out_pos.get());
}

bool AiFindNearestLootableTile(GbaPtr<AiPos> out_pos,
                               GbaPtr<uint8_t> out_key_slot) {
  return FindNearestLootableTileImpl(out_pos.get(), out_key_slot.get());
}

void AiMoveTowards(int16_t x, int16_t y, uint8_t action_id,
                   uint8_t danger_threshold, uint8_t unitless_flood) {
  MoveTowardsImpl(x, y, action_id, danger_threshold, unitless_flood,
                  /*no_doors=*/false);
}

void AiMoveTowardsNoDoors(int16_t x, int16_t y, uint8_t action_id,
                          uint8_t danger_threshold, uint8_t unitless_flood) {
  MoveTowardsImpl(x, y, action_id, danger_threshold, unitless_flood,
                  /*no_doors=*/true);
}

void AiScriptCmdAttackPid(GbaPtr<uint8_t> pc_ptr) {
  GbaPtr<const AiScriptCmd> cur = CurCmd();
  if (static_cast<uint8_t>(RandNext(100)) <= cur->arg1) {
    const int8_t special = static_cast<int8_t>(
        GbaCall(kAddrAiTrySpecialAction, kAddrAiIsUnitEnemy | 1u));
    if (special == 0) {
      if (AiIsUnitAliveByPid(static_cast<uint16_t>(cur->u32_arg4))) {
        GbaPtr<Unit> target = GetUnitByPid(static_cast<int>(cur->u32_arg4));
        if ((target->flags & kUnitFlagRescued) != 0u) {
          *GbaPtr<uint8_t>(kAiStateAddr + 0x86u) = 3u;
          *GbaPtr<int8_t>(kAiScriptDoneAddr) = 0;
        } else {
          GbaCall(kAddrAiPerformAttack, kAddrAiIsUnitEnemyWithPid | 1u);
        }
      } else {
        *GbaPtr<uint8_t>(kAiStateAddr + 0x86u) = 1u;
        *GbaPtr<int8_t>(kAiScriptDoneAddr) = 0;
      }
    }
  } else {
    *GbaPtr<uint8_t>(kAiStateAddr + 0x79u) = 4u;
  }
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
}

void AiScriptCmdAttack(GbaPtr<uint8_t> pc_ptr) {
  GbaPtr<const AiScriptCmd> cur = CurCmd();
  if (static_cast<uint8_t>(RandNext(100)) <= cur->arg1) {
    if (cur->u32_arg8 == 0u) {
      if ((GbaCall(kAddrAiTrySpecialAction, kAddrAiIsUnitEnemy | 1u) & 0xFFu) ==
          0u) {
        GbaCall(kAddrAiPerformAttack, kAddrAiIsUnitEnemy | 1u);
      }
    } else {
      if ((GbaCall(kAddrAiTrySpecialAction,
                   kAddrAiIsUnitEnemyOrInPidList | 1u) &
           0xFFu) == 0u) {
        GbaCall(kAddrAiPerformAttack, kAddrAiIsUnitEnemyNotInPidList | 1u);
      }
    }
  } else {
    *GbaPtr<uint8_t>(kAiStateAddr + 0x79u) = 4u;
  }
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
}

void AiScriptCmdNop06(GbaPtr<uint8_t> pc_ptr) {
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
}

void AiScriptCmdAttackInPlace(GbaPtr<uint8_t> pc_ptr) {
  GbaPtr<const AiScriptCmd> cur = CurCmd();
  if (static_cast<uint8_t>(RandNext(100)) <= cur->arg1) {
    GbaPtr<uint8_t> flags(kAiStateAddr + 0x7Bu);
    *flags = static_cast<uint8_t>(*flags | 2u);
    if ((GbaCall(kAddrAiTrySpecialAction, kAddrAiIsUnitEnemy | 1u) & 0xFFu) ==
        0u) {
      GbaCall(kAddrAiPerformAttack, kAddrAiIsUnitEnemy | 1u);
    }
  } else {
    *GbaPtr<uint8_t>(kAiStateAddr + 0x79u) = 4u;
  }
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
}

void AiScriptCmdAttackJid(GbaPtr<uint8_t> pc_ptr) {
  GbaPtr<const AiScriptCmd> cur = CurCmd();
  if (static_cast<uint8_t>(RandNext(100)) <= cur->arg1) {
    if ((GbaCall(kAddrAiTrySpecialAction, kAddrAiIsUnitEnemyWithJid | 1u) &
         0xFFu) == 0u) {
      GbaCall(kAddrAiPerformAttack, kAddrAiIsUnitEnemyWithJid | 1u);
    }
  } else {
    *GbaPtr<uint8_t>(kAiStateAddr + 0x79u) = 4u;
  }
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
}

void AiScriptCmdAction09(GbaPtr<uint8_t> pc_ptr) {
  GbaCall(kAddrAiTrySpecialAction, kAddrAiIsUnitEnemy | 1u);
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
}

void AiScriptCmdAction0A(GbaPtr<uint8_t> pc_ptr) {
  GbaCall(kAddrAiTrySpecialAction, kAddrAiIsUnitEnemy | 1u);
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
}

void AiScriptCmdAction0B(GbaPtr<uint8_t> pc_ptr) {
  GbaCall(kAddrAiTrySpecialAction, kAddrAiIsUnitEnemy | 1u);
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
}

void AiScriptCmdMoveToCoord(GbaPtr<uint8_t> pc_ptr) {
  GbaPtr<const AiScriptCmd> cur = CurCmd();
  AiMoveTowards(static_cast<int16_t>(cur->arg1),
                static_cast<int16_t>(cur->arg3), 0u, cur->arg2, 1u);
  GbaPtr<const AiDecision> dec(kAiDecisionAddr);
  if (dec->action_performed == 1 && dec->x_move == cur->arg1 &&
      dec->y_move == cur->arg3) {
    *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
  }
}

void AiScriptCmdMoveToPid(GbaPtr<uint8_t> pc_ptr) {
  GbaPtr<const AiScriptCmd> cur = CurCmd();
  AiPos pos{};
  if (TryFindUnitByPidImpl(static_cast<int>(cur->u32_arg4), &pos)) {
    AiMoveTowards(pos.x, pos.y, 0u, cur->arg2, 1u);
    GbaPtr<AiDecision> dec(kAiDecisionAddr);
    if (AiIsWithinManhattanDistance(pos.x, pos.y, dec->x_move, dec->y_move,
                                    1u)) {
      GbaPtr<Unit> target = GetUnitByPid(static_cast<int>(cur->u32_arg4));
      if ((target->flags & kUnitFlagRescued) != 0u) {
        *GbaPtr<uint8_t>(kAiStateAddr + 0x86u) = 3u;
      } else {
        AiUpdateDecision(0u, 0u, 0u, 0u, static_cast<uint8_t>(target->id));
        *GbaPtr<uint8_t>(kAiStateAddr + 0x86u) = 2u;
        dec->action_performed = 0;
        *GbaPtr<int8_t>(kAiScriptDoneAddr) = 0;
      }
    }
  } else {
    *GbaPtr<int8_t>(kAiScriptDoneAddr) = 0;
  }
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
}

void AiScriptCmdNop0E(GbaPtr<uint8_t> pc_ptr) {
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
}

void AiScriptCmdMoveToJid(GbaPtr<uint8_t> pc_ptr) {
  GbaPtr<const AiScriptCmd> cur = CurCmd();
  GbaPtr<const Unit> active = ConstUnit(ActiveUnit());
  MapFloodRangeUnitless(active->x, active->y, GetUnitMovementCost(active));

  // Inline AiTryFindNearestUnitOfClass into host-stack AiPos.
  AiPos pos{-1, 0};
  int min_cost = 0xFF;
  GbaPtr<GbaPtr<uint8_t>> range = MapRange();
  for (int uid = 1; uid <= 0xBF; ++uid) {
    GbaPtr<Unit> unit = GetUnit(uid);
    if (!unit || !unit->pinfo || (unit->flags & 0x00010025u) != 0u) {
      continue;
    }
    const uint8_t cost = range[unit->y][unit->x];
    if (cost <= 0x78u && unit->jinfo->id == cur->u32_arg4 &&
        min_cost >= static_cast<int8_t>(cost)) {
      min_cost = cost;
      pos.x = unit->x;
      pos.y = unit->y;
    }
  }

  if (pos.x >= 0) {
    AiMoveTowards(pos.x, pos.y, 0u, cur->arg2, 1u);
  }
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
}

void AiScriptCmdLoot(GbaPtr<uint8_t> pc_ptr) {
  GbaPtr<const AiScriptCmd> cur = CurCmd();
  if (static_cast<int8_t>(GbaCall(kAddrAiTryDoorOrChestAction)) == 1) {
    if (cur->arg3 != 0u) {
      GbaPtr<Unit> active = ActiveUnit();
      active->unk_46 = static_cast<uint8_t>(active->unk_46 + 1u);
      if (active->unk_46 == cur->arg3) {
        *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
        *GbaPtr<int8_t>(kAiScriptDoneAddr) = 0;
      }
    }
    return;
  }

  AiPos pos{};
  uint8_t key_slot = 0u;
  if (!FindNearestLootableTileImpl(&pos, &key_slot)) {
    *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
    *GbaPtr<int8_t>(kAiScriptDoneAddr) = 0;
    return;
  }

  AiMoveTowards(pos.x, pos.y, 0u, 0xFFu, 1u);
  GbaPtr<const AiDecision> dec(kAiDecisionAddr);
  if (AiIsSpecialLootableTerrain(dec->x_move, dec->y_move)) {
    AiSetDecision(dec->x_move, dec->y_move, 4u, 0u, key_slot, 0u, 0u);
    if (cur->arg3 != 0u) {
      GbaPtr<Unit> active = ActiveUnit();
      active->unk_46 = static_cast<uint8_t>(active->unk_46 + 1u);
      if (active->unk_46 == cur->arg3) {
        *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
        *GbaPtr<int8_t>(kAiScriptDoneAddr) = 0;
      }
    }
  }
}

void AiScriptCmdMoveToSafe(GbaPtr<uint8_t> pc_ptr) {
  GbaPtr<const Unit> active = ConstUnit(ActiveUnit());
  uint8_t min_danger = 0xFFu;
  GbaPtr<GbaPtr<uint8_t>> mov = MapMovement();
  if ((*GbaPtr<const uint8_t>(kAiStateAddr + 0x7Bu) & 2u) != 0u) {
    MapFill(mov, -1);
    mov[active->y][active->x] = 0u;
  } else {
    MapFloodUnit(active);
  }

  AiPos pos{};
  const MapSize size = GetMapSize();
  GbaPtr<GbaPtr<uint8_t>> unit_map = MapUnit();
  GbaPtr<GbaPtr<uint8_t>> other = MapOther();
  const uint8_t active_id = ActiveUnitId();
  for (int y = size.y - 1; y >= 0; --y) {
    for (int x = size.x - 1; x >= 0; --x) {
      if (mov[y][x] > 0x78u) {
        continue;
      }
      const uint8_t uid = unit_map[y][x];
      if (uid != 0u && uid != active_id) {
        continue;
      }
      if (min_danger >= other[y][x]) {
        pos.x = static_cast<int16_t>(x);
        pos.y = static_cast<int16_t>(y);
        min_danger = other[y][x];
      }
    }
  }

  if (min_danger != 0xFFu) {
    AiSetDecision(pos.x, pos.y, 0u, 0u, 0u, 0u, 0u);
  }
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
}

void AiScriptCmdMoveToEnemy(GbaPtr<uint8_t> pc_ptr) {
  GbaPtr<const AiScriptCmd> cur = CurCmd();
  const uint32_t pred = (cur->u32_arg8 == 0u)
                            ? (kAddrAiIsUnitEnemy | 1u)
                            : (kAddrAiIsUnitEnemyNotInPidList | 1u);
  AiPos pos{};
  if (TryFindClosestUnitMatchingImpl(pred, /*no_doors=*/false, &pos)) {
    AiMoveTowards(pos.x, pos.y, 0u, cur->arg2, 1u);
  }
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
}

void AiScriptCmdMoveToEnemyNoDoors(GbaPtr<uint8_t> pc_ptr) {
  GbaPtr<const AiScriptCmd> cur = CurCmd();
  const uint32_t pred = (cur->u32_arg8 == 0u)
                            ? (kAddrAiIsUnitEnemy | 1u)
                            : (kAddrAiIsUnitEnemyNotInPidList | 1u);
  AiPos pos{};
  if (TryFindClosestUnitMatchingImpl(pred, /*no_doors=*/true, &pos)) {
    AiMoveTowardsNoDoors(pos.x, pos.y, 0u, cur->arg2, 1u);
  }
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
}

void AiScriptCmdNop14(GbaPtr<uint8_t> pc_ptr) {
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
}

void AiScriptCmdNop15(GbaPtr<uint8_t> pc_ptr) {
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
}

void AiScriptCmdMoveRandom(GbaPtr<uint8_t> pc_ptr) {
  AiTryMoveRandomly();
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
}

void AiScriptCmdEscape(GbaPtr<uint8_t> pc_ptr) {
  GbaPtr<Unit> active = ActiveUnit();
  active->ai_flags = static_cast<uint8_t>(active->ai_flags | 8u);
  GbaCall(kAddrAiPerformEscape);
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
}

uint32_t AiGetObstacleAttackPosScore(int x, int y) {
  uint32_t score =
      static_cast<uint32_t>(AiGetTerrainCombatPositionScoreComponent(x, y)) +
      static_cast<uint32_t>(AiGetFriendZonePositionScoreComponent(x, y));
  score -= static_cast<uint32_t>(static_cast<int8_t>(MapMovement()[y][x]));
  score -= static_cast<uint32_t>(MapOther()[y][x] >> 3);
  score += 0x7FFFFFFFu;
  return score;
}

bool AiFindBestObstacleAttackPos(int target_x, int target_y,
                                 GbaPtr<AiPos> out_pos,
                                 GbaPtr<uint8_t> out_slot) {
  return FindBestObstacleAttackPosImpl(target_x, target_y, out_pos.get(),
                                       out_slot.get());
}

bool AiFindNearestObstacleTerrain(GbaPtr<const uint8_t> terrain_list,
                                  uint32_t flags, GbaPtr<AiPos> out_pos) {
  return FindNearestTerrainInListImpl(terrain_list, flags, out_pos.get());
}

void AiScriptCmdAttackObstacle(GbaPtr<uint8_t> pc_ptr) {
  GbaCall(kAddrAiMapFloodObstacles, ActiveUnit().addr);
  AiPos target{};
  if (FindNearestTerrainInListImpl(
          GbaPtr<const uint8_t>(kAiObstacleTerrainsAddr), 0u, &target)) {
    AiPos atk_pos{};
    uint8_t item_slot = 0u;
    if (FindBestObstacleAttackPosImpl(target.x, target.y, &atk_pos,
                                      &item_slot)) {
      if (!GetTrapAt(target.x, target.y) &&
          !GetTrapAt(target.x, target.y + 1)) {
        return;
      }
      AiSetDecision(atk_pos.x, atk_pos.y, 1u, 0u, item_slot,
                    static_cast<uint8_t>(target.x),
                    static_cast<uint8_t>(target.y));
    } else {
      AiMoveTowards(target.x, target.y, 0u, 0xFFu, 1u);
    }
  } else {
    *GbaPtr<uint8_t>(kAiStateAddr + 0x86u) = 4u;
    *GbaPtr<int8_t>(kAiScriptDoneAddr) = 0;
  }
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
}

void AiScriptCmdMoveToTerrain(GbaPtr<uint8_t> pc_ptr) {
  GbaPtr<const Unit> active = ConstUnit(ActiveUnit());
  MapFloodRangeUnitless(active->x, active->y, GetUnitMovementCost(active));
  GbaPtr<const AiScriptCmd> cur = CurCmd();
  AiPos pos{};
  if (FindNearestTerrainInListImpl(GbaPtr<const uint8_t>(cur.addr + 3u), 0u,
                                   &pos)) {
    AiMoveTowards(pos.x, pos.y, 0u, cur->arg2, 1u);
  } else {
    *GbaPtr<uint8_t>(kAiStateAddr + 0x86u) = 4u;
    *GbaPtr<int8_t>(kAiScriptDoneAddr) = 0;
  }
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
}

void AiScriptCmdMoveToTerrainList(GbaPtr<uint8_t> pc_ptr) {
  GbaPtr<const Unit> active = ConstUnit(ActiveUnit());
  MapFloodRangeUnitless(active->x, active->y, GetUnitMovementCost(active));
  GbaPtr<const AiScriptCmd> cur = CurCmd();
  AiPos pos{};
  if (FindNearestTerrainInListImpl(GbaPtr<const uint8_t>(cur->u32_arg8), 0u,
                                   &pos)) {
    AiMoveTowards(pos.x, pos.y, 0u, cur->arg2, 1u);
  } else {
    *GbaPtr<uint8_t>(kAiStateAddr + 0x86u) = 4u;
    *GbaPtr<int8_t>(kAiScriptDoneAddr) = 0;
  }
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
}

void AiScriptCmdLabel(GbaPtr<uint8_t> pc_ptr) {
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
  *GbaPtr<int8_t>(kAiScriptDoneAddr) = 0;
}

void AiBerserkAction() {
  if ((GbaCall(kAddrAiTrySpecialAction, kAddrAiIsUnitEnemy | 1u) & 0xFFu) ==
      0u) {
    GbaCall(kAddrAiPerformAttack, kAddrAiIsUnitNotActive | 1u);
  }
}

void AiBerserkMove() {
  AiPos pos{};
  if (TryFindClosestUnitMatchingImpl(kAddrAiIsUnitNotActive | 1u,
                                     /*no_doors=*/false, &pos)) {
    AiMoveTowards(pos.x, pos.y, 0u, 0xFFu, 1u);
  }
}

bool AiReturnTrue() { return true; }

bool AiCheckJobRankAndReturnTrue(GbaPtr<const uint8_t> jid_ptr) {
  AiGetJobRank(*jid_ptr);
  return true;
}

void RegisterAiOpcodeFunctions() {
  RegisterNative(kAddrAiTryFindUnitByPid, &ThunkAiTryFindUnitByPid);
  RegisterNative(kAddrAiTryFindClosestUnitMatching,
                 &ThunkAiTryFindClosestUnitMatching);
  RegisterNative(kAddrAiTryFindClosestUnitMatchingNoDoors,
                 &ThunkAiTryFindClosestUnitMatchingNoDoors);
  RegisterNative(kAddrAiFindNearestTerrainInList,
                 &ThunkAiFindNearestTerrainInList);
  RegisterNative(kAddrAiFindNearestAdjacentToTerrainInList,
                 &ThunkAiFindNearestAdjacentToTerrainInList);
  RegisterNative(kAddrAiFindNearestDoorOrChest, &ThunkAiFindNearestDoorOrChest);
  RegisterNative(kAddrAiFindBestAdjacentPosition,
                 &ThunkAiFindBestAdjacentPosition);
  RegisterNative(kAddrAiFindNearestLootableTile,
                 &ThunkAiFindNearestLootableTile);
  RegisterNative(kAddrAiMoveTowards, &ThunkAiMoveTowards);
  RegisterNative(kAddrAiMoveTowardsNoDoors, &ThunkAiMoveTowardsNoDoors);

  RegisterNative(kAddrAiScriptCmdAttackPid, &ThunkAiScriptCmdAttackPid);
  RegisterNative(kAddrAiScriptCmdAttack, &ThunkAiScriptCmdAttack);
  RegisterNative(kAddrAiScriptCmdNop06, &ThunkAiScriptCmdNop06);
  RegisterNative(kAddrAiScriptCmdAttackInPlace, &ThunkAiScriptCmdAttackInPlace);
  RegisterNative(kAddrAiScriptCmdAttackJid, &ThunkAiScriptCmdAttackJid);
  RegisterNative(kAddrAiScriptCmdAction09, &ThunkAiScriptCmdAction09);
  RegisterNative(kAddrAiScriptCmdAction0A, &ThunkAiScriptCmdAction0A);
  RegisterNative(kAddrAiScriptCmdAction0B, &ThunkAiScriptCmdAction0B);
  RegisterNative(kAddrAiScriptCmdMoveToCoord, &ThunkAiScriptCmdMoveToCoord);
  RegisterNative(kAddrAiScriptCmdMoveToPid, &ThunkAiScriptCmdMoveToPid);
  RegisterNative(kAddrAiScriptCmdNop0E, &ThunkAiScriptCmdNop0E);
  RegisterNative(kAddrAiScriptCmdMoveToJid, &ThunkAiScriptCmdMoveToJid);
  RegisterNative(kAddrAiScriptCmdLoot, &ThunkAiScriptCmdLoot);
  RegisterNative(kAddrAiScriptCmdMoveToSafe, &ThunkAiScriptCmdMoveToSafe);
  RegisterNative(kAddrAiScriptCmdMoveToEnemy, &ThunkAiScriptCmdMoveToEnemy);
  RegisterNative(kAddrAiScriptCmdMoveToEnemyNoDoors,
                 &ThunkAiScriptCmdMoveToEnemyNoDoors);
  RegisterNative(kAddrAiScriptCmdNop14, &ThunkAiScriptCmdNop14);
  RegisterNative(kAddrAiScriptCmdNop15, &ThunkAiScriptCmdNop15);
  RegisterNative(kAddrAiScriptCmdMoveRandom, &ThunkAiScriptCmdMoveRandom);
  RegisterNative(kAddrAiScriptCmdEscape, &ThunkAiScriptCmdEscape);
  RegisterNative(kAddrAiGetObstacleAttackPosScore,
                 &ThunkAiGetObstacleAttackPosScore);
  RegisterNative(kAddrAiFindBestObstacleAttackPos,
                 &ThunkAiFindBestObstacleAttackPos);
  RegisterNative(kAddrAiFindNearestObstacleTerrain,
                 &ThunkAiFindNearestObstacleTerrain);
  RegisterNative(kAddrAiScriptCmdAttackObstacle,
                 &ThunkAiScriptCmdAttackObstacle);
  RegisterNative(kAddrAiScriptCmdMoveToTerrain, &ThunkAiScriptCmdMoveToTerrain);
  RegisterNative(kAddrAiScriptCmdMoveToTerrainList,
                 &ThunkAiScriptCmdMoveToTerrainList);
  RegisterNative(kAddrAiScriptCmdLabel, &ThunkAiScriptCmdLabel);
  RegisterNative(kAddrAiBerserkAction, &ThunkAiBerserkAction);
  RegisterNative(kAddrAiBerserkMove, &ThunkAiBerserkMove);
  RegisterNative(kAddrAiReturnTrue, &ThunkAiReturnTrue);
  RegisterNative(kAddrAiCheckJobRankAndReturnTrue,
                 &ThunkAiCheckJobRankAndReturnTrue);
}

}  // namespace fe7
