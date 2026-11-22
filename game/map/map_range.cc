#include "game/map/map_range.h"

#include <cstdint>

#include "absl/base/nullability.h"
#include "game/action/action.h"
#include "game/engine/clock.h"
#include "game/engine/rng.h"
#include "game/item/item.h"
#include "game/map/ballista.h"
#include "game/map/map.h"
#include "game/phase/phase.h"
#include "game/unit.h"
#include "game/unit/unit.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

namespace {

inline GbaPtr<GbaPtr<uint8_t>> MapRows(uint32_t map_ptr_addr) {
  return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(map_ptr_addr);
}

inline uint8_t MapAt(GbaPtr<GbaPtr<uint8_t>> rows, int x, int y) {
  return rows[y][x];
}

inline uint8_t& MapCell(GbaPtr<GbaPtr<uint8_t>> rows, int x, int y) {
  return rows[y][x];
}

inline bool CanStandAt(int x, int y) {
  if (MapAt(MapRows(kMapMovementAddr), x, y) > 0x78u) {
    return false;
  }
  if (MapAt(MapRows(kMapUnitAddr), x, y) != 0u) {
    return false;
  }
  if (MapAt(MapRows(kMapOtherAddr), x, y) != 0u) {
    return false;
  }
  return true;
}

inline uint32_t UnitAttributes(GbaPtr<const Unit> unit) {
  return unit->pinfo->attributes | unit->jinfo->attributes;
}

void ExtendMapByOneTile(uint32_t map_ptr_addr) {
  const GbaPtr<const MapSize> sz(kMapSizeAddr);
  const GbaPtr<MapFloodState> st(kMapFloodStAddr);
  const GbaPtr<GbaPtr<uint8_t>> rows = MapRows(map_ptr_addr);

  for (int y = sz->y - 1; y >= 0; --y) {
    for (int x = sz->x - 1; x >= 0; --x) {
      const uint8_t val = rows[y][x];
      if (val > 0x78u) {
        continue;
      }
      if (static_cast<int32_t>(static_cast<int8_t>(val)) ==
          static_cast<int32_t>(st->unk_0b)) {
        continue;
      }
      if (x > 0 && static_cast<int8_t>(rows[y][x - 1]) < 0) {
        rows[y][x - 1] = st->unk_0b;
      }
      if (x < sz->x - 1 && static_cast<int8_t>(rows[y][x + 1]) < 0) {
        rows[y][x + 1] = st->unk_0b;
      }
      if (y > 0 && static_cast<int8_t>(rows[y - 1][x]) < 0) {
        rows[y - 1][x] = st->unk_0b;
      }
      if (y < sz->y - 1 && static_cast<int8_t>(rows[y + 1][x]) < 0) {
        rows[y + 1][x] = st->unk_0b;
      }
    }
  }
  st->unk_0b = static_cast<uint8_t>(st->unk_0b + 1u);
}

// ---------------------------------------------------------------------------
// Native dispatch thunks
// ---------------------------------------------------------------------------

uint32_t ThunkBuildBestMoveScript(uint32_t r0, uint32_t r1, uint32_t r2,
                                  uint32_t /*r3*/,
                                  const uint32_t* absl_nullable /*stack*/) {
  BuildBestMoveScript(static_cast<int32_t>(r0), static_cast<int32_t>(r1),
                      GbaPtr<uint8_t>(r2));
  return 0u;
}

uint32_t ThunkReverseMoveScript(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                                uint32_t /*r3*/,
                                const uint32_t* absl_nullable /*stack*/) {
  ReverseMoveScript(GbaPtr<uint8_t>(r0), GbaPtr<uint8_t>(r1));
  return 0u;
}

uint32_t ThunkApplyMoveScriptToAction(uint32_t r0, uint32_t r1, uint32_t r2,
                                      uint32_t /*r3*/,
                                      const uint32_t* absl_nullable /*stack*/) {
  ApplyMoveScriptToAction(GbaPtr<const Unit>(r0), static_cast<int32_t>(r1),
                          static_cast<int32_t>(r2));
  return 0u;
}

uint32_t ThunkMapFloodRangeExtend(uint32_t /*r0*/, uint32_t /*r1*/,
                                  uint32_t /*r2*/, uint32_t /*r3*/,
                                  const uint32_t* absl_nullable /*stack*/) {
  MapFloodRangeExtend();
  return 0u;
}

uint32_t ThunkMapFloodPostProcess(uint32_t /*r0*/, uint32_t /*r1*/,
                                  uint32_t /*r2*/, uint32_t /*r3*/,
                                  const uint32_t* absl_nullable /*stack*/) {
  MapFloodPostProcess();
  return 0u;
}

uint32_t ThunkMapFillInRange(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                             const uint32_t* absl_nullable /*stack*/) {
  MapFillInRange(static_cast<int32_t>(r0), static_cast<int32_t>(r1),
                 static_cast<int32_t>(r2), static_cast<int32_t>(r3));
  return 0u;
}

uint32_t ThunkBuildUnitCompleteAttackRange(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  BuildUnitCompleteAttackRange(GbaPtr<Unit>(r0));
  return 0u;
}

uint32_t ThunkBuildUnitStandingRangeForReach(
    uint32_t r0, uint32_t r1, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  BuildUnitStandingRangeForReach(GbaPtr<Unit>(r0), static_cast<int32_t>(r1));
  return 0u;
}

uint32_t ThunkBuildUnitCompleteStaffRange(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  BuildUnitCompleteStaffRange(GbaPtr<Unit>(r0));
  return 0u;
}

uint32_t ThunkGenerateDangerZoneRange(uint32_t r0, uint32_t /*r1*/,
                                      uint32_t /*r2*/, uint32_t /*r3*/,
                                      const uint32_t* absl_nullable /*stack*/) {
  GenerateDangerZoneRange((r0 & 0xFFu) != 0u);
  return 0u;
}

uint32_t ThunkMapMarkMagicSealRange(uint32_t r0, uint32_t /*r1*/,
                                    uint32_t /*r2*/, uint32_t /*r3*/,
                                    const uint32_t* absl_nullable /*stack*/) {
  MapMarkMagicSealRange(static_cast<int32_t>(r0));
  return 0u;
}

uint32_t ThunkGetMapRangeBlinkColorA(uint32_t /*r0*/, uint32_t /*r1*/,
                                     uint32_t /*r2*/, uint32_t /*r3*/,
                                     const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetMapRangeBlinkColorA());
}

uint32_t ThunkGetMapRangeBlinkColorB(uint32_t /*r0*/, uint32_t /*r1*/,
                                     uint32_t /*r2*/, uint32_t /*r3*/,
                                     const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetMapRangeBlinkColorB());
}

}  // namespace

// ---------------------------------------------------------------------------
// 0x08019E5C: BuildBestMoveScript
// ---------------------------------------------------------------------------
void BuildBestMoveScript(int x, int y, GbaPtr<uint8_t> out_buf) {
  const GbaPtr<const MapSize> sz(kMapSizeAddr);
  GbaPtr<uint8_t> cur = out_buf;
  uint8_t dir = 0u;

  while (static_cast<int8_t>(MapAt(MapRows(kWorkingMapAddr), x, y)) != 0) {
    uint8_t costs[4];
    const GbaPtr<GbaPtr<uint8_t>> rows = MapRows(kWorkingMapAddr);
    costs[0] = (x == sz->x - 1) ? 0xFFu : rows[y][x + 1];
    costs[1] = (x == 0) ? 0xFFu : rows[y][x - 1];
    costs[3] = (y == sz->y - 1) ? 0xFFu : rows[y + 1][x];
    costs[2] = (y == 0) ? 0xFFu : rows[y - 1][x];

    int min_cost = 0x100;
    for (int i = 0; i < 4; ++i) {
      if (costs[i] < min_cost) {
        min_cost = costs[i];
      }
    }

    uint8_t best_dirs[4];
    int16_t count = 0;
    for (int i = 0; i < 4; ++i) {
      if (costs[i] == min_cost) {
        best_dirs[count] = static_cast<uint8_t>(i);
        ++count;
      }
    }

    switch (count) {
      case 1:
        dir = best_dirs[0];
        break;
      case 2:
        dir = best_dirs[RandNext(2)];
        break;
      case 3:
        dir = best_dirs[RandNext(3)];
        break;
      case 4:
        dir = best_dirs[RandNext(4)];
        break;
      default:
        break;
    }

    *cur = dir;
    cur = GbaPtr<uint8_t>(cur.addr + 1u);

    switch (dir) {
      case 0u:
        ++x;
        break;
      case 1u:
        --x;
        break;
      case 2u:
        --y;
        break;
      case 3u:
        ++y;
        break;
      default:
        break;
    }
  }

  ReverseMoveScript(out_buf, cur);
}

// ---------------------------------------------------------------------------
// 0x0801A010: ReverseMoveScript
// ---------------------------------------------------------------------------
void ReverseMoveScript(GbaPtr<uint8_t> begin, GbaPtr<uint8_t> end) {
  uint8_t tmp[0x40];
  int len = 0;
  uint32_t it = end.addr;
  while (it > begin.addr) {
    --it;
    tmp[len++] = *GbaPtr<const uint8_t>(it);
  }
  tmp[len] = 4u;

  GbaPtr<uint8_t> dst = begin;
  int idx = 0;
  while (tmp[idx] != 4u) {
    *dst = tmp[idx];
    dst = GbaPtr<uint8_t>(dst.addr + 1u);
    ++idx;
  }
  *dst = 4u;
}

// ---------------------------------------------------------------------------
// 0x0801A044: ApplyMoveScriptToAction
// ---------------------------------------------------------------------------
void ApplyMoveScriptToAction(GbaPtr<const Unit> unit, int x, int y) {
  GbaPtr<uint8_t> script(kWorkingMoveScriptAddr);
  GbaPtr<uint8_t> act(kActionDataAddr);
  const GbaPtr<GbaPtr<uint8_t>> hidden = MapRows(kMapHiddenAddr);

  for (;;) {
    act[0x0E] = static_cast<uint8_t>(x);
    act[0x0F] = static_cast<uint8_t>(y);

    const uint8_t cmd = *script;
    switch (cmd) {
      case 0u:
        --x;
        break;
      case 1u:
        ++x;
        break;
      case 2u:
        ++y;
        break;
      case 3u:
        --y;
        break;
      default:
        break;
    }

    if ((UnitAttributes(unit) & 0x02001808u) == 0u &&
        (MapAt(hidden, x, y) & 2u) != 0u) {
      script[1] = 4u;
      act[0x11] = 0x1Bu;
      act[0x0E] = static_cast<uint8_t>(x);
      act[0x0F] = static_cast<uint8_t>(y);
      return;
    }

    if ((MapAt(hidden, x, y) & 1u) != 0u) {
      script[0] = 0x0Au;
      script[1] = 4u;
      act[0x11] = 0x1Bu;
      return;
    }

    if (*script == 4u) {
      return;
    }
    script = GbaPtr<uint8_t>(script.addr + 1u);
  }
}

// ---------------------------------------------------------------------------
// 0x0801A0FC: MapFloodRangeExtend
// ---------------------------------------------------------------------------
void MapFloodRangeExtend() { ExtendMapByOneTile(kMapMovementAddr); }

// ---------------------------------------------------------------------------
// 0x0801A1E8: MapFloodPostProcess
// ---------------------------------------------------------------------------
void MapFloodPostProcess() { ExtendMapByOneTile(kWorkingMapAddr); }

// ---------------------------------------------------------------------------
// 0x0801A3D8: MapFillInRange
// ---------------------------------------------------------------------------
void MapFillInRange(int x, int y, int range, int value) {
  const GbaPtr<const MapSize> sz(kMapSizeAddr);
  const GbaPtr<GbaPtr<uint8_t>> rows = MapRows(kWorkingMapAddr);
  const uint8_t byte_val = static_cast<uint8_t>(value);

  int span = range;
  for (int iy = y; iy <= y + range && iy < sz->y; ++iy) {
    int ix = x - span;
    int width = span * 2 + 1;
    if (ix < 0) {
      width += ix;
      ix = 0;
    }
    int end_x = ix + width;
    if (end_x > sz->x) {
      end_x = sz->x;
    }
    for (int cx = ix; cx < end_x; ++cx) {
      rows[iy][cx] = byte_val;
    }
    --span;
  }

  span = range - 1;
  for (int iy = y - 1; iy >= y - range && iy >= 0; --iy) {
    int ix = x - span;
    int width = span * 2 + 1;
    if (ix < 0) {
      width += ix;
      ix = 0;
    }
    int end_x = ix + width;
    if (end_x > sz->x) {
      end_x = sz->x;
    }
    for (int cx = ix; cx < end_x; ++cx) {
      rows[iy][cx] = byte_val;
    }
    --span;
  }
}

// ---------------------------------------------------------------------------
// 0x0801A4D4: BuildUnitCompleteAttackRange
// ---------------------------------------------------------------------------
void BuildUnitCompleteAttackRange(GbaPtr<Unit> unit) {
  const GbaPtr<const MapSize> sz(kMapSizeAddr);
  const int reach = static_cast<int32_t>(
      GbaCall(kAddrGetUnitWeaponReach, unit.addr, 0xFFFFFFFFu));

  auto apply_pair = [&](int max_r, int min_minus_1) {
    for (int y = sz->y - 1; y >= 0; --y) {
      for (int x = sz->x - 1; x >= 0; --x) {
        if (CanStandAt(x, y)) {
          MapAddInRange(static_cast<int16_t>(x), static_cast<int16_t>(y), max_r,
                        1);
          MapAddInRange(static_cast<int16_t>(x), static_cast<int16_t>(y),
                        min_minus_1, -1);
        }
      }
    }
  };

  auto apply_double = [&](int max_a, int min_a_m1, int max_b, int min_b_m1) {
    for (int y = sz->y - 1; y >= 0; --y) {
      for (int x = sz->x - 1; x >= 0; --x) {
        if (CanStandAt(x, y)) {
          MapAddInRange(static_cast<int16_t>(x), static_cast<int16_t>(y), max_a,
                        1);
          MapAddInRange(static_cast<int16_t>(x), static_cast<int16_t>(y),
                        min_a_m1, -1);
          MapAddInRange(static_cast<int16_t>(x), static_cast<int16_t>(y), max_b,
                        1);
          MapAddInRange(static_cast<int16_t>(x), static_cast<int16_t>(y),
                        min_b_m1, -1);
        }
      }
    }
  };

  switch (reach) {
    case 1:
      apply_pair(1, 0);
      break;
    case 2:
      apply_pair(2, 1);
      break;
    case 3:
      apply_pair(2, 0);
      break;
    case 4:
      apply_pair(3, 2);
      break;
    case 5:
      apply_double(1, 0, 3, 2);
      break;
    case 6:
      apply_pair(3, 1);
      break;
    case 7:
      apply_pair(3, 0);
      break;
    case 12:
      apply_pair(10, 2);
      break;
    case 13:
      apply_double(1, 0, 10, 2);
      break;
    case 15:
      apply_pair(10, 0);
      break;
    default:
      break;
  }

  if ((UnitAttributes(GbaPtr<const Unit>(unit.addr)) & 0x80u) != 0u) {
    for (int y = sz->y - 1; y >= 0; --y) {
      for (int x = sz->x - 1; x >= 0; --x) {
        if (!CanStandAt(x, y)) {
          continue;
        }
        const int item = GetBallistaItemAtPos(x, y);
        if (item != 0) {
          const int min_r = static_cast<int16_t>(
              GetItemMinRange(static_cast<uint16_t>(item)));
          const int max_r = static_cast<int16_t>(
              GetItemMaxRange(static_cast<uint16_t>(item)));
          MapAddInRange(static_cast<int16_t>(x), static_cast<int16_t>(y), max_r,
                        1);
          MapAddInRange(static_cast<int16_t>(x), static_cast<int16_t>(y),
                        min_r - 1, -1);
        }
      }
    }
  }

  SetWorkingMap(MapRows(kMapMovementAddr));
}

// ---------------------------------------------------------------------------
// 0x0801AC60: BuildUnitStandingRangeForReach
// ---------------------------------------------------------------------------
void BuildUnitStandingRangeForReach(GbaPtr<Unit> unit, int reach) {
  const int x = unit->x;
  const int y = unit->y;

  switch (reach) {
    case 1:
      MapAddInRange(x, y, 1, 1);
      MapAddInRange(x, y, 0, -1);
      break;
    case 2:
      MapAddInRange(x, y, 2, 1);
      MapAddInRange(x, y, 1, -1);
      break;
    case 3:
      MapAddInRange(x, y, 2, 1);
      MapAddInRange(x, y, 0, -1);
      break;
    case 4:
      MapAddInRange(x, y, 3, 1);
      MapAddInRange(x, y, 2, -1);
      break;
    case 5:
      MapAddInRange(x, y, 1, 1);
      MapAddInRange(x, y, 0, -1);
      MapAddInRange(x, y, 3, 1);
      MapAddInRange(x, y, 2, -1);
      break;
    case 6:
      MapAddInRange(x, y, 3, 1);
      MapAddInRange(x, y, 1, -1);
      break;
    case 7:
      MapAddInRange(x, y, 3, 1);
      MapAddInRange(x, y, 0, -1);
      break;
    case 12:
      MapAddInRange(x, y, 10, 1);
      MapAddInRange(x, y, 2, -1);
      break;
    case 13:
      MapAddInRange(x, y, 1, 1);
      MapAddInRange(x, y, 0, -1);
      MapAddInRange(x, y, 10, 1);
      MapAddInRange(x, y, 2, -1);
      break;
    case 15:
      MapAddInRange(x, y, 10, 1);
      MapAddInRange(x, y, 0, -1);
      break;
    case 32: {
      const int mag_r =
          static_cast<int16_t>(GetUnitMagRange(GbaPtr<const Unit>(unit.addr)));
      MapAddInRange(x, y, mag_r, 1);
      MapAddInRange(x, y, 0, -1);
      break;
    }
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// 0x0801AE10: BuildUnitCompleteStaffRange
// ---------------------------------------------------------------------------
void BuildUnitCompleteStaffRange(GbaPtr<Unit> unit) {
  const GbaPtr<const MapSize> sz(kMapSizeAddr);
  const int reach =
      static_cast<int32_t>(GbaCall(kAddrGetUnitStaffReach, unit.addr));
  const int mag_r =
      static_cast<int16_t>(GetUnitMagRange(GbaPtr<const Unit>(unit.addr)));

  int max_r = 0;
  if (reach == 1) {
    max_r = 1;
  } else if (reach == 3) {
    max_r = 2;
  } else if (reach == 0x20) {
    max_r = mag_r;
  } else {
    return;
  }

  for (int y = sz->y - 1; y >= 0; --y) {
    for (int x = sz->x - 1; x >= 0; --x) {
      if (CanStandAt(x, y)) {
        MapAddInRange(static_cast<int16_t>(x), static_cast<int16_t>(y), max_r,
                      1);
        MapAddInRange(static_cast<int16_t>(x), static_cast<int16_t>(y), 0, -1);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// 0x0801B008: GenerateDangerZoneRange
// ---------------------------------------------------------------------------
void GenerateDangerZoneRange(bool magic_only) {
  int prev_magic = -1;
  MapFill(MapRows(kMapRangeAddr), 0);

  const int faction = GetNonActiveFaction();
  for (int uid = faction + 1; uid < faction + 0x80; ++uid) {
    GbaPtr<Unit> unit = GetUnit(uid);
    if (!unit || !unit->pinfo) {
      continue;
    }
    if (magic_only && !UnitKnowsMagic(GbaPtr<const Unit>(unit.addr))) {
      continue;
    }
    if (*GbaPtr<const uint8_t>(kMapPlayStatusAddr + 0x0Du) != 0u &&
        MapAt(MapRows(kMapFogAddr), unit->x, unit->y) == 0u) {
      continue;
    }
    if ((unit->flags & 0x80u) != 0u) {
      continue;
    }

    const int mov =
        static_cast<int8_t>(unit->bonus_mov + unit->jinfo->base_mov);
    MapFloodUnitMovement(GbaPtr<const Unit>(unit.addr), mov);

    uint8_t& unit_cell = MapCell(MapRows(kMapUnitAddr), unit->x, unit->y);
    const uint8_t saved_uid = unit_cell;
    unit_cell = 0u;

    const int knows_magic =
        UnitKnowsMagic(GbaPtr<const Unit>(unit.addr)) ? 1 : 0;
    if (prev_magic != knows_magic) {
      MapFill(MapRows(kMapOtherAddr), 0);
      if (knows_magic != 0) {
        MapMarkMagicSealRange(1);
      }
      prev_magic = knows_magic;
    }

    SetWorkingMap(MapRows(kMapRangeAddr));
    if (magic_only) {
      BuildUnitCompleteStaffRange(unit);
    } else {
      BuildUnitCompleteAttackRange(unit);
    }

    unit_cell = saved_uid;
  }
}

// ---------------------------------------------------------------------------
// 0x0801B148: MapMarkMagicSealRange
// ---------------------------------------------------------------------------
void MapMarkMagicSealRange(int value) {
  for (int uid = 0x81; uid <= 0xBF; ++uid) {
    GbaPtr<Unit> unit = GetUnit(uid);
    if (!unit || !unit->pinfo) {
      continue;
    }
    if ((UnitAttributes(GbaPtr<const Unit>(unit.addr)) & 0x04000000u) != 0u) {
      MapFillInRange(unit->x, unit->y, 10, value);
    }
  }
}

// ---------------------------------------------------------------------------
// 0x0801B1E4: GetMapRangeBlinkColorA
// ---------------------------------------------------------------------------
int GetMapRangeBlinkColorA() {
  const uint32_t secs = (GetGameTime() / 60u) % 60u;
  return (secs & 1u) != 0u ? 3 : 2;
}

// ---------------------------------------------------------------------------
// 0x0801B214: GetMapRangeBlinkColorB
// ---------------------------------------------------------------------------
int GetMapRangeBlinkColorB() {
  const uint32_t secs = (GetGameTime() / 60u) % 60u;
  return (secs & 1u) != 0u ? 2 : 3;
}

void RegisterMapRangeFunctions() {
  RegisterNative(kAddrBuildBestMoveScript, &ThunkBuildBestMoveScript);
  RegisterNative(kAddrReverseMoveScript, &ThunkReverseMoveScript);
  RegisterNative(kAddrApplyMoveScriptToAction, &ThunkApplyMoveScriptToAction);
  RegisterNative(kAddrMapFloodRangeExtend, &ThunkMapFloodRangeExtend);
  RegisterNative(kAddrMapFloodPostProcess, &ThunkMapFloodPostProcess);
  RegisterNative(kAddrMapFillInRange, &ThunkMapFillInRange);
  RegisterNative(kAddrBuildUnitCompleteAttackRange,
                 &ThunkBuildUnitCompleteAttackRange);
  RegisterNative(kAddrBuildUnitStandingRangeForReach,
                 &ThunkBuildUnitStandingRangeForReach);
  RegisterNative(kAddrBuildUnitCompleteStaffRange,
                 &ThunkBuildUnitCompleteStaffRange);
  RegisterNative(kAddrGenerateDangerZoneRange, &ThunkGenerateDangerZoneRange);
  RegisterNative(kAddrMapMarkMagicSealRange, &ThunkMapMarkMagicSealRange);
  RegisterNative(kAddrGetMapRangeBlinkColorA, &ThunkGetMapRangeBlinkColorA);
  RegisterNative(kAddrGetMapRangeBlinkColorB, &ThunkGetMapRangeBlinkColorB);
}

}  // namespace fe7
