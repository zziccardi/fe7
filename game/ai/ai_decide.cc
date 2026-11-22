#include "game/ai/ai_decide.h"

#include <cstdint>

#include "absl/base/nullability.h"
#include "game/ai/ai.h"
#include "game/engine/rng.h"
#include "game/item.h"
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

inline uint8_t ActiveUnitId() { return *GbaPtr<uint8_t>(kAiActiveUnitIdAddr); }

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

inline GbaPtr<const Unit> ConstUnit(GbaPtr<Unit> u) {
  return GbaPtr<const Unit>(u.addr);
}

inline MapSize GetMapSize() { return *GbaPtr<MapSize>(kMapSizeAddr); }

// Host-pointer implementation of `AiRankUnitItems` shared by both the public
// GbaPtr entry point and `AiEquipBestItem` (avoiding GBA stack scratch).
uint32_t RankUnitItemsImpl(uint16_t* absl_nonnull out_ranks) {
  GbaPtr<Unit> active = ActiveUnit();
  GbaPtr<const Unit> cactive = ConstUnit(active);
  if (GetUnitItemCount(cactive) == 0) {
    return 0u;
  }

  out_ranks[0] = 0u;
  uint16_t item = active->items[0];
  if (item == 0u) {
    // Original agbcc omits an explicit return statement here, leaving
    // `gActiveUnit` in r0.
    return active.addr;
  }

  for (int slot = 0;;) {
    const uint32_t attrs = GetItemAttributes(item);
    if ((attrs & (kItemAttrWeapon | kItemAttrStaff)) != 0u &&
        (attrs & 0x400u) == 0u) {
      if (CanUnitUseWeapon(cactive, item) || CanUnitUseStaff(cactive, item)) {
        if ((attrs & kItemAttrWeapon) != 0u) {
          if (GetItemMinRange(item) > 1) {
            out_ranks[slot] = static_cast<uint16_t>(out_ranks[slot] | 2u);
          }
          if (GetItemMaxRange(item) == 1) {
            out_ranks[slot] = static_cast<uint16_t>(out_ranks[slot] | 1u);
          }
          const int max_uses = GetItemMaxUses(item);
          const uint32_t pct =
              max_uses != 0
                  ? static_cast<uint32_t>((GetItemUses(item) * 100) / max_uses)
                  : 0u;
          if (pct <= 10u) {
            out_ranks[slot] = static_cast<uint16_t>(out_ranks[slot] | 4u);
          }
        } else {
          AiUpdateUnitSpecialStaffFlags(item);
          out_ranks[slot] = static_cast<uint16_t>(out_ranks[slot] | 8u);
        }
        const uint32_t might_bits = static_cast<uint32_t>(GetItemMight(item))
                                    << 8;
        out_ranks[slot] = static_cast<uint16_t>(out_ranks[slot] | might_bits);
      }
    }

    ++slot;
    if (slot > 4) {
      // Loop exit leaves `r0 = 2` from the slot-offset increment.
      return 2u;
    }
    out_ranks[slot] = 0u;
    item = active->items[slot];
    if (item == 0u) {
      // Early break leaves `&gActiveUnit->items[slot]` in r0.
      return active.addr + 0x1Eu + static_cast<uint32_t>(slot) * 2u;
    }
  }
}

void EquipBestItemWithMaskImpl(uint32_t mask,
                               const uint16_t* absl_nonnull item_ranks) {
  int best_slot = -1;
  uint32_t best_might = 0u;
  for (int slot = 0; slot < 5; ++slot) {
    const uint16_t rank = item_ranks[slot];
    if (rank == 0u || (static_cast<uint32_t>(rank) & mask) == 0u) {
      continue;
    }
    const uint32_t might_bits = static_cast<uint32_t>(rank) & 0xFF00u;
    if (might_bits > best_might) {
      best_might = might_bits;
      best_slot = slot;
    }
  }
  if (best_slot > 0) {
    UnitEquipItemSlot(ActiveUnit(), best_slot);
  }
}

void ChooseBestItemToEquipImpl(uint16_t ranged_threat, uint16_t melee_threat,
                               uint16_t /*total_threat*/,
                               const uint16_t* absl_nonnull item_ranks) {
  // Thumb `cmn r1, r0; beq` skips equipping only when both threats are 0.
  if (static_cast<uint32_t>(ranged_threat) +
          static_cast<uint32_t>(melee_threat) ==
      0u) {
    return;
  }
  if (melee_threat >= ranged_threat) {
    EquipBestItemWithMaskImpl(1u, item_ranks);
  } else {
    EquipBestItemWithMaskImpl(2u, item_ranks);
  }
}

void ComputeEnemyThreatAtTileImpl(int x, int y,
                                  uint16_t* absl_nonnull out_ranged,
                                  uint16_t* absl_nonnull out_melee,
                                  uint16_t* absl_nonnull out_total) {
  *out_total = 0u;
  *out_melee = 0u;
  *out_ranged = 0u;

  GbaPtr<GbaPtr<uint8_t>> other = MapOther();
  MapFill(other, 0);

  const uint8_t active_id = ActiveUnitId();
  const MapSize size = GetMapSize();

  for (int uid = 1; uid <= 0xBF; ++uid) {
    GbaPtr<Unit> unit = GetUnit(uid);
    if (!unit || !unit->pinfo) {
      continue;
    }
    if ((unit->flags & (kUnitFlagDead | kUnitFlagRescued)) != 0u) {
      continue;
    }
    if (AreUnitIdsAllied(active_id, unit->id)) {
      continue;
    }
    GbaPtr<const Unit> cunit = ConstUnit(unit);
    if (!AiIsWithinMoveBirdsEye(cunit, x, y)) {
      continue;
    }

    MapFloodUnit(cunit);
    GbaPtr<GbaPtr<uint8_t>> mov = MapMovement();
    if (mov[y][x] == 0xFFu) {
      continue;
    }

    const uint16_t weapon = static_cast<uint16_t>(GetUnitEquippedWeapon(cunit));
    const int power = GetUnitPower(cunit) + GetItemMight(weapon);
    if (GetItemMinRange(weapon) > 1) {
      *out_ranged = static_cast<uint16_t>(static_cast<uint32_t>(*out_ranged) +
                                          static_cast<uint32_t>(power));
    }
    if (GetItemMaxRange(weapon) == 1) {
      *out_melee = static_cast<uint16_t>(static_cast<uint32_t>(*out_melee) +
                                         static_cast<uint32_t>(power));
    }

    for (int ty = size.y - 1; ty >= 0; --ty) {
      for (int tx = size.x - 1; tx >= 0; --tx) {
        if (mov[ty][tx] <= 0x78u) {
          int sum = static_cast<int>(other[ty][tx]) + power;
          if (sum > 0xFF) {
            sum = 0xFF;
          }
          other[ty][tx] = static_cast<uint8_t>(sum);
        }
      }
    }
  }

  *out_total = static_cast<uint16_t>(static_cast<uint32_t>(*out_ranged) +
                                     static_cast<uint32_t>(*out_melee));
}

// Dispatch thunks ------------------------------------------------------------

uint32_t ThunkAiEquipBestItem(uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/,
                              uint32_t /*r3*/,
                              const uint32_t* absl_nullable /*stack_args*/) {
  AiEquipBestItem();
  return 0u;
}

uint32_t ThunkAiCompare(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t /*r3*/,
                        const uint32_t* absl_nullable /*stack_args*/) {
  return AiCompare(GbaPtr<const uint8_t>(r0), static_cast<uint8_t>(r1), r2)
             ? 1u
             : 0u;
}

uint32_t ThunkAiTryFindNearestUnitOfClass(
    uint32_t r0, uint32_t r1, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return AiTryFindNearestUnitOfClass(r0, GbaPtr<AiPos>(r1)) ? 1u : 0u;
}

uint32_t ThunkAiTryMoveRandomly(uint32_t /*r0*/, uint32_t /*r1*/,
                                uint32_t /*r2*/, uint32_t /*r3*/,
                                const uint32_t* absl_nullable /*stack_args*/) {
  AiTryMoveRandomly();
  return 0u;
}

uint32_t ThunkAiGetMoveCostAt(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                              uint32_t /*r3*/,
                              const uint32_t* absl_nullable /*stack_args*/) {
  return AiGetMoveCostAt(static_cast<int32_t>(r0), static_cast<int32_t>(r1));
}

uint32_t ThunkAiCountUnitsInRange(
    uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return static_cast<uint32_t>(AiCountUnitsInRange());
}

uint32_t ThunkAiCountEnemiesInRange(
    uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return static_cast<uint32_t>(AiCountEnemiesInRange());
}

uint32_t ThunkAiCountAlliesInRange(
    uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return static_cast<uint32_t>(AiCountAlliesInRange());
}

uint32_t ThunkAiCountUnitsNearby(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                                 uint32_t /*r3*/,
                                 const uint32_t* absl_nullable /*stack_args*/) {
  return static_cast<uint32_t>(
      AiCountUnitsNearby(static_cast<int16_t>(r0), static_cast<int16_t>(r1)));
}

uint32_t ThunkAiCountEnemiesNearby(
    uint32_t r0, uint32_t r1, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return static_cast<uint32_t>(
      AiCountEnemiesNearby(static_cast<int16_t>(r0), static_cast<int16_t>(r1)));
}

uint32_t ThunkAiCountAlliesNearby(
    uint32_t r0, uint32_t r1, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return static_cast<uint32_t>(
      AiCountAlliesNearby(static_cast<int16_t>(r0), static_cast<int16_t>(r1)));
}

uint32_t ThunkAiIsWithinDangerThreshold(
    uint32_t r0, uint32_t r1, uint32_t r2, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return AiIsWithinDangerThreshold(static_cast<int32_t>(r0),
                                   static_cast<int32_t>(r1),
                                   static_cast<uint8_t>(r2))
             ? 1u
             : 0u;
}

uint32_t ThunkAiUpdateFactionRecovery(
    uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  AiUpdateFactionRecovery();
  return 0u;
}

uint32_t ThunkAiUpdateUnitRecoveryMode(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return AiUpdateUnitRecoveryMode(GbaPtr<Unit>(r0)) ? 1u : 0u;
}

uint32_t ThunkAiGetClosestEscapePoint(
    uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return AiGetClosestEscapePoint().addr;
}

uint32_t ThunkAiCanEquipBestItem(uint32_t /*r0*/, uint32_t /*r1*/,
                                 uint32_t /*r2*/, uint32_t /*r3*/,
                                 const uint32_t* absl_nullable /*stack_args*/) {
  return AiCanEquipBestItem() ? 1u : 0u;
}

uint32_t ThunkAiRankUnitItems(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                              uint32_t /*r3*/,
                              const uint32_t* absl_nullable /*stack_args*/) {
  return AiRankUnitItems(GbaPtr<uint16_t>(r0));
}

uint32_t ThunkAiComputeEnemyThreatAtTile(
    uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
    const uint32_t* absl_nullable stack_args) {
  const uint32_t out_total = stack_args != nullptr ? stack_args[0] : 0u;
  AiComputeEnemyThreatAtTile(static_cast<int32_t>(r0), static_cast<int32_t>(r1),
                             GbaPtr<uint16_t>(r2), GbaPtr<uint16_t>(r3),
                             GbaPtr<uint16_t>(out_total));
  return 0u;
}

uint32_t ThunkAiEquipBestItemWithMask(
    uint32_t r0, uint32_t r1, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  AiEquipBestItemWithMask(r0, GbaPtr<const uint16_t>(r1));
  return 0u;
}

uint32_t ThunkAiChooseBestItemToEquip(
    uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
    const uint32_t* absl_nullable /*stack_args*/) {
  AiChooseBestItemToEquip(static_cast<uint16_t>(r0), static_cast<uint16_t>(r1),
                          static_cast<uint16_t>(r2),
                          GbaPtr<const uint16_t>(r3));
  return 0u;
}

uint32_t ThunkAiUpdateUnitSpecialStaffFlags(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  AiUpdateUnitSpecialStaffFlags(static_cast<uint16_t>(r0));
  return 0u;
}

uint32_t ThunkAiIsWithinMoveBirdsEye(
    uint32_t r0, uint32_t r1, uint32_t r2, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return AiIsWithinMoveBirdsEye(GbaPtr<const Unit>(r0),
                                static_cast<int32_t>(r1),
                                static_cast<int32_t>(r2))
             ? 1u
             : 0u;
}

uint32_t ThunkAiGetUnitAttackPower(
    uint32_t r0, uint32_t r1, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack_args*/) {
  return static_cast<uint32_t>(
      AiGetUnitAttackPower(GbaPtr<const Unit>(r0), GbaPtr<uint16_t>(r1)));
}

uint32_t ThunkAiTryDanceOrPlay(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                               uint32_t /*r3*/,
                               const uint32_t* absl_nullable /*stack_args*/) {
  return AiTryDanceOrPlay(static_cast<int32_t>(r0), static_cast<int32_t>(r1))
             ? 1u
             : 0u;
}

}  // namespace

void AiEquipBestItem() {
  if (!AiCanEquipBestItem()) {
    return;
  }
  uint16_t item_ranks[5]{};
  if (static_cast<uint8_t>(RankUnitItemsImpl(item_ranks)) == 0u) {
    return;
  }
  GbaPtr<const AiDecision> dec(kAiDecisionAddr);
  uint16_t ranged_threat = 0u;
  uint16_t melee_threat = 0u;
  uint16_t total_threat = 0u;
  ComputeEnemyThreatAtTileImpl(dec->x_move, dec->y_move, &ranged_threat,
                               &melee_threat, &total_threat);
  ChooseBestItemToEquipImpl(ranged_threat, melee_threat, total_threat,
                            item_ranks);
}

bool AiCompare(GbaPtr<const uint8_t> ptr, uint8_t op, uint32_t val) {
  const uint32_t lhs = *ptr;
  switch (op) {
    case 0:
      return lhs > val;
    case 1:
      return lhs >= val;
    case 2:
      return lhs == val;
    case 3:
      return lhs <= val;
    case 4:
      return lhs < val;
    case 5:
      return lhs != val;
    default:
      return false;
  }
}

bool AiTryFindNearestUnitOfClass(uint32_t jid, GbaPtr<AiPos> out_pos) {
  GbaPtr<const Unit> active = ConstUnit(ActiveUnit());
  MapFloodRangeUnitless(active->x, active->y, GetUnitMovementCost(active));

  out_pos->x = -1;
  int min_cost = 0xFF;
  GbaPtr<GbaPtr<uint8_t>> range = MapRange();

  for (int uid = 1; uid <= 0xBF; ++uid) {
    GbaPtr<Unit> unit = GetUnit(uid);
    if (!unit || !unit->pinfo) {
      continue;
    }
    if ((unit->flags & 0x00010025u) != 0u) {
      continue;
    }
    const uint8_t cost = range[unit->y][unit->x];
    if (cost > 0x78u) {
      continue;
    }
    if (unit->jinfo->id != jid) {
      continue;
    }
    if (min_cost >= static_cast<int8_t>(cost)) {
      min_cost = cost;
      out_pos->x = unit->x;
      out_pos->y = unit->y;
    }
  }
  return out_pos->x >= 0;
}

void AiTryMoveRandomly() {
  MapFloodUnit(ConstUnit(ActiveUnit()));

  const MapSize size = GetMapSize();
  GbaPtr<GbaPtr<uint8_t>> mov = MapMovement();
  GbaPtr<GbaPtr<uint8_t>> unit_map = MapUnit();

  uint32_t best_rn = 0u;
  int16_t best_x = -1;
  int16_t best_y = 0;

  for (int16_t y = static_cast<int16_t>(size.y - 1); y >= 0; --y) {
    for (int16_t x = static_cast<int16_t>(size.x - 1); x >= 0; --x) {
      if (mov[y][x] <= 0x78u && unit_map[y][x] == 0u) {
        const uint32_t rn = static_cast<uint8_t>(RandNext(0x100));
        if (rn >= best_rn) {
          best_rn = rn;
          best_x = x;
          best_y = y;
        }
      }
    }
  }

  if (best_x >= 0) {
    AiSetDecision(best_x, best_y, 0u, 0u, 0u, 0u, 0u);
  }
}

uint8_t AiGetMoveCostAt(int x, int y) {
  GbaPtr<GbaPtr<uint8_t>> range = MapRange();
  // Signed int8_t comparison (`> 0x77`) matches the Thumb `ldrsb` check.
  if (static_cast<int8_t>(range[y][x]) > 0x77) {
    return 0xFFu;
  }
  const uint8_t uid = MapUnit()[y][x];
  if (uid != 0u && uid != ActiveUnitId()) {
    return 0xFFu;
  }
  return range[y][x];
}

int AiCountUnitsInRange() {
  int count = 0;
  const MapSize size = GetMapSize();
  GbaPtr<GbaPtr<uint8_t>> range = MapRange();
  GbaPtr<GbaPtr<uint8_t>> unit_map = MapUnit();
  for (int y = size.y - 1; y >= 0; --y) {
    for (int x = size.x - 1; x >= 0; --x) {
      if (static_cast<int8_t>(range[y][x]) != 0 && unit_map[y][x] != 0u) {
        ++count;
      }
    }
  }
  return count;
}

int AiCountEnemiesInRange() {
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
      if (uid != 0u && !AreUnitIdsAllied(active_id, uid)) {
        ++count;
      }
    }
  }
  return count;
}

int AiCountAlliesInRange() {
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
        ++count;
      }
    }
  }
  return count;
}

int AiCountUnitsNearby(int16_t x, int16_t y) {
  int count = 0;
  GbaPtr<const AiPos> it(kAiNearbyOffsetTableAddr - 4u);
  if (it->x == 0x270F) {
    return 0;
  }
  const MapSize size = GetMapSize();
  GbaPtr<GbaPtr<uint8_t>> unit_map = MapUnit();
  do {
    it = it + 1;
    const int tx = static_cast<int>(x) + static_cast<int>(it->x);
    const int ty = static_cast<int>(y) + static_cast<int>(it->y);
    if (tx < size.x && ty < size.y && unit_map[ty][tx] != 0u) {
      ++count;
    }
  } while (it->x != 0x270F);
  return count;
}

int AiCountEnemiesNearby(int16_t x, int16_t y) {
  int count = 0;
  GbaPtr<const AiPos> it(kAiNearbyOffsetTableAddr - 4u);
  if (it->x == 0x270F) {
    return 0;
  }
  const MapSize size = GetMapSize();
  GbaPtr<GbaPtr<uint8_t>> unit_map = MapUnit();
  const uint8_t active_id = ActiveUnitId();
  do {
    it = it + 1;
    const int tx = static_cast<int>(x) + static_cast<int>(it->x);
    const int ty = static_cast<int>(y) + static_cast<int>(it->y);
    if (tx < size.x && ty < size.y) {
      const uint8_t uid = unit_map[ty][tx];
      if (uid != 0u && !AreUnitIdsAllied(active_id, uid)) {
        ++count;
      }
    }
  } while (it->x != 0x270F);
  return count;
}

int AiCountAlliesNearby(int16_t x, int16_t y) {
  int count = 0;
  GbaPtr<const AiPos> it(kAiNearbyOffsetTableAddr - 4u);
  if (it->x == 0x270F) {
    return 0;
  }
  const MapSize size = GetMapSize();
  GbaPtr<GbaPtr<uint8_t>> unit_map = MapUnit();
  const uint8_t active_id = ActiveUnitId();
  do {
    it = it + 1;
    const int tx = static_cast<int>(x) + static_cast<int>(it->x);
    const int ty = static_cast<int>(y) + static_cast<int>(it->y);
    if (tx < size.x && ty < size.y) {
      const uint8_t uid = unit_map[ty][tx];
      if (uid != 0u && AreUnitIdsAllied(active_id, uid)) {
        ++count;
      }
    }
  } while (it->x != 0x270F);
  return count;
}

bool AiIsWithinDangerThreshold(int x, int y, uint8_t threshold) {
  return MapOther()[y][x] <= threshold;
}

void AiUpdateFactionRecovery() {
  const uint8_t faction = *GbaPtr<const uint8_t>(kAiPlayStatusAddr + 0x0Fu);
  const int32_t count =
      GbaPtr<const int32_t>(kAiFactionUnitCountsAddr)[faction >> 6];
  for (int i = 0; i < count; ++i) {
    GbaPtr<Unit> unit = GetUnit(static_cast<int>(faction) + 1 + i);
    if (unit && unit->pinfo) {
      AiUpdateUnitRecoveryMode(unit);
    }
  }
}

bool AiUpdateUnitRecoveryMode(GbaPtr<Unit> unit) {
  const int max_hp = GetUnitMaxHp(ConstUnit(unit));
  const uint16_t hp_pct =
      max_hp != 0
          ? static_cast<uint16_t>((GetUnitCurrentHp(unit) * 100) / max_hp)
          : 0u;
  const uint32_t idx = static_cast<uint32_t>(unit->ai_config & 7u) * 4u;
  GbaPtr<const uint8_t> entry(kAiRecoveryThresholdTableAddr + idx);

  if ((unit->ai_flags & 1u) != 0u) {
    if (entry[0] > hp_pct) {
      return true;
    }
    unit->ai_flags = static_cast<uint8_t>(unit->ai_flags & 0xFEu);
    return false;
  }
  if (entry[1] > hp_pct) {
    unit->ai_flags = static_cast<uint8_t>(unit->ai_flags | 1u);
    return true;
  }
  return false;
}

GbaPtr<const uint8_t> AiGetClosestEscapePoint() {
  const int8_t chapter = *GbaPtr<const int8_t>(kAiPlayStatusAddr + 0x0Eu);
  const uint8_t faction = *GbaPtr<const uint8_t>(kAiPlayStatusAddr + 0x0Fu);

  GbaPtr<const uint8_t> list(0u);
  if (faction == 0x00u) {
    return GbaPtr<const uint8_t>(0u);
  }
  if (faction == 0x80u) {
    list =
        GbaPtr<const GbaPtr<const uint8_t>>(kAiEnemyEscapeTableAddr)[chapter];
  } else if (faction == 0x40u) {
    list = GbaPtr<const GbaPtr<const uint8_t>>(kAiNpcEscapeTableAddr)[chapter];
  }

  GbaPtr<GbaPtr<uint8_t>> mov = MapMovement();
  int best_cost = 0xFF;
  GbaPtr<const uint8_t> best_ptr(0u);
  for (; list[0] != 0xFFu; list = list + 4) {
    const uint8_t cost = mov[list[1]][list[0]];
    if (cost <= 0x78u && best_cost > static_cast<int8_t>(cost)) {
      best_cost = cost;
      best_ptr = list;
    }
  }
  return best_ptr;
}

bool AiCanEquipBestItem() {
  GbaPtr<const Unit> active = ConstUnit(ActiveUnit());
  if ((active->flags & kUnitFlagHasMoved) != 0u) {
    return false;
  }
  if (GbaPtr<const AiDecision>(kAiDecisionAddr)->action_id == 1u) {
    return false;
  }
  if (active->status == static_cast<uint8_t>(UnitStatus::kBerserk)) {
    return false;
  }
  return true;
}

uint32_t AiRankUnitItems(GbaPtr<uint16_t> out_ranks) {
  return RankUnitItemsImpl(out_ranks.get());
}

void AiComputeEnemyThreatAtTile(int x, int y, GbaPtr<uint16_t> out_ranged,
                                GbaPtr<uint16_t> out_melee,
                                GbaPtr<uint16_t> out_total) {
  ComputeEnemyThreatAtTileImpl(x, y, out_ranged.get(), out_melee.get(),
                               out_total.get());
}

void AiEquipBestItemWithMask(uint32_t mask, GbaPtr<const uint16_t> item_ranks) {
  EquipBestItemWithMaskImpl(mask, item_ranks.get());
}

void AiChooseBestItemToEquip(uint16_t ranged_threat, uint16_t melee_threat,
                             uint16_t total_threat,
                             GbaPtr<const uint16_t> item_ranks) {
  ChooseBestItemToEquipImpl(ranged_threat, melee_threat, total_threat,
                            item_ranks.get());
}

void AiUpdateUnitSpecialStaffFlags(uint16_t item) {
  const int iid = GetItemIid(item);
  GbaPtr<Unit> active = ActiveUnit();
  if (iid >= 0x4A && iid <= 0x4E) {
    active->ai_flags = static_cast<uint8_t>(active->ai_flags | 4u);
  } else if (iid == 0x56) {
    active->ai_flags = static_cast<uint8_t>(active->ai_flags | 0x10u);
  }
}

bool AiIsWithinMoveBirdsEye(GbaPtr<const Unit> unit, int x, int y) {
  const int mov = static_cast<int>(unit->bonus_mov) +
                  static_cast<int>(static_cast<int8_t>(unit->jinfo->base_mov));
  int dx = x - static_cast<int>(unit->x);
  if (dx < 0) {
    dx = -dx;
  }
  int dy = y - static_cast<int>(unit->y);
  if (dy < 0) {
    dy = -dy;
  }
  return mov >= (dx + dy);
}

int AiGetUnitAttackPower(GbaPtr<const Unit> unit, GbaPtr<uint16_t> out_weapon) {
  const uint16_t weapon = static_cast<uint16_t>(GetUnitEquippedWeapon(unit));
  *out_weapon = weapon;
  return GetUnitPower(unit) + GetItemMight(weapon);
}

bool AiTryDanceOrPlay(int x, int y) {
  GbaPtr<const Unit> active = ConstUnit(ActiveUnit());
  if ((UnitAttributes(*active) & 0x30u) == 0u) {
    return false;
  }

  GbaPtr<GbaPtr<uint8_t>> mov = MapMovement();
  MapFill(mov, -1);
  MapAddInRange(x, y, 1, 1);

  const MapSize size = GetMapSize();
  GbaPtr<GbaPtr<uint8_t>> unit_map = MapUnit();
  const uint8_t active_id = ActiveUnitId();

  int best_level = 0;
  uint8_t best_uid = 0u;

  for (int ty = size.y - 1; ty >= 0; --ty) {
    for (int tx = size.x - 1; tx >= 0; --tx) {
      if (mov[ty][tx] > 0x78u) {
        continue;
      }
      const uint8_t uid = unit_map[ty][tx];
      if (uid == 0u || !AreUnitIdsAllied(active_id, uid)) {
        continue;
      }
      GbaPtr<const Unit> target = ConstUnit(GetUnit(uid));
      if ((UnitAttributes(*target) & 0x30u) != 0u) {
        continue;
      }
      if (best_level < static_cast<int>(target->level)) {
        best_level = static_cast<uint8_t>(target->level);
        best_uid = uid;
      }
    }
  }

  if (best_level == 0) {
    return false;
  }

  AiSetDecision(static_cast<int16_t>(x), static_cast<int16_t>(y), 7u, best_uid,
                0u, 0u, 0u);
  return true;
}

void RegisterAiDecideFunctions() {
  RegisterNative(kAddrAiEquipBestItem, &ThunkAiEquipBestItem);
  RegisterNative(kAddrAiCompare, &ThunkAiCompare);
  RegisterNative(kAddrAiTryFindNearestUnitOfClass,
                 &ThunkAiTryFindNearestUnitOfClass);
  RegisterNative(kAddrAiTryMoveRandomly, &ThunkAiTryMoveRandomly);
  RegisterNative(kAddrAiGetMoveCostAt, &ThunkAiGetMoveCostAt);
  RegisterNative(kAddrAiCountUnitsInRange, &ThunkAiCountUnitsInRange);
  RegisterNative(kAddrAiCountEnemiesInRange, &ThunkAiCountEnemiesInRange);
  RegisterNative(kAddrAiCountAlliesInRange, &ThunkAiCountAlliesInRange);
  RegisterNative(kAddrAiCountUnitsNearby, &ThunkAiCountUnitsNearby);
  RegisterNative(kAddrAiCountEnemiesNearby, &ThunkAiCountEnemiesNearby);
  RegisterNative(kAddrAiCountAlliesNearby, &ThunkAiCountAlliesNearby);
  RegisterNative(kAddrAiIsWithinDangerThreshold,
                 &ThunkAiIsWithinDangerThreshold);
  RegisterNative(kAddrAiUpdateFactionRecovery, &ThunkAiUpdateFactionRecovery);
  RegisterNative(kAddrAiUpdateUnitRecoveryMode, &ThunkAiUpdateUnitRecoveryMode);
  RegisterNative(kAddrAiGetClosestEscapePoint, &ThunkAiGetClosestEscapePoint);
  RegisterNative(kAddrAiCanEquipBestItem, &ThunkAiCanEquipBestItem);
  RegisterNative(kAddrAiRankUnitItems, &ThunkAiRankUnitItems);
  RegisterNative(kAddrAiComputeEnemyThreatAtTile,
                 &ThunkAiComputeEnemyThreatAtTile);
  RegisterNative(kAddrAiEquipBestItemWithMask, &ThunkAiEquipBestItemWithMask);
  RegisterNative(kAddrAiChooseBestItemToEquip, &ThunkAiChooseBestItemToEquip);
  RegisterNative(kAddrAiUpdateUnitSpecialStaffFlags,
                 &ThunkAiUpdateUnitSpecialStaffFlags);
  RegisterNative(kAddrAiIsWithinMoveBirdsEye, &ThunkAiIsWithinMoveBirdsEye);
  RegisterNative(kAddrAiGetUnitAttackPower, &ThunkAiGetUnitAttackPower);
  RegisterNative(kAddrAiTryDanceOrPlay, &ThunkAiTryDanceOrPlay);
}

}  // namespace fe7
