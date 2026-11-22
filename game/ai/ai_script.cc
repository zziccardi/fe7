#include "game/ai/ai_script.h"

#include <cstdint>

#include "absl/base/nullability.h"
#include "game/ai/ai.h"
#include "game/ai/ai_decide.h"
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

inline GbaPtr<GbaPtr<uint8_t>> MapHidden() {
  return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapHiddenAddr);
}

inline GbaPtr<const AiScriptCmd> CurrentScriptStart() {
  const int32_t kind = *GbaPtr<const int32_t>(kAiScriptKindAddr);
  GbaPtr<const Unit> active = ConstUnit(ActiveUnit());
  if (kind == 0) {
    GbaPtr<const GbaPtr<const AiScriptCmd>> table =
        *GbaPtr<const GbaPtr<const GbaPtr<const AiScriptCmd>>>(
            kAiScriptTableAPtrAddr);
    return table[active->ai_a];
  }
  GbaPtr<const GbaPtr<const AiScriptCmd>> table =
      *GbaPtr<const GbaPtr<const GbaPtr<const AiScriptCmd>>>(
          kAiScriptTableBPtrAddr);
  return table[active->ai_b];
}

// Host-pointer helper shared by `AiFindChestKeySlot` and
// `AiIsSpecialLootableTerrain` to avoid allocating GBA stack scratch.
bool FindChestKeySlotImpl(uint8_t* absl_nonnull out_slot) {
  *out_slot = 0u;
  GbaPtr<Unit> active = ActiveUnit();
  if (GetUnitItemCount(ConstUnit(active)) == 5) {
    active->ai_flags = static_cast<uint8_t>(active->ai_flags | 8u);
    return false;
  }
  for (int slot = 0; slot < 5; ++slot) {
    const uint16_t item = active->items[slot];
    if (item == 0u) {
      return false;
    }
    *out_slot = static_cast<uint8_t>(slot);
    const int iid = GetItemIid(item);
    if (iid == 0x68) {
      return true;
    }
    if (iid == 0x6A && (UnitAttributes(*active) & 4u) != 0u) {
      return true;
    }
  }
  return false;
}

void GotoScriptLabel(GbaPtr<uint8_t> pc_ptr, uint8_t label) {
  GbaPtr<const AiScriptCmd> base = CurrentScriptStart();
  if (label == 0u) {
    *pc_ptr = 0u;
    return;
  }
  uint8_t idx = 0u;
  for (;;) {
    GbaPtr<const AiScriptCmd> entry = base + idx;
    if (entry->cmd == 0x1Bu && entry->arg3 == label) {
      *pc_ptr = static_cast<uint8_t>(idx + 1u);
      return;
    }
    idx = static_cast<uint8_t>(idx + 1u);
  }
}

// Dispatch thunks ------------------------------------------------------------

uint32_t ThunkAiMakeMoveRangeMapsForUnitAndStaff(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  AiMakeMoveRangeMapsForUnitAndStaff(GbaPtr<const Unit>(r0));
  return 0u;
}

uint32_t ThunkAiMakeMoveRangeMapsForUnitAndItem(
    uint32_t r0, uint32_t r1, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  AiMakeMoveRangeMapsForUnitAndItem(GbaPtr<const Unit>(r0),
                                    static_cast<uint16_t>(r1));
  return 0u;
}

uint32_t ThunkAiGetStealItemPriority(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return static_cast<uint32_t>(
      AiGetStealItemPriority(static_cast<uint16_t>(r0)));
}

uint32_t ThunkAiGetBestStealableItemSlot(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return static_cast<uint32_t>(
      static_cast<int32_t>(AiGetBestStealableItemSlot(GbaPtr<const Unit>(r0))));
}

uint32_t ThunkAiFindSafestReachableLocation(
    uint32_t r0, uint32_t r1, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return AiFindSafestReachableLocation(GbaPtr<const Unit>(r0),
                                       GbaPtr<AiPos>(r1))
             ? 1u
             : 0u;
}

uint32_t ThunkAiFindChestKeySlot(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                                 uint32_t /*r3*/,
                                 const uint32_t* absl_nullable /*stack_args*/) {
  return AiFindChestKeySlot(GbaPtr<uint8_t>(r0)) ? 1u : 0u;
}

uint32_t ThunkAiGetUnitClosestValidPosition(
    uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
    const uint32_t* absl_nullable /*stack_args*/) {
  return AiGetUnitClosestValidPosition(
             GbaPtr<const Unit>(r0), static_cast<int16_t>(r1),
             static_cast<int16_t>(r2), GbaPtr<AiPos>(r3))
             ? 1u
             : 0u;
}

uint32_t ThunkAiIsUnitAliveByPid(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                                 uint32_t /*r3*/,
                                 const uint32_t* absl_nullable /*stack_args*/) {
  return AiIsUnitAliveByPid(static_cast<uint16_t>(r0)) ? 1u : 0u;
}

uint32_t ThunkAiIsWithinManhattanDistance(
    uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
    const uint32_t* absl_nullable stack_args) {
  const uint8_t max_dist =
      stack_args != nullptr ? static_cast<uint8_t>(stack_args[0]) : 0u;
  return AiIsWithinManhattanDistance(
             static_cast<int16_t>(r0), static_cast<int16_t>(r1),
             static_cast<uint8_t>(r2), static_cast<uint8_t>(r3), max_dist)
             ? 1u
             : 0u;
}

uint32_t ThunkAiIsSpecialLootableTerrain(
    uint32_t r0, uint32_t r1, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return AiIsSpecialLootableTerrain(static_cast<uint8_t>(r0),
                                    static_cast<uint8_t>(r1))
             ? 1u
             : 0u;
}

uint32_t ThunkAiRefreshAllBlueUnitFlags(
    uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  AiRefreshAllBlueUnitFlags();
  return 0u;
}

uint32_t ThunkAiCountAlliesInRecoveryMode(
    uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return static_cast<uint32_t>(AiCountAlliesInRecoveryMode());
}

uint32_t ThunkAiCountAlliesInRangeNeedingRecovery(
    uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return static_cast<uint32_t>(AiCountAlliesInRangeNeedingRecovery());
}

uint32_t ThunkAiUnitHasUsableStaffOrMagic(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return AiUnitHasUsableStaffOrMagic(GbaPtr<const Unit>(r0)) ? 1u : 0u;
}

uint32_t ThunkAiFloodMovementForUnit(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  AiFloodMovementForUnit(GbaPtr<const Unit>(r0));
  return 0u;
}

uint32_t ThunkAiTryExecScriptA(uint32_t /*r0*/, uint32_t /*r1*/,
                               uint32_t /*r2*/, uint32_t /*r3*/,
                               const uint32_t* absl_nullable /*stack_args*/) {
  return static_cast<uint32_t>(static_cast<int32_t>(AiTryExecScriptA()));
}

uint32_t ThunkAiExecFallbackScriptA(
    uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return static_cast<uint32_t>(static_cast<int32_t>(AiExecFallbackScriptA()));
}

uint32_t ThunkAiTryExecScriptB(uint32_t /*r0*/, uint32_t /*r1*/,
                               uint32_t /*r2*/, uint32_t /*r3*/,
                               const uint32_t* absl_nullable /*stack_args*/) {
  return static_cast<uint32_t>(static_cast<int32_t>(AiTryExecScriptB()));
}

uint32_t ThunkAiExecFallbackScriptB(
    uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return static_cast<uint32_t>(static_cast<int32_t>(AiExecFallbackScriptB()));
}

uint32_t ThunkAiScriptExec(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                           uint32_t /*r3*/,
                           const uint32_t* absl_nullable /*stack_args*/) {
  AiScriptExec(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdConditionalGoto(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  AiScriptCmdConditionalGoto(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdCallFunc(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  AiScriptCmdCallFunc(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdChangeAi(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  AiScriptCmdChangeAi(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiScriptCmdGoto(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                              uint32_t /*r3*/,
                              const uint32_t* absl_nullable /*stack_args*/) {
  AiScriptCmdGoto(GbaPtr<uint8_t>(r0));
  return 0u;
}

uint32_t ThunkAiIsUnitEnemy(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                            uint32_t /*r3*/,
                            const uint32_t* absl_nullable /*stack_args*/) {
  return AiIsUnitEnemy(GbaPtr<const Unit>(r0)) ? 1u : 0u;
}

uint32_t ThunkAiIsUnitNotActive(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                                uint32_t /*r3*/,
                                const uint32_t* absl_nullable /*stack_args*/) {
  return AiIsUnitNotActive(GbaPtr<const Unit>(r0)) ? 1u : 0u;
}

uint32_t ThunkAiIsUnitEnemyNotInPidList(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return AiIsUnitEnemyNotInPidList(GbaPtr<const Unit>(r0)) ? 1u : 0u;
}

uint32_t ThunkAiIsUnitEnemyOrInPidList(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return AiIsUnitEnemyOrInPidList(GbaPtr<const Unit>(r0)) ? 1u : 0u;
}

uint32_t ThunkAiIsUnitEnemyWithPid(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return AiIsUnitEnemyWithPid(GbaPtr<const Unit>(r0)) ? 1u : 0u;
}

uint32_t ThunkAiIsUnitEnemyWithJid(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return AiIsUnitEnemyWithJid(GbaPtr<const Unit>(r0)) ? 1u : 0u;
}

}  // namespace

void AiMakeMoveRangeMapsForUnitAndStaff(GbaPtr<const Unit> unit) {
  int range = GetUnitPower(unit);
  if (range > 0x14) {
    range = 0x14;
  }
  MapFloodUnit(unit);
  MapFill(MapRange(), 0);

  const MapSize size = GetMapSize();
  GbaPtr<GbaPtr<uint8_t>> mov = MapMovement();
  for (int y = size.y - 1; y >= 0; --y) {
    for (int x = size.x - 1; x >= 0; --x) {
      if (mov[y][x] <= 0x78u) {
        MapAddInRange(x, y, range, 1);
      }
    }
  }
}

void AiMakeMoveRangeMapsForUnitAndItem(GbaPtr<const Unit> unit, uint16_t item) {
  MapFloodUnit(unit);
  MapFill(MapRange(), 0);

  const MapSize size = GetMapSize();
  GbaPtr<GbaPtr<uint8_t>> mov = MapMovement();
  for (int y = size.y - 1; y >= 0; --y) {
    for (int x = size.x - 1; x >= 0; --x) {
      if (mov[y][x] <= 0x78u) {
        const int min_r = static_cast<int16_t>(GetItemMinRange(item));
        const int max_r = static_cast<int16_t>(GetItemMaxRange(item));
        MapSetInRange(static_cast<int16_t>(x), static_cast<int16_t>(y), min_r,
                      max_r);
      }
    }
  }
}

int AiGetStealItemPriority(uint16_t iid) {
  GbaPtr<const uint16_t> it(kAiStealPriorityListAddr);
  int idx = 0;
  while (*it != 0xFFFFu) {
    if (*it == iid) {
      return idx;
    }
    it = it + 1;
    ++idx;
  }
  return -1;
}

int8_t AiGetBestStealableItemSlot(GbaPtr<const Unit> unit) {
  uint8_t best_prio = 0xFFu;
  uint8_t best_slot = 0xFFu;
  for (int slot = 0; slot < 5; ++slot) {
    const uint16_t item = unit->items[slot];
    if (item == 0u) {
      break;
    }
    const uint8_t prio = static_cast<uint8_t>(
        AiGetStealItemPriority(static_cast<uint16_t>(item & 0xFFu)));
    if (best_prio >= prio) {
      best_prio = prio;
      best_slot = static_cast<uint8_t>(slot);
    }
  }
  return static_cast<int8_t>(best_slot);
}

bool AiFindSafestReachableLocation(GbaPtr<const Unit> unit,
                                   GbaPtr<AiPos> out_pos) {
  uint8_t min_danger = 0xFFu;
  GbaPtr<GbaPtr<uint8_t>> mov = MapMovement();
  if ((*GbaPtr<const uint8_t>(kAiStateAddr + 0x7Bu) & 2u) != 0u) {
    MapFill(mov, -1);
    mov[unit->y][unit->x] = 0u;
  } else {
    MapFloodUnit(unit);
  }

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
      const uint8_t danger = other[y][x];
      if (min_danger >= danger) {
        out_pos->x = static_cast<int16_t>(x);
        out_pos->y = static_cast<int16_t>(y);
        min_danger = danger;
      }
    }
  }
  return min_danger != 0xFFu;
}

bool AiFindChestKeySlot(GbaPtr<uint8_t> out_slot) {
  return FindChestKeySlotImpl(out_slot.get());
}

bool AiGetUnitClosestValidPosition(GbaPtr<const Unit> unit, int16_t x,
                                   int16_t y, GbaPtr<AiPos> out_pos) {
  GbaPtr<GbaPtr<uint8_t>> unit_map = MapUnit();
  GbaPtr<GbaPtr<uint8_t>> other = MapOther();
  GbaPtr<GbaPtr<uint8_t>> hidden = MapHidden();

  if ((unit_map[y][x] | other[y][x] | hidden[y][x]) == 0u) {
    out_pos->x = x;
    out_pos->y = y;
    return true;
  }

  MapFloodRangeUnitless(x, y, GetUnitMovementCost(unit));
  MapFloodUnitExtended(unit);

  uint32_t min_dist = 0x7Cu;
  out_pos->x = -1;
  const MapSize size = GetMapSize();
  GbaPtr<GbaPtr<uint8_t>> mov = MapMovement();
  GbaPtr<GbaPtr<uint8_t>> range = MapRange();

  for (int16_t ty = static_cast<int16_t>(size.y - 1); ty >= 0; --ty) {
    for (int16_t tx = static_cast<int16_t>(size.x - 1); tx >= 0; --tx) {
      if (mov[ty][tx] > 0x78u) {
        continue;
      }
      if ((unit_map[ty][tx] | other[ty][tx] | hidden[ty][tx]) != 0u) {
        continue;
      }
      const uint8_t dist = range[ty][tx];
      if (dist <= min_dist) {
        min_dist = dist;
        out_pos->x = tx;
        out_pos->y = ty;
      }
    }
  }
  return out_pos->x != -1;
}

bool AiIsUnitAliveByPid(uint16_t pid) {
  for (int uid = 1; uid <= 0xBF; ++uid) {
    GbaPtr<Unit> unit = GetUnit(uid);
    if (!unit || !unit->pinfo) {
      continue;
    }
    if (unit->pinfo->id != pid) {
      continue;
    }
    if ((unit->flags & kUnitFlagRescued) != 0u) {
      return true;
    }
    return (unit->flags & 0x00010005u) == 0u;
  }
  return false;
}

bool AiIsWithinManhattanDistance(int16_t x1, int16_t y1, uint8_t x2, uint8_t y2,
                                 uint8_t max_dist) {
  int dx = static_cast<int>(x1) - static_cast<int>(x2);
  if (dx < 0) {
    dx = -dx;
  }
  int dy = static_cast<int>(y1) - static_cast<int>(y2);
  if (dy < 0) {
    dy = -dy;
  }
  const uint16_t dist = static_cast<uint16_t>(dx + dy);
  return dist <= max_dist;
}

bool AiIsSpecialLootableTerrain(uint8_t x, uint8_t y) {
  const uint8_t t = MapTerrain()[y][x];
  if (t == 0x03u || t == 0x24u || t == 0x37u) {
    return true;
  }
  if (t == 0x21u) {
    uint8_t slot = 0u;
    return FindChestKeySlotImpl(&slot);
  }
  return false;
}

void AiRefreshAllBlueUnitFlags() {
  GbaPtr<uint8_t> max_mov_ptr(kAiStateAddr + 0x85u);
  *max_mov_ptr = 0u;

  for (int uid = 1; uid <= 0x3F; ++uid) {
    GbaPtr<Unit> unit = GetUnit(uid);
    if (!unit || !unit->pinfo) {
      continue;
    }
    if ((unit->flags & 0x00010005u) != 0u) {
      continue;
    }
    const uint8_t mov =
        static_cast<uint8_t>(static_cast<uint8_t>(unit->bonus_mov) +
                             static_cast<uint8_t>(unit->jinfo->base_mov));
    if (mov > *max_mov_ptr) {
      *max_mov_ptr = mov;
    }

    GbaPtr<const Unit> cunit = ConstUnit(unit);
    for (int slot = 0; slot < 5; ++slot) {
      const uint16_t item = unit->items[slot];
      if (item == 0u) {
        break;
      }
      if (CanUnitUseWeapon(cunit, item) || CanUnitUseStaff(cunit, item)) {
        if ((GetItemAttributes(item) & 2u) != 0u) {
          unit->ai_flags = static_cast<uint8_t>(unit->ai_flags | 1u);
        }
        AiUpdateUnitStaffFlags(unit, item);
        AiUpdateUnitItemFlags(unit, item);
      }
    }
    AiCountNearbyAllies(unit);
  }
}

int AiCountAlliesInRecoveryMode() {
  int count = 0;
  const int phase = GetCurrentPhase();
  for (int uid = phase + 1; uid < phase + 0x80; ++uid) {
    GbaPtr<Unit> unit = GetUnit(uid);
    if (!unit || !unit->pinfo) {
      continue;
    }
    if ((unit->flags & 0x00010005u) != 0u) {
      continue;
    }
    if ((unit->ai_flags & 1u) != 0u) {
      ++count;
    }
  }
  return count;
}

int AiCountAlliesInRangeNeedingRecovery() {
  int count = 0;
  const MapSize size = GetMapSize();
  GbaPtr<GbaPtr<uint8_t>> range = MapRange();
  GbaPtr<GbaPtr<uint8_t>> unit_map = MapUnit();
  const uint8_t active_id = ActiveUnitId();

  for (int y = size.y - 1; y >= 0; --y) {
    for (int x = size.x - 1; x >= 0; --x) {
      if (static_cast<int8_t>(range[y][x]) == 0) {
        continue;
      }
      const uint8_t uid = unit_map[y][x];
      if (uid != 0u && AreUnitIdsAllied(active_id, uid)) {
        if ((GetUnit(uid)->ai_flags & 1u) != 0u) {
          ++count;
        }
      }
    }
  }
  return count;
}

bool AiUnitHasUsableStaffOrMagic(GbaPtr<const Unit> unit) {
  for (int slot = 0; slot < 5; ++slot) {
    const uint16_t item = unit->items[slot];
    if (item == 0u) {
      return false;
    }
    if ((GetItemAttributes(item) & 6u) != 0u && CanUnitUseStaff(unit, item)) {
      return true;
    }
  }
  return false;
}

void AiFloodMovementForUnit(GbaPtr<const Unit> unit) {
  if ((*GbaPtr<const uint8_t>(kAiStateAddr + 0x7Bu) & 2u) != 0u) {
    MapFloodUnitMovement(unit, 0);
  } else {
    MapFloodUnit(unit);
  }
}

int8_t AiTryExecScriptA() {
  GbaPtr<Unit> active = ActiveUnit();
  GbaPtr<const GbaPtr<const AiScriptCmd>> table =
      *GbaPtr<const GbaPtr<const GbaPtr<const AiScriptCmd>>>(
          kAiScriptTableAPtrAddr);
  GbaPtr<const AiScriptCmd> cur = table[active->ai_a] + active->ai_a_pc;
  *GbaPtr<GbaPtr<const AiScriptCmd>>(kAiScriptCurAddr) = cur;
  *GbaPtr<int8_t>(kAiScriptDoneAddr) = 1;
  *GbaPtr<int32_t>(kAiScriptKindAddr) = 0;
  AiScriptExec(GbaPtr<uint8_t>(active.addr + 0x43u));
  return *GbaPtr<const int8_t>(kAiScriptDoneAddr);
}

int8_t AiExecFallbackScriptA() {
  *GbaPtr<GbaPtr<const AiScriptCmd>>(kAiScriptCurAddr) =
      GbaPtr<const AiScriptCmd>(kAiFallbackScriptAAddr);
  *GbaPtr<int8_t>(kAiScriptDoneAddr) = 1;
  *GbaPtr<int32_t>(kAiScriptKindAddr) = 0;
  AiScriptExec(GbaPtr<uint8_t>(ActiveUnit().addr + 0x43u));
  return *GbaPtr<const int8_t>(kAiScriptDoneAddr);
}

int8_t AiTryExecScriptB() {
  GbaPtr<Unit> active = ActiveUnit();
  GbaPtr<const GbaPtr<const AiScriptCmd>> table =
      *GbaPtr<const GbaPtr<const GbaPtr<const AiScriptCmd>>>(
          kAiScriptTableBPtrAddr);
  GbaPtr<const AiScriptCmd> cur = table[active->ai_b] + active->ai_b_pc;
  *GbaPtr<GbaPtr<const AiScriptCmd>>(kAiScriptCurAddr) = cur;
  *GbaPtr<int8_t>(kAiScriptDoneAddr) = 1;
  *GbaPtr<int32_t>(kAiScriptKindAddr) = 1;
  AiScriptExec(GbaPtr<uint8_t>(active.addr + 0x45u));
  return *GbaPtr<const int8_t>(kAiScriptDoneAddr);
}

int8_t AiExecFallbackScriptB() {
  *GbaPtr<GbaPtr<const AiScriptCmd>>(kAiScriptCurAddr) =
      GbaPtr<const AiScriptCmd>(kAiFallbackScriptBAddr);
  *GbaPtr<int8_t>(kAiScriptDoneAddr) = 1;
  *GbaPtr<int32_t>(kAiScriptKindAddr) = 1;
  AiScriptExec(GbaPtr<uint8_t>(ActiveUnit().addr + 0x45u));
  return *GbaPtr<const int8_t>(kAiScriptDoneAddr);
}

void AiScriptExec(GbaPtr<uint8_t> pc_ptr) {
  GbaPtr<GbaPtr<const AiScriptCmd>> cur_slot(kAiScriptCurAddr);
  if ((*cur_slot)->cmd > 0x1Bu) {
    const int32_t kind = *GbaPtr<const int32_t>(kAiScriptKindAddr);
    *cur_slot = GbaPtr<const AiScriptCmd>(kind == 0 ? kAiFallbackScriptAAddr
                                                    : kAiFallbackScriptBAddr);
  }
  GbaPtr<const AiScriptCmd> cur = *cur_slot;
  *GbaPtr<uint8_t>(kAiStateAddr + 0x7Eu) = cur->arg2;
  const uint32_t handler =
      GbaPtr<const uint32_t>(kAiScriptOpcodeTableAddr)[cur->cmd];
  GbaCall(handler & ~1u, pc_ptr.addr);
}

void AiScriptCmdConditionalGoto(GbaPtr<uint8_t> pc_ptr) {
  GbaPtr<const AiScriptCmd> cur =
      *GbaPtr<const GbaPtr<const AiScriptCmd>>(kAiScriptCurAddr);
  if (AiCompare(GbaPtr<const uint8_t>(cur->u32_arg8), cur->arg1,
                cur->u32_arg4)) {
    GotoScriptLabel(pc_ptr, cur->arg3);
  } else {
    *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
  }
  *GbaPtr<int8_t>(kAiScriptDoneAddr) = 0;
}

void AiScriptCmdCallFunc(GbaPtr<uint8_t> pc_ptr) {
  GbaPtr<const AiScriptCmd> cur =
      *GbaPtr<const GbaPtr<const AiScriptCmd>>(kAiScriptCurAddr);
  const uint32_t fn = cur->u32_arg8;
  *GbaPtr<uint32_t>(kAiScriptFuncPtrAddr) = fn;
  const uint32_t res = GbaCall(fn & ~1u, cur->u32_argC);
  *GbaPtr<int8_t>(kAiScriptDoneAddr) = static_cast<int8_t>(res);
  *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
}

void AiScriptCmdChangeAi(GbaPtr<uint8_t> pc_ptr) {
  GbaPtr<const AiScriptCmd> cur =
      *GbaPtr<const GbaPtr<const AiScriptCmd>>(kAiScriptCurAddr);
  const uint8_t new_a = cur->arg1;
  const uint8_t new_b = cur->arg2;
  GbaPtr<Unit> active = ActiveUnit();
  if (new_a != 0xFFu) {
    active->ai_a = new_a;
    active->ai_a_pc = 0u;
  }
  if (new_b != 0xFFu) {
    active->ai_b = new_b;
    active->ai_b_pc = 0u;
  }
  const int32_t kind = *GbaPtr<const int32_t>(kAiScriptKindAddr);
  if ((kind == 0 && new_a == 0xFFu) || (kind == 1 && new_b == 0xFFu)) {
    *pc_ptr = static_cast<uint8_t>(*pc_ptr + 1u);
  }
  *GbaPtr<uint8_t>(kAiStateAddr + 0x79u) = 0u;
}

void AiScriptCmdGoto(GbaPtr<uint8_t> pc_ptr) {
  GbaPtr<const AiScriptCmd> cur =
      *GbaPtr<const GbaPtr<const AiScriptCmd>>(kAiScriptCurAddr);
  GotoScriptLabel(pc_ptr, cur->arg3);
  *GbaPtr<int8_t>(kAiScriptDoneAddr) = 0;
}

bool AiIsUnitEnemy(GbaPtr<const Unit> unit) {
  return !AreUnitIdsAllied(ActiveUnit()->id, unit->id);
}

bool AiIsUnitNotActive(GbaPtr<const Unit> unit) {
  return unit.addr != ActiveUnit().addr;
}

bool AiIsUnitEnemyNotInPidList(GbaPtr<const Unit> unit) {
  GbaPtr<const AiScriptCmd> cur =
      *GbaPtr<const GbaPtr<const AiScriptCmd>>(kAiScriptCurAddr);
  if (AiIsInShortList(GbaPtr<const uint16_t>(cur->u32_arg8), unit->pinfo->id)) {
    return false;
  }
  return !AreUnitIdsAllied(ActiveUnit()->id, unit->id);
}

bool AiIsUnitEnemyOrInPidList(GbaPtr<const Unit> unit) {
  GbaPtr<const AiScriptCmd> cur =
      *GbaPtr<const GbaPtr<const AiScriptCmd>>(kAiScriptCurAddr);
  if (AiIsInShortList(GbaPtr<const uint16_t>(cur->u32_arg8), unit->pinfo->id)) {
    return true;
  }
  return !AreUnitIdsAllied(ActiveUnit()->id, unit->id);
}

bool AiIsUnitEnemyWithPid(GbaPtr<const Unit> unit) {
  GbaPtr<const AiScriptCmd> cur =
      *GbaPtr<const GbaPtr<const AiScriptCmd>>(kAiScriptCurAddr);
  if (unit->pinfo->id != static_cast<uint8_t>(cur->u32_arg4)) {
    return false;
  }
  return !AreUnitIdsAllied(ActiveUnit()->id, unit->id);
}

bool AiIsUnitEnemyWithJid(GbaPtr<const Unit> unit) {
  GbaPtr<const AiScriptCmd> cur =
      *GbaPtr<const GbaPtr<const AiScriptCmd>>(kAiScriptCurAddr);
  if (unit->jinfo->id != static_cast<uint8_t>(cur->u32_arg4)) {
    return false;
  }
  return !AreUnitIdsAllied(ActiveUnit()->id, unit->id);
}

void RegisterAiScriptFunctions() {
  RegisterNative(kAddrAiMakeMoveRangeMapsForUnitAndStaff,
                 &ThunkAiMakeMoveRangeMapsForUnitAndStaff);
  RegisterNative(kAddrAiMakeMoveRangeMapsForUnitAndItem,
                 &ThunkAiMakeMoveRangeMapsForUnitAndItem);
  RegisterNative(kAddrAiGetStealItemPriority, &ThunkAiGetStealItemPriority);
  RegisterNative(kAddrAiGetBestStealableItemSlot,
                 &ThunkAiGetBestStealableItemSlot);
  RegisterNative(kAddrAiFindSafestReachableLocation,
                 &ThunkAiFindSafestReachableLocation);
  RegisterNative(kAddrAiFindChestKeySlot, &ThunkAiFindChestKeySlot);
  RegisterNative(kAddrAiGetUnitClosestValidPosition,
                 &ThunkAiGetUnitClosestValidPosition);
  RegisterNative(kAddrAiIsUnitAliveByPid, &ThunkAiIsUnitAliveByPid);
  RegisterNative(kAddrAiIsWithinManhattanDistance,
                 &ThunkAiIsWithinManhattanDistance);
  RegisterNative(kAddrAiIsSpecialLootableTerrain,
                 &ThunkAiIsSpecialLootableTerrain);
  RegisterNative(kAddrAiRefreshAllBlueUnitFlags,
                 &ThunkAiRefreshAllBlueUnitFlags);
  RegisterNative(kAddrAiCountAlliesInRecoveryMode,
                 &ThunkAiCountAlliesInRecoveryMode);
  RegisterNative(kAddrAiCountAlliesInRangeNeedingRecovery,
                 &ThunkAiCountAlliesInRangeNeedingRecovery);
  RegisterNative(kAddrAiUnitHasUsableStaffOrMagic,
                 &ThunkAiUnitHasUsableStaffOrMagic);
  RegisterNative(kAddrAiFloodMovementForUnit, &ThunkAiFloodMovementForUnit);
  RegisterNative(kAddrAiTryExecScriptA, &ThunkAiTryExecScriptA);
  RegisterNative(kAddrAiExecFallbackScriptA, &ThunkAiExecFallbackScriptA);
  RegisterNative(kAddrAiTryExecScriptB, &ThunkAiTryExecScriptB);
  RegisterNative(kAddrAiExecFallbackScriptB, &ThunkAiExecFallbackScriptB);
  RegisterNative(kAddrAiScriptExec, &ThunkAiScriptExec);
  RegisterNative(kAddrAiScriptCmdConditionalGoto,
                 &ThunkAiScriptCmdConditionalGoto);
  RegisterNative(kAddrAiScriptCmdCallFunc, &ThunkAiScriptCmdCallFunc);
  RegisterNative(kAddrAiScriptCmdChangeAi, &ThunkAiScriptCmdChangeAi);
  RegisterNative(kAddrAiScriptCmdGoto, &ThunkAiScriptCmdGoto);
  RegisterNative(kAddrAiIsUnitEnemy, &ThunkAiIsUnitEnemy);
  RegisterNative(kAddrAiIsUnitNotActive, &ThunkAiIsUnitNotActive);
  RegisterNative(kAddrAiIsUnitEnemyNotInPidList,
                 &ThunkAiIsUnitEnemyNotInPidList);
  RegisterNative(kAddrAiIsUnitEnemyOrInPidList, &ThunkAiIsUnitEnemyOrInPidList);
  RegisterNative(kAddrAiIsUnitEnemyWithPid, &ThunkAiIsUnitEnemyWithPid);
  RegisterNative(kAddrAiIsUnitEnemyWithJid, &ThunkAiIsUnitEnemyWithJid);
}

}  // namespace fe7
