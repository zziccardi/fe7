#include "game/ai/ai_staff_loot.h"

#include <cstdint>

#include "absl/base/nullability.h"
#include "game/ai/ai.h"
#include "game/ai/ai_actions.h"
#include "game/ai/ai_opcodes.h"
#include "game/ai/ai_script.h"
#include "game/item/item.h"
#include "game/map/map.h"
#include "game/map/map_range.h"
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

inline GbaPtr<uint8_t> AiStateBytes() { return GbaPtr<uint8_t>(kAiStateAddr); }

inline GbaPtr<uint8_t> AiDecisionBytes() {
  return GbaPtr<uint8_t>(kAiDecisionAddr);
}

inline GbaPtr<GbaPtr<uint8_t>> MapRows(uint32_t ptr_addr) {
  return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(ptr_addr);
}

inline uint8_t MapAt(uint32_t ptr_addr, int x, int y) {
  return MapRows(ptr_addr)[y][x];
}

inline uint32_t UnitAttributes(GbaPtr<const Unit> unit) {
  return unit->pinfo->attributes | unit->jinfo->attributes;
}

// Stack-free mirror of AiFindBestAdjacentStaffPos (0x0803AB80) so staff AI
// functions never need GBA stack scratch space.
bool FindBestAdjacentStaffPosLocal(int tx, int ty, AiPos* out) {
  static constexpr int8_t kDirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  const GbaPtr<const MapSize> sz(kMapSizeAddr);
  const uint8_t active_uid = *GbaPtr<const uint8_t>(kAiActiveUnitIdAddr);
  uint8_t best_danger = 0xFFu;
  int best_x = 0;
  int best_y = 0;

  for (int i = 0; i < 4; ++i) {
    const int x = tx + kDirs[i][0];
    const int y = ty + kDirs[i][1];
    if (x < 0 || x >= sz->x || y < 0 || y >= sz->y) {
      continue;
    }
    if (MapAt(kMapMovementAddr, x, y) > 0x78u) {
      continue;
    }
    const uint8_t uid = MapAt(kMapUnitAddr, x, y);
    if (uid != 0u && uid != active_uid) {
      continue;
    }
    const uint8_t danger = MapAt(kMapOtherAddr, x, y);
    if (danger < best_danger) {
      best_danger = danger;
      best_x = x;
      best_y = y;
    }
  }
  if (best_danger == 0xFFu) {
    return false;
  }
  out->x = static_cast<int16_t>(best_x);
  out->y = static_cast<int16_t>(best_y);
  return true;
}

// Stack-free mirror of AiFindBestPositionInRange (0x08037380).
bool FindBestPosInRangeLocal(AiPos* out) {
  const GbaPtr<const MapSize> sz(kMapSizeAddr);
  const uint8_t active_uid = *GbaPtr<const uint8_t>(kAiActiveUnitIdAddr);
  uint8_t best_danger = 0xFFu;
  int best_x = 0;
  int best_y = 0;

  for (int y = sz->y - 1; y >= 0; --y) {
    for (int x = sz->x - 1; x >= 0; --x) {
      if (MapAt(kMapMovementAddr, x, y) > 0x78u) {
        continue;
      }
      if (MapAt(kMapRangeAddr, x, y) == 0u) {
        continue;
      }
      const uint8_t uid = MapAt(kMapUnitAddr, x, y);
      if (uid != 0u && uid != active_uid) {
        continue;
      }
      const uint8_t danger = MapAt(kMapOtherAddr, x, y);
      if (danger < best_danger) {
        best_danger = danger;
        best_x = x;
        best_y = y;
      }
    }
  }
  if (best_danger == 0xFFu) {
    return false;
  }
  out->x = static_cast<int16_t>(best_x);
  out->y = static_cast<int16_t>(best_y);
  return true;
}

// Stack-free helper for AiFindBestWarpDestination (0x0803B83C).
bool FindBestWarpDestLocal(AiPos* out) {
  int best_enemy_dist = 0xFF;
  int best_x = 0;
  int best_y = 0;
  int cur_x = 0;
  int cur_y = 0;

  const GbaPtr<const int8_t> mov_table(
      GbaCall(kAddrGetUnitMovementCost, ActiveUnit().addr));
  const int faction = GetCurrentPhase();
  const GbaPtr<const MapSize> sz(kMapSizeAddr);

  for (int uid = faction + 1; uid < faction + 0x80; ++uid) {
    GbaPtr<Unit> unit = GetUnit(uid);
    if (!unit || !unit->pinfo) {
      continue;
    }
    if ((unit->flags & 0x00010025u) != 0u) {
      continue;
    }

    AiMapFloodRangeUnitMov(unit);

    int enemy_dist = 0xFF;
    int warp_dist = 0xFF;
    for (int y = sz->y - 1; y >= 0; --y) {
      for (int x = sz->x - 1; x >= 0; --x) {
        const uint8_t rval = MapAt(kMapRangeAddr, x, y);
        if (rval > 0x78u) {
          continue;
        }
        const uint8_t occ = MapAt(kMapUnitAddr, x, y);
        if (occ != 0u) {
          if (!AreUnitIdsAllied(unit->id, occ)) {
            const int sr = static_cast<int8_t>(rval);
            if (enemy_dist > sr) {
              enemy_dist = rval;
            }
          }
          continue;
        }
        const uint8_t terrain = MapAt(kMapTerrainAddr, x, y);
        if (mov_table[terrain] <= 0) {
          continue;
        }
        if (static_cast<int8_t>(MapAt(kMapMovementAddr, x, y)) <= 0x78) {
          continue;
        }
        const int sr = static_cast<int8_t>(rval);
        if (warp_dist > sr) {
          cur_x = x;
          cur_y = y;
          warp_dist = rval;
        }
      }
    }

    if (enemy_dist != 0xFF &&
        static_cast<uint32_t>(best_enemy_dist) >=
            static_cast<uint32_t>(enemy_dist) &&
        warp_dist != 0xFF) {
      best_x = cur_x;
      best_y = cur_y;
      best_enemy_dist = enemy_dist;
    }
  }

  if (best_enemy_dist == 0xFF) {
    return false;
  }
  out->x = static_cast<int16_t>(best_x);
  out->y = static_cast<int16_t>(best_y);
  return true;
}

// ---------------------------------------------------------------------------
// Native dispatch thunks
// ---------------------------------------------------------------------------

uint32_t ThunkAiTryWarpStaff(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                             uint32_t /*r3*/,
                             const uint32_t* absl_nullable /*stack*/) {
  AiTryWarpStaff(static_cast<int32_t>(r0), r1);
  return 0u;
}

uint32_t ThunkAiTryRestoreStaff(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                                uint32_t /*r3*/,
                                const uint32_t* absl_nullable /*stack*/) {
  AiTryRestoreStaff(static_cast<int32_t>(r0), r1);
  return 0u;
}

uint32_t ThunkAiUnitHasUsableWeaponOrStaff(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  return AiUnitHasUsableWeaponOrStaff(GbaPtr<const Unit>(r0)) ? 1u : 0u;
}

uint32_t ThunkAiGetStatusStaffTargetScore(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(
      AiGetStatusStaffTargetScore(GbaPtr<const Unit>(r0)));
}

uint32_t ThunkAiTrySilenceStaff(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                                uint32_t /*r3*/,
                                const uint32_t* absl_nullable /*stack*/) {
  AiTrySilenceStaff(static_cast<int32_t>(r0), r1);
  return 0u;
}

uint32_t ThunkAiTrySleepOrBerserkStaff(
    uint32_t r0, uint32_t r1, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  AiTrySleepOrBerserkStaff(static_cast<int32_t>(r0), r1);
  return 0u;
}

uint32_t ThunkAiTryBarrierStaff(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                                uint32_t /*r3*/,
                                const uint32_t* absl_nullable /*stack*/) {
  AiTryBarrierStaff(static_cast<int32_t>(r0), r1);
  return 0u;
}

uint32_t ThunkAiFindBestWarpDestination(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  return AiFindBestWarpDestination(GbaPtr<AiPos>(r0)) ? 1u : 0u;
}

uint32_t ThunkAiGetSpecialItemTableIndex(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(
      AiGetSpecialItemTableIndex(static_cast<uint16_t>(r0)));
}

uint32_t ThunkAiTryDoorOrChestAction(uint32_t /*r0*/, uint32_t /*r1*/,
                                     uint32_t /*r2*/, uint32_t /*r3*/,
                                     const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(AiTryDoorOrChestAction());
}

uint32_t ThunkAiTryUseDoorKey(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                              uint32_t /*r3*/,
                              const uint32_t* absl_nullable /*stack*/) {
  AiTryUseDoorKey(static_cast<int32_t>(r0));
  return 0u;
}

uint32_t ThunkAiTryUseChestKeyOrLockpick(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  AiTryUseChestKeyOrLockpick(static_cast<int32_t>(r0));
  return 0u;
}

uint32_t ThunkAiTryUseAntitoxin(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                                uint32_t /*r3*/,
                                const uint32_t* absl_nullable /*stack*/) {
  AiTryUseAntitoxin(static_cast<int32_t>(r0));
  return 0u;
}

uint32_t ThunkAiGetRangeCostIfPassable(
    uint32_t r0, uint32_t r1, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  return AiGetRangeCostIfPassable(static_cast<int32_t>(r0),
                                  static_cast<int32_t>(r1));
}

uint32_t ThunkAiFindReachableDoor(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                                  uint32_t /*r3*/,
                                  const uint32_t* absl_nullable /*stack*/) {
  return AiFindReachableDoor(GbaPtr<Unit>(r0), GbaPtr<AiPos>(r1)) ? 1u : 0u;
}

uint32_t ThunkAiFindReachableChest(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                                   uint32_t /*r3*/,
                                   const uint32_t* absl_nullable /*stack*/) {
  return AiFindReachableChest(GbaPtr<Unit>(r0), GbaPtr<AiPos>(r1)) ? 1u : 0u;
}

uint32_t ThunkAiFindReachableDoorOrChest(
    uint32_t r0, uint32_t r1, uint32_t r2, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  return AiFindReachableDoorOrChest(GbaPtr<Unit>(r0), static_cast<int32_t>(r1),
                                    GbaPtr<AiPos>(r2))
             ? 1u
             : 0u;
}

uint32_t ThunkAiSetMovTableAllPassable(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  AiSetMovTableAllPassable(GbaPtr<const int8_t>(r0));
  return 0u;
}

uint32_t ThunkAiSetMovTableAllowOneTerrain(
    uint32_t r0, uint32_t r1, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  AiSetMovTableAllowOneTerrain(GbaPtr<const int8_t>(r0),
                               static_cast<int32_t>(r1));
  return 0u;
}

uint32_t ThunkAiSetMovTableAllowTwoTerrains(
    uint32_t r0, uint32_t r1, uint32_t r2, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  AiSetMovTableAllowTwoTerrains(GbaPtr<const int8_t>(r0),
                                static_cast<int32_t>(r1),
                                static_cast<int32_t>(r2));
  return 0u;
}

uint32_t ThunkAiMapFloodUnitExtended(uint32_t r0, uint32_t /*r1*/,
                                     uint32_t /*r2*/, uint32_t /*r3*/,
                                     const uint32_t* absl_nullable /*stack*/) {
  AiMapFloodUnitExtended(GbaPtr<Unit>(r0));
  return 0u;
}

uint32_t ThunkAiMapFloodRangeExtended(uint32_t r0, uint32_t /*r1*/,
                                      uint32_t /*r2*/, uint32_t /*r3*/,
                                      const uint32_t* absl_nullable /*stack*/) {
  AiMapFloodRangeExtended(GbaPtr<Unit>(r0));
  return 0u;
}

uint32_t ThunkAiMapFloodUnitAllPassable(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  AiMapFloodUnitAllPassable(GbaPtr<Unit>(r0));
  return 0u;
}

uint32_t ThunkAiMapFloodUnitAllPassableNoUnit(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  AiMapFloodUnitAllPassableNoUnit(GbaPtr<Unit>(r0));
  return 0u;
}

uint32_t ThunkAiMapFloodNoDoors(uint32_t r0, uint32_t r1, uint32_t r2,
                                uint32_t /*r3*/,
                                const uint32_t* absl_nullable /*stack*/) {
  AiMapFloodNoDoors(static_cast<int32_t>(r0), static_cast<int32_t>(r1),
                    GbaPtr<const int8_t>(r2));
  return 0u;
}

uint32_t ThunkAiMapFloodNoDoorsUnit(uint32_t r0, uint32_t r1, uint32_t r2,
                                    uint32_t /*r3*/,
                                    const uint32_t* absl_nullable /*stack*/) {
  AiMapFloodNoDoorsUnit(static_cast<int32_t>(r0), static_cast<int32_t>(r1),
                        GbaPtr<Unit>(r2));
  return 0u;
}

uint32_t ThunkAiMapFloodRangeAllowDoor(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  AiMapFloodRangeAllowDoor(GbaPtr<Unit>(r0));
  return 0u;
}

uint32_t ThunkAiMapFloodRangeAllowDoorNoUnit(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  AiMapFloodRangeAllowDoorNoUnit(GbaPtr<Unit>(r0));
  return 0u;
}

uint32_t ThunkAiMapFloodRangeAllowObstacles(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  AiMapFloodRangeAllowObstacles(GbaPtr<Unit>(r0));
  return 0u;
}

uint32_t ThunkAiMapFloodObstacles(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                                  uint32_t /*r3*/,
                                  const uint32_t* absl_nullable /*stack*/) {
  AiMapFloodObstacles(GbaPtr<Unit>(r0));
  return 0u;
}

uint32_t ThunkAiMapFloodRangeUnitMov(uint32_t r0, uint32_t /*r1*/,
                                     uint32_t /*r2*/, uint32_t /*r3*/,
                                     const uint32_t* absl_nullable /*stack*/) {
  AiMapFloodRangeUnitMov(GbaPtr<Unit>(r0));
  return 0u;
}

uint32_t ThunkAiUpdateIgnoreSpecialItemsFlag(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  AiUpdateIgnoreSpecialItemsFlag(GbaPtr<const Unit>(r0));
  return 0u;
}

uint32_t ThunkAiMapFloodRangeWithUnit(uint32_t r0, uint32_t r1, uint32_t r2,
                                      uint32_t /*r3*/,
                                      const uint32_t* absl_nullable /*stack*/) {
  AiMapFloodRangeWithUnit(static_cast<int32_t>(r0), static_cast<int32_t>(r1),
                          GbaPtr<Unit>(r2));
  return 0u;
}

}  // namespace

// ---------------------------------------------------------------------------
// 0x0803B0A4: AiTryWarpStaff
// ---------------------------------------------------------------------------
void AiTryWarpStaff(int slot, uint32_t target_filter_fn) {
  int best_level = 0;
  int best_x = 0;
  int best_y = 0;
  int best_uid = 0;

  if ((AiStateBytes()[0x7B] & 4u) != 0u) {
    return;
  }

  AiFloodMovementForUnit(GbaPtr<const Unit>(ActiveUnit().addr));
  MapMarkMagicSealRange(-1);
  MapFloodRangeExtend();

  const GbaPtr<const MapSize> sz(kMapSizeAddr);
  for (int y = sz->y - 1; y >= 0; --y) {
    for (int x = sz->x - 1; x >= 0; --x) {
      if (MapAt(kMapMovementAddr, x, y) > 0x78u) {
        continue;
      }
      const uint8_t uid = MapAt(kMapUnitAddr, x, y);
      if (uid == 0u) {
        continue;
      }
      GbaPtr<Unit> unit = GetUnit(uid);
      if ((AiStateBytes()[0x7B] & 4u) == 0u && target_filter_fn != 0u) {
        if (static_cast<int8_t>(GbaCall(target_filter_fn, unit.addr) & 0xFFu) ==
            1) {
          continue;
        }
      }
      if ((unit->ai_flags & 9u) != 0u) {
        continue;
      }
      if (unit->level < best_level) {
        continue;
      }
      AiPos pos{};
      if (!FindBestAdjacentStaffPosLocal(x, y, &pos)) {
        continue;
      }
      best_level = static_cast<uint8_t>(unit->level);
      best_x = pos.x;
      best_y = pos.y;
      best_uid = uid;
    }
  }

  if (best_level != 0) {
    AiPos warp_dest{};
    if (FindBestWarpDestLocal(&warp_dest)) {
      AiSetDecision(static_cast<int16_t>(best_x), static_cast<int16_t>(best_y),
                    5, static_cast<uint8_t>(best_uid),
                    static_cast<uint8_t>(slot & 0xFF),
                    static_cast<uint8_t>(warp_dest.x & 0xFF),
                    static_cast<uint8_t>(warp_dest.y & 0xFF));
    }
  }
}

// ---------------------------------------------------------------------------
// 0x0803B1FC: AiTryRestoreStaff
// ---------------------------------------------------------------------------
void AiTryRestoreStaff(int slot, uint32_t target_filter_fn) {
  int best_level = 0;
  int best_x = 0;
  int best_y = 0;
  int best_uid = 0;

  if ((AiStateBytes()[0x7B] & 4u) != 0u) {
    return;
  }

  AiFloodMovementForUnit(GbaPtr<const Unit>(ActiveUnit().addr));
  MapMarkMagicSealRange(-1);
  MapFloodRangeExtend();

  const GbaPtr<const MapSize> sz(kMapSizeAddr);
  for (int y = sz->y - 1; y >= 0; --y) {
    for (int x = sz->x - 1; x >= 0; --x) {
      if (MapAt(kMapMovementAddr, x, y) > 0x78u) {
        continue;
      }
      const uint8_t uid = MapAt(kMapUnitAddr, x, y);
      if (uid == 0u) {
        continue;
      }
      GbaPtr<Unit> unit = GetUnit(uid);
      if ((AiStateBytes()[0x7B] & 4u) == 0u && target_filter_fn != 0u) {
        if (static_cast<int8_t>(GbaCall(target_filter_fn, unit.addr) & 0xFFu) ==
            1) {
          continue;
        }
      }
      if (unit->status == 0u) {
        continue;
      }
      if (unit->level < best_level) {
        continue;
      }
      AiPos pos{};
      if (!FindBestAdjacentStaffPosLocal(x, y, &pos)) {
        continue;
      }
      best_level = static_cast<uint8_t>(unit->level);
      best_x = pos.x;
      best_y = pos.y;
      best_uid = uid;
    }
  }

  if (best_level != 0) {
    AiSetDecision(static_cast<int16_t>(best_x), static_cast<int16_t>(best_y), 5,
                  static_cast<uint8_t>(best_uid),
                  static_cast<uint8_t>(slot & 0xFF), 0, 0);
  }
}

// ---------------------------------------------------------------------------
// 0x0803B340: AiUnitHasUsableWeaponOrStaff
// ---------------------------------------------------------------------------
bool AiUnitHasUsableWeaponOrStaff(GbaPtr<const Unit> unit) {
  for (int i = 0; i < kUnitItemCount; ++i) {
    const uint16_t item = unit->items[i];
    if (item == 0u) {
      break;
    }
    if ((GetItemAttributes(item) & 0x00000405u) != 0u) {
      if (CanUnitUseWeapon(unit, item) || CanUnitUseStaff(unit, item)) {
        return true;
      }
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// 0x0803B398: AiGetStatusStaffTargetScore
// ---------------------------------------------------------------------------
int AiGetStatusStaffTargetScore(GbaPtr<const Unit> target) {
  uint8_t acc = static_cast<uint8_t>(
      GbaCall(kAddrGetOffensiveStaffAccuracy, ActiveUnit().addr, target.addr) &
      0xFFu);
  if (acc <= 4u) {
    return 0;
  }
  uint8_t score = static_cast<uint8_t>(acc + GetUnitPower(target));
  const uint16_t weapon = static_cast<uint16_t>(GetUnitEquippedWeapon(target));
  if (weapon != 0u && (GetItemAttributes(weapon) & 2u) != 0u) {
    score = static_cast<uint8_t>(score << 1);
  }
  return score;
}

// ---------------------------------------------------------------------------
// 0x0803B3EC: AiTrySilenceStaff
// ---------------------------------------------------------------------------
void AiTrySilenceStaff(int slot, uint32_t target_filter_fn) {
  uint32_t best_score = 0u;
  int best_x = 0;
  int best_y = 0;
  int best_uid = 0;

  if ((AiStateBytes()[0x7B] & 4u) != 0u) {
    return;
  }

  AiFloodMovementForUnit(GbaPtr<const Unit>(ActiveUnit().addr));
  MapMarkMagicSealRange(-1);

  for (int uid = 1; uid <= 0xBF; ++uid) {
    GbaPtr<Unit> unit = GetUnit(uid);
    if (!unit || !unit->pinfo) {
      continue;
    }
    if ((unit->flags & 0x00010005u) != 0u) {
      continue;
    }
    if ((AiStateBytes()[0x7B] & 4u) == 0u && target_filter_fn != 0u) {
      if ((GbaCall(target_filter_fn, unit.addr) & 0xFFu) == 0u) {
        continue;
      }
    }
    if (unit->status == 3u) {
      continue;
    }
    if (!AiUnitHasUsableStaffOrMagic(GbaPtr<const Unit>(unit.addr))) {
      continue;
    }

    const GbaPtr<Unit> active = ActiveUnit();
    const int max_dist = (GetUnitMagRange(GbaPtr<const Unit>(active.addr)) +
                          static_cast<uint8_t>(active->bonus_mov) +
                          static_cast<uint8_t>(active->jinfo->base_mov)) &
                         0xFF;
    if (!AiIsWithinManhattanDistance(
            active->x, active->y, static_cast<uint8_t>(unit->x),
            static_cast<uint8_t>(unit->y), static_cast<uint8_t>(max_dist))) {
      continue;
    }
    if (!AiUnitHasUsableWeaponOrStaff(GbaPtr<const Unit>(unit.addr))) {
      continue;
    }
    const uint32_t score = static_cast<uint8_t>(
        AiGetStatusStaffTargetScore(GbaPtr<const Unit>(unit.addr)));
    if (score == 0u || score < best_score) {
      continue;
    }

    MapFill(MapRows(kMapRangeAddr), 0);
    MapAddInRange(unit->x, unit->y,
                  GetUnitMagRange(GbaPtr<const Unit>(active.addr)), 1);

    AiPos pos{};
    if (!FindBestPosInRangeLocal(&pos)) {
      continue;
    }
    best_score = score;
    best_x = pos.x;
    best_y = pos.y;
    best_uid = unit->id;
  }

  if (best_score != 0u) {
    AiSetDecision(static_cast<int16_t>(best_x), static_cast<int16_t>(best_y), 5,
                  static_cast<uint8_t>(best_uid & 0xFF),
                  static_cast<uint8_t>(slot & 0xFF), 0, 0);
  }
}

// ---------------------------------------------------------------------------
// 0x0803B578: AiTrySleepOrBerserkStaff
// ---------------------------------------------------------------------------
void AiTrySleepOrBerserkStaff(int slot, uint32_t target_filter_fn) {
  int best_level = 0;
  int best_x = 0;
  int best_y = 0;
  int best_uid = 0;

  if ((AiStateBytes()[0x7B] & 4u) != 0u) {
    return;
  }

  AiFloodMovementForUnit(GbaPtr<const Unit>(ActiveUnit().addr));
  MapMarkMagicSealRange(-1);

  for (int uid = 1; uid <= 0xBF; ++uid) {
    GbaPtr<Unit> unit = GetUnit(uid);
    if (!unit || !unit->pinfo) {
      continue;
    }
    if ((unit->flags & 0x00010005u) != 0u) {
      continue;
    }
    if ((AiStateBytes()[0x7B] & 4u) == 0u && target_filter_fn != 0u) {
      if ((GbaCall(target_filter_fn, unit.addr) & 0xFFu) == 0u) {
        continue;
      }
    }
    if (unit->status != 0u) {
      continue;
    }

    const GbaPtr<Unit> active = ActiveUnit();
    const int max_dist = (GetUnitMagRange(GbaPtr<const Unit>(active.addr)) +
                          static_cast<uint8_t>(active->bonus_mov) +
                          static_cast<uint8_t>(active->jinfo->base_mov)) &
                         0xFF;
    if (!AiIsWithinManhattanDistance(
            active->x, active->y, static_cast<uint8_t>(unit->x),
            static_cast<uint8_t>(unit->y), static_cast<uint8_t>(max_dist))) {
      continue;
    }
    if (!AiUnitHasUsableWeaponOrStaff(GbaPtr<const Unit>(unit.addr))) {
      continue;
    }

    const uint8_t acc = static_cast<uint8_t>(
        GbaCall(kAddrGetOffensiveStaffAccuracy, active.addr, unit.addr) &
        0xFFu);
    if (acc <= 4u) {
      continue;
    }
    if (static_cast<int>(acc) + static_cast<int>(unit->level) < best_level) {
      continue;
    }

    MapFill(MapRows(kMapRangeAddr), 0);
    MapAddInRange(unit->x, unit->y,
                  GetUnitMagRange(GbaPtr<const Unit>(active.addr)), 1);

    AiPos pos{};
    if (!FindBestPosInRangeLocal(&pos)) {
      continue;
    }
    best_level = static_cast<uint8_t>(unit->level);
    best_x = pos.x;
    best_y = pos.y;
    best_uid = unit->id;
  }

  if (best_level != 0) {
    AiSetDecision(static_cast<int16_t>(best_x), static_cast<int16_t>(best_y), 5,
                  static_cast<uint8_t>(best_uid & 0xFF),
                  static_cast<uint8_t>(slot & 0xFF), 0, 0);
  }
}

// ---------------------------------------------------------------------------
// 0x0803B6FC: AiTryBarrierStaff
// ---------------------------------------------------------------------------
void AiTryBarrierStaff(int slot, uint32_t target_filter_fn) {
  int best_res = 0xFF;
  int best_x = 0;
  int best_y = 0;
  int best_uid = 0;

  if ((AiStateBytes()[0x7B] & 4u) != 0u) {
    return;
  }

  AiFloodMovementForUnit(GbaPtr<const Unit>(ActiveUnit().addr));
  MapMarkMagicSealRange(-1);
  MapFloodRangeExtend();

  const GbaPtr<const MapSize> sz(kMapSizeAddr);
  for (int y = sz->y - 1; y >= 0; --y) {
    for (int x = sz->x - 1; x >= 0; --x) {
      if (MapAt(kMapMovementAddr, x, y) > 0x78u) {
        continue;
      }
      const uint8_t uid = MapAt(kMapUnitAddr, x, y);
      if (uid == 0u) {
        continue;
      }
      GbaPtr<Unit> unit = GetUnit(uid);
      if ((AiStateBytes()[0x7B] & 4u) == 0u && target_filter_fn != 0u) {
        if (static_cast<int8_t>(GbaCall(target_filter_fn, unit.addr) & 0xFFu) ==
            1) {
          continue;
        }
      }
      if (GetUnitResistance(GbaPtr<const Unit>(unit.addr)) > best_res) {
        continue;
      }
      AiPos pos{};
      if (!FindBestAdjacentStaffPosLocal(x, y, &pos)) {
        continue;
      }
      best_res = GetUnitResistance(GbaPtr<const Unit>(unit.addr)) & 0xFF;
      best_x = pos.x;
      best_y = pos.y;
      best_uid = uid;
    }
  }

  if (best_res != 0xFF) {
    AiSetDecision(static_cast<int16_t>(best_x), static_cast<int16_t>(best_y), 5,
                  static_cast<uint8_t>(best_uid),
                  static_cast<uint8_t>(slot & 0xFF), 0, 0);
  }
}

// ---------------------------------------------------------------------------
// 0x0803B83C: AiFindBestWarpDestination
// ---------------------------------------------------------------------------
bool AiFindBestWarpDestination(GbaPtr<AiPos> out_pos) {
  AiPos res{};
  if (!FindBestWarpDestLocal(&res)) {
    return false;
  }
  out_pos->x = res.x;
  out_pos->y = res.y;
  return true;
}

// ---------------------------------------------------------------------------
// 0x0803B9C4: AiGetSpecialItemTableIndex
// ---------------------------------------------------------------------------
int AiGetSpecialItemTableIndex(uint16_t item) {
  const uint16_t iid = static_cast<uint16_t>(GetItemIid(item));
  for (int i = 0;; ++i) {
    const uint32_t entry_addr =
        kAiSpecialItemTableAddr + static_cast<uint32_t>(i * 8);
    const uint16_t entry_iid = *GbaPtr<const uint16_t>(entry_addr);
    if (entry_iid == 0u) {
      return -1;
    }
    if (entry_iid == iid && *GbaPtr<const uint32_t>(entry_addr + 4u) != 0u) {
      return i;
    }
  }
}

// ---------------------------------------------------------------------------
// 0x0803BA18: AiTryDoorOrChestAction
// ---------------------------------------------------------------------------
int AiTryDoorOrChestAction() {
  if ((AiStateBytes()[0x7B] & 2u) != 0u) {
    return 0;
  }

  for (int slot = 0; slot < kUnitItemCount; ++slot) {
    const uint16_t item = ActiveUnit()->items[slot];
    if (item == 0u) {
      break;
    }
    if (GetItemKind(item) != 0) {
      const int idx = AiGetSpecialItemTableIndex(item);
      if (idx != -1) {
        const uint32_t fn = *GbaPtr<const uint32_t>(
            kAiSpecialItemTableAddr + 4u + static_cast<uint32_t>(idx * 8));
        GbaCall(fn, static_cast<uint32_t>(slot));
      }
    }
  }

  if (AiStateBytes()[0x79] != 0u) {
    return static_cast<int8_t>(AiDecisionBytes()[0x0A]);
  }
  return 1;
}

// ---------------------------------------------------------------------------
// 0x0803BAA8: AiTryUseDoorKey
// ---------------------------------------------------------------------------
void AiTryUseDoorKey(int slot) {
  if ((*GbaPtr<const uint32_t>(kAiStateAddr + 0x80u) & 0x80000001u) == 0u) {
    return;
  }
  const GbaPtr<Unit> active = ActiveUnit();
  AiMapFloodRangeAllowDoorNoUnit(active);
  AiPos pos{};
  if (!AiFindNearestAdjacentToTerrainInListOut(
          GbaPtr<const uint8_t>(kAiDoorTerrainListAddr), 0u, &pos)) {
    return;
  }
  AiMapFloodRangeExtended(active);
  if (static_cast<int8_t>(MapAt(kMapRangeAddr, pos.x, pos.y)) > 0x77) {
    return;
  }

  AiMoveTowards(pos.x, pos.y, 0u, AiStateBytes()[0x7E], 1u);
  if (AiDecisionBytes()[0x0A] == 1u) {
    if (AiIsWithinManhattanDistance(pos.x, pos.y, AiDecisionBytes()[2],
                                    AiDecisionBytes()[3], 0)) {
      AiSetDecision(AiDecisionBytes()[2], AiDecisionBytes()[3], 6, 0,
                    slot & 0xFF, 0, 0);
    }
  }
}

// ---------------------------------------------------------------------------
// 0x0803BB40: AiTryUseChestKeyOrLockpick
// ---------------------------------------------------------------------------
void AiTryUseChestKeyOrLockpick(int slot) {
  int flags = 0;
  if ((*GbaPtr<const uint32_t>(kAiStateAddr + 0x80u) & 2u) == 0u) {
    return;
  }

  const GbaPtr<Unit> active = ActiveUnit();
  if (GetUnitItemCount(GbaPtr<const Unit>(active.addr)) > 4 &&
      (active->ai_flags & 8u) == 0u) {
    active->ai_flags |= 8u;
    AiStateBytes()[0x79] = 0u;
    return;
  }

  if ((UnitAttributes(GbaPtr<const Unit>(active.addr)) & 4u) == 0u) {
    return;
  }
  if (GetUnitItemCount(GbaPtr<const Unit>(active.addr)) > 4) {
    flags |= 4;
  }

  AiPos pos{};
  bool found = false;
  AiMapFloodUnitExtended(active);
  AiMapFloodRangeAllowDoor(active);
  if (AiFindNearestDoorOrChestOut(static_cast<uint32_t>(flags | 1), &pos) &&
      static_cast<int8_t>(MapAt(kMapMovementAddr, pos.x, pos.y)) <= 0x77) {
    found = true;
  } else {
    AiMapFloodRangeAllowDoorNoUnit(active);
    if (AiFindNearestDoorOrChestOut(static_cast<uint32_t>(flags), &pos) &&
        (static_cast<int8_t>(MapAt(kMapMovementAddr, pos.x, pos.y)) > 0x77 ||
         MapAt(kMapUnitAddr, pos.x, pos.y) != 0u)) {
      found = true;
    }
  }
  if (!found) {
    return;
  }

  AiMoveTowards(pos.x, pos.y, 0u, AiStateBytes()[0x7E], 0u);
  if (AiDecisionBytes()[0x0A] == 1u) {
    if (AiIsWithinManhattanDistance(pos.x, pos.y, AiDecisionBytes()[2],
                                    AiDecisionBytes()[3], 0)) {
      AiSetDecision(AiDecisionBytes()[2], AiDecisionBytes()[3], 6, 0,
                    slot & 0xFF, 0, 0);
    }
  }
}

// ---------------------------------------------------------------------------
// 0x0803BC28: AiTryUseAntitoxin
// ---------------------------------------------------------------------------
void AiTryUseAntitoxin(int slot) {
  if ((*GbaPtr<const uint32_t>(kAiStateAddr + 0x80u) & 4u) == 0u) {
    return;
  }
  const GbaPtr<Unit> active = ActiveUnit();
  if (active->status != 1u) {
    return;
  }

  // Mirror AiFindSafestReachableLocation (0x08036900) without GBA stack AiPos.
  if ((AiStateBytes()[0x7B] & 2u) != 0u) {
    MapFill(MapRows(kMapMovementAddr), -1);
    MapRows(kMapMovementAddr)[active->y][active->x] = 0u;
  } else {
    MapFloodUnit(GbaPtr<const Unit>(active.addr));
  }
  const GbaPtr<const MapSize> sz(kMapSizeAddr);
  const uint8_t active_uid = *GbaPtr<const uint8_t>(kAiActiveUnitIdAddr);
  int best_danger = 0xFF;
  int best_x = 0;
  int best_y = 0;
  for (int y = sz->y - 1; y >= 0; --y) {
    for (int x = sz->x - 1; x >= 0; --x) {
      if (MapAt(kMapMovementAddr, x, y) > 0x78u) {
        continue;
      }
      const uint8_t u = MapAt(kMapUnitAddr, x, y);
      if (u != 0u && u != active_uid) {
        continue;
      }
      const int d = MapAt(kMapOtherAddr, x, y);
      if (best_danger >= d) {
        best_danger = d;
        best_x = x;
        best_y = y;
      }
    }
  }
  if (best_danger == 0xFF) {
    return;
  }
  AiSetDecision(static_cast<int16_t>(best_x), static_cast<int16_t>(best_y), 6,
                0, static_cast<uint8_t>(slot & 0xFF), 0, 0);
}

// ---------------------------------------------------------------------------
// 0x0803BC90: AiGetRangeCostIfPassable
// ---------------------------------------------------------------------------
uint8_t AiGetRangeCostIfPassable(int x, int y) {
  const uint8_t cost = MapAt(kMapRangeAddr, x, y);
  if (static_cast<int8_t>(cost) > 0x77) {
    return 0xFFu;
  }
  const uint8_t uid = MapAt(kMapUnitAddr, x, y);
  if (uid != 0u && uid != *GbaPtr<const uint8_t>(kAiActiveUnitIdAddr)) {
    return 0xFFu;
  }
  return cost;
}

// ---------------------------------------------------------------------------
// 0x0803BCE8: AiFindReachableDoor
// ---------------------------------------------------------------------------
bool AiFindReachableDoor(GbaPtr<Unit> unit, GbaPtr<AiPos> out_pos) {
  AiMapFloodRangeAllowDoorNoUnit(unit);
  if ((GbaCall(kAddrAiFindNearestAdjacentToTerrainInList,
               kAiDoorTerrainListAddr, 0u, out_pos.addr) &
       0xFFu) == 0u) {
    return false;
  }
  AiMapFloodRangeExtended(unit);
  return static_cast<int8_t>(MapAt(kMapRangeAddr, out_pos->x, out_pos->y)) <=
         0x77;
}

// ---------------------------------------------------------------------------
// 0x0803BD3C: AiFindReachableChest
// ---------------------------------------------------------------------------
bool AiFindReachableChest(GbaPtr<Unit> unit, GbaPtr<AiPos> out_pos) {
  AiMapFloodRangeExtended(unit);
  return (GbaCall(kAddrAiFindNearestTerrainInList, kAiChestTerrainListAddr, 0u,
                  out_pos.addr) &
          0xFFu) != 0u;
}

// ---------------------------------------------------------------------------
// 0x0803BD64: AiFindReachableDoorOrChest
// ---------------------------------------------------------------------------
bool AiFindReachableDoorOrChest(GbaPtr<Unit> unit, int flags,
                                GbaPtr<AiPos> out_pos) {
  AiMapFloodUnitExtended(unit);
  AiMapFloodRangeAllowDoor(unit);
  if (static_cast<int8_t>(GbaCall(kAddrAiFindNearestDoorOrChest,
                                  static_cast<uint32_t>(flags | 1),
                                  out_pos.addr) &
                          0xFFu) == 1 &&
      static_cast<int8_t>(MapAt(kMapMovementAddr, out_pos->x, out_pos->y)) <=
          0x77) {
    return true;
  }

  AiMapFloodRangeAllowDoorNoUnit(unit);
  if (static_cast<int8_t>(GbaCall(kAddrAiFindNearestDoorOrChest,
                                  static_cast<uint32_t>(flags), out_pos.addr) &
                          0xFFu) == 1) {
    if (static_cast<int8_t>(MapAt(kMapMovementAddr, out_pos->x, out_pos->y)) >
            0x77 ||
        MapAt(kMapUnitAddr, out_pos->x, out_pos->y) != 0u) {
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// 0x0803BE04..0x0803BE6C: Working movement table helpers
// ---------------------------------------------------------------------------
void AiSetMovTableAllPassable(GbaPtr<const int8_t> base_table) {
  GbaPtr<int8_t> dst(kWorkingMovTableAddr);
  for (int i = 1; i <= 0x40; ++i) {
    dst[i] = base_table[i] > 0 ? base_table[i] : 1;
  }
}

void AiSetMovTableAllowOneTerrain(GbaPtr<const int8_t> base_table,
                                  int terrain_id) {
  GbaPtr<int8_t> dst(kWorkingMovTableAddr);
  for (int i = 1; i <= 0x40; ++i) {
    dst[i] = base_table[i];
  }
  dst[terrain_id] = 1;
}

void AiSetMovTableAllowTwoTerrains(GbaPtr<const int8_t> base_table,
                                   int terrain_a, int terrain_b) {
  GbaPtr<int8_t> dst(kWorkingMovTableAddr);
  for (int i = 1; i <= 0x40; ++i) {
    dst[i] = base_table[i];
  }
  dst[terrain_a] = 1;
  dst[terrain_b] = 1;
}

// ---------------------------------------------------------------------------
// 0x0803BEA0..0x0803C0FC: AI map-flood wrappers & special-items flag
// ---------------------------------------------------------------------------
void AiMapFloodUnitExtended(GbaPtr<Unit> unit) {
  SetWorkingMoveTable(
      GbaPtr<const int8_t>(GbaCall(kAddrGetUnitMovementCost, unit.addr)));
  SetWorkingMap(MapRows(kMapMovementAddr));
  BeginMapFlood(unit->x, unit->y, 0x7C, unit->id);
}

void AiMapFloodRangeExtended(GbaPtr<Unit> unit) {
  SetWorkingMoveTable(
      GbaPtr<const int8_t>(GbaCall(kAddrGetUnitMovementCost, unit.addr)));
  SetWorkingMap(MapRows(kMapRangeAddr));
  BeginMapFlood(unit->x, unit->y, 0x7C, unit->id);
}

void AiMapFloodUnitAllPassable(GbaPtr<Unit> unit) {
  AiSetMovTableAllPassable(
      GbaPtr<const int8_t>(GbaCall(kAddrGetUnitMovementCost, unit.addr)));
  SetWorkingMap(MapRows(kMapMovementAddr));
  BeginMapFlood(unit->x, unit->y, 0x7C, unit->id);
}

void AiMapFloodUnitAllPassableNoUnit(GbaPtr<Unit> unit) {
  AiSetMovTableAllPassable(
      GbaPtr<const int8_t>(GbaCall(kAddrGetUnitMovementCost, unit.addr)));
  SetWorkingMap(MapRows(kMapMovementAddr));
  BeginMapFlood(unit->x, unit->y, 0x7C, 0);
}

void AiMapFloodNoDoors(int x, int y, GbaPtr<const int8_t> mov_table) {
  AiSetMovTableAllPassable(mov_table);
  SetWorkingMap(MapRows(kMapRangeAddr));
  BeginMapFlood(x, y, 0x7C, 0);
}

void AiMapFloodNoDoorsUnit(int x, int y, GbaPtr<Unit> unit) {
  AiSetMovTableAllPassable(
      GbaPtr<const int8_t>(GbaCall(kAddrGetUnitMovementCost, unit.addr)));
  SetWorkingMap(MapRows(kMapRangeAddr));
  BeginMapFlood(x, y, 0x7C, unit->id);
}

void AiMapFloodRangeAllowDoor(GbaPtr<Unit> unit) {
  AiSetMovTableAllowOneTerrain(
      GbaPtr<const int8_t>(GbaCall(kAddrGetUnitMovementCost, unit.addr)), 0x1E);
  SetWorkingMap(MapRows(kMapRangeAddr));
  BeginMapFlood(unit->x, unit->y, 0x7C, unit->id);
}

void AiMapFloodRangeAllowDoorNoUnit(GbaPtr<Unit> unit) {
  AiSetMovTableAllowOneTerrain(
      GbaPtr<const int8_t>(GbaCall(kAddrGetUnitMovementCost, unit.addr)), 0x1E);
  SetWorkingMap(MapRows(kMapRangeAddr));
  BeginMapFlood(unit->x, unit->y, 0x7C, 0);
}

void AiMapFloodRangeAllowObstacles(GbaPtr<Unit> unit) {
  AiSetMovTableAllowTwoTerrains(
      GbaPtr<const int8_t>(GbaCall(kAddrGetUnitMovementCost, unit.addr)), 0x1B,
      0x33);
  SetWorkingMap(MapRows(kMapRangeAddr));
  BeginMapFlood(unit->x, unit->y, 0x7C, unit->id);
}

void AiMapFloodObstacles(GbaPtr<Unit> unit) {
  AiSetMovTableAllowTwoTerrains(
      GbaPtr<const int8_t>(GbaCall(kAddrGetUnitMovementCost, unit.addr)), 0x1B,
      0x33);
  SetWorkingMap(MapRows(kMapRangeAddr));
  BeginMapFlood(unit->x, unit->y, 0x7C, 0);
}

void AiMapFloodRangeUnitMov(GbaPtr<Unit> unit) {
  SetWorkingMoveTable(
      GbaPtr<const int8_t>(GbaCall(kAddrGetUnitMovementCost, unit.addr)));
  SetWorkingMap(MapRows(kMapRangeAddr));
  const int mov = unit->bonus_mov + unit->jinfo->base_mov;
  BeginMapFlood(unit->x, unit->y, mov, unit->id);
}

void AiUpdateIgnoreSpecialItemsFlag(GbaPtr<const Unit> unit) {
  if ((unit->ai_config & 0x2000u) != 0u) {
    AiStateBytes()[0x7B] |= 2u;
  } else {
    AiStateBytes()[0x7B] &= static_cast<uint8_t>(~2u);
  }
}

void AiMapFloodRangeWithUnit(int x, int y, GbaPtr<Unit> unit) {
  SetWorkingMoveTable(
      GbaPtr<const int8_t>(GbaCall(kAddrGetUnitMovementCost, unit.addr)));
  SetWorkingMap(MapRows(kMapRangeAddr));
  BeginMapFlood(x, y, 0x7C, unit->id);
}

void RegisterAiStaffLootFunctions() {
  RegisterNative(kAddrAiTryWarpStaff, &ThunkAiTryWarpStaff);
  RegisterNative(kAddrAiTryRestoreStaff, &ThunkAiTryRestoreStaff);
  RegisterNative(kAddrAiUnitHasUsableWeaponOrStaff,
                 &ThunkAiUnitHasUsableWeaponOrStaff);
  RegisterNative(kAddrAiGetStatusStaffTargetScore,
                 &ThunkAiGetStatusStaffTargetScore);
  RegisterNative(kAddrAiTrySilenceStaff, &ThunkAiTrySilenceStaff);
  RegisterNative(kAddrAiTrySleepOrBerserkStaff, &ThunkAiTrySleepOrBerserkStaff);
  RegisterNative(kAddrAiTryBarrierStaff, &ThunkAiTryBarrierStaff);
  RegisterNative(kAddrAiFindBestWarpDestination,
                 &ThunkAiFindBestWarpDestination);
  RegisterNative(kAddrAiGetSpecialItemTableIndex,
                 &ThunkAiGetSpecialItemTableIndex);
  RegisterNative(kAddrAiTryDoorOrChestAction, &ThunkAiTryDoorOrChestAction);
  RegisterNative(kAddrAiTryUseDoorKey, &ThunkAiTryUseDoorKey);
  RegisterNative(kAddrAiTryUseChestKeyOrLockpick,
                 &ThunkAiTryUseChestKeyOrLockpick);
  RegisterNative(kAddrAiTryUseAntitoxin, &ThunkAiTryUseAntitoxin);
  RegisterNative(kAddrAiGetRangeCostIfPassable, &ThunkAiGetRangeCostIfPassable);
  RegisterNative(kAddrAiFindReachableDoor, &ThunkAiFindReachableDoor);
  RegisterNative(kAddrAiFindReachableChest, &ThunkAiFindReachableChest);
  RegisterNative(kAddrAiFindReachableDoorOrChest,
                 &ThunkAiFindReachableDoorOrChest);
  RegisterNative(kAddrAiSetMovTableAllPassable, &ThunkAiSetMovTableAllPassable);
  RegisterNative(kAddrAiSetMovTableAllowOneTerrain,
                 &ThunkAiSetMovTableAllowOneTerrain);
  RegisterNative(kAddrAiSetMovTableAllowTwoTerrains,
                 &ThunkAiSetMovTableAllowTwoTerrains);
  RegisterNative(kAddrAiMapFloodUnitExtended, &ThunkAiMapFloodUnitExtended);
  RegisterNative(kAddrAiMapFloodRangeExtended, &ThunkAiMapFloodRangeExtended);
  RegisterNative(kAddrAiMapFloodUnitAllPassable,
                 &ThunkAiMapFloodUnitAllPassable);
  RegisterNative(kAddrAiMapFloodUnitAllPassableNoUnit,
                 &ThunkAiMapFloodUnitAllPassableNoUnit);
  RegisterNative(kAddrAiMapFloodNoDoors, &ThunkAiMapFloodNoDoors);
  RegisterNative(kAddrAiMapFloodNoDoorsUnit, &ThunkAiMapFloodNoDoorsUnit);
  RegisterNative(kAddrAiMapFloodRangeAllowDoor, &ThunkAiMapFloodRangeAllowDoor);
  RegisterNative(kAddrAiMapFloodRangeAllowDoorNoUnit,
                 &ThunkAiMapFloodRangeAllowDoorNoUnit);
  RegisterNative(kAddrAiMapFloodRangeAllowObstacles,
                 &ThunkAiMapFloodRangeAllowObstacles);
  RegisterNative(kAddrAiMapFloodObstacles, &ThunkAiMapFloodObstacles);
  RegisterNative(kAddrAiMapFloodRangeUnitMov, &ThunkAiMapFloodRangeUnitMov);
  RegisterNative(kAddrAiUpdateIgnoreSpecialItemsFlag,
                 &ThunkAiUpdateIgnoreSpecialItemsFlag);
  RegisterNative(kAddrAiMapFloodRangeWithUnit, &ThunkAiMapFloodRangeWithUnit);
}

}  // namespace fe7
