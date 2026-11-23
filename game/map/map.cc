#include "game/map/map.h"

#include <cstdint>
#include <cstring>

#include "absl/base/nullability.h"
#include "game/unit.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

uint32_t ThunkMapFloodCoreRam(uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/,
                              uint32_t /*r3*/,
                              const uint32_t* absl_nullable /*stack*/) {
  MapFloodCoreRam();
  return 0u;
}

uint32_t ThunkMapInit(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                      const uint32_t* absl_nullable /*stack*/) {
  MapInit(GbaPtr<uint8_t>(r0), GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(r1),
          static_cast<int>(r2), static_cast<int>(r3));
  return 0u;
}

uint32_t ThunkMapFill(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                      uint32_t /*r3*/,
                      const uint32_t* absl_nullable /*stack*/) {
  MapFill(GbaPtr<GbaPtr<uint8_t>>(r0), static_cast<int>(r1));
  return 0u;
}

uint32_t ThunkMapFillEdges(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                           uint32_t /*r3*/,
                           const uint32_t* absl_nullable /*stack*/) {
  MapFillEdges(GbaPtr<GbaPtr<uint8_t>>(r0), static_cast<uint8_t>(r1 & 0xFFu));
  return 0u;
}

uint32_t ThunkMapFloodUnit(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                           uint32_t /*r3*/,
                           const uint32_t* absl_nullable /*stack*/) {
  MapFloodUnit(GbaPtr<const Unit>(r0));
  return 0u;
}

uint32_t ThunkMapFloodUnitMovement(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                                   uint32_t /*r3*/,
                                   const uint32_t* absl_nullable /*stack*/) {
  MapFloodUnitMovement(GbaPtr<const Unit>(r0),
                       static_cast<int>(static_cast<int8_t>(r1 & 0xFFu)));
  return 0u;
}

uint32_t ThunkMapFloodUnitExtended(uint32_t r0, uint32_t /*r1*/,
                                   uint32_t /*r2*/, uint32_t /*r3*/,
                                   const uint32_t* absl_nullable /*stack*/) {
  MapFloodUnitExtended(GbaPtr<const Unit>(r0));
  return 0u;
}

uint32_t ThunkMapFloodRangeUnitless(uint32_t r0, uint32_t r1, uint32_t r2,
                                    uint32_t /*r3*/,
                                    const uint32_t* absl_nullable /*stack*/) {
  MapFloodRangeUnitless(static_cast<int>(r0), static_cast<int>(r1),
                        GbaPtr<const int8_t>(r2));
  return 0u;
}

uint32_t ThunkMapFloodExtended(uint32_t r0, uint32_t r1, uint32_t r2,
                               uint32_t /*r3*/,
                               const uint32_t* absl_nullable /*stack*/) {
  MapFloodExtended(static_cast<int>(r0), static_cast<int>(r1),
                   GbaPtr<const int8_t>(r2));
  return 0u;
}

uint32_t ThunkMapFloodOnWorkingMap(uint32_t r0, uint32_t r1, uint32_t r2,
                                   uint32_t r3,
                                   const uint32_t* absl_nullable /*stack*/) {
  MapFloodOnWorkingMap(GbaPtr<const Unit>(r0), static_cast<int>(r1),
                       static_cast<int>(r2), static_cast<int>(r3));
  return 0u;
}

uint32_t ThunkSetWorkingMoveTable(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                                  uint32_t /*r3*/,
                                  const uint32_t* absl_nullable /*stack*/) {
  SetWorkingMoveTable(GbaPtr<const int8_t>(r0));
  return 0u;
}

uint32_t ThunkBeginMapFlood(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                            const uint32_t* absl_nullable /*stack*/) {
  BeginMapFlood(static_cast<int>(r0), static_cast<int>(r1),
                static_cast<int>(r2), static_cast<int>(r3));
  return 0u;
}

uint32_t ThunkMapFloodCoreStep(uint32_t r0, uint32_t r1, uint32_t r2,
                               uint32_t /*r3*/,
                               const uint32_t* absl_nullable /*stack*/) {
  MapFloodCoreStep(static_cast<int>(r0), static_cast<int>(r1),
                   static_cast<int>(r2));
  return 0u;
}

uint32_t ThunkMapAddInRange(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                            const uint32_t* absl_nullable /*stack*/) {
  MapAddInRange(static_cast<int>(r0), static_cast<int>(r1),
                static_cast<int>(r2), static_cast<int>(r3));
  return 0u;
}

uint32_t ThunkSetWorkingMap(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                            uint32_t /*r3*/,
                            const uint32_t* absl_nullable /*stack*/) {
  SetWorkingMap(GbaPtr<GbaPtr<uint8_t>>(r0));
  return 0u;
}

uint32_t ThunkMapSetInRange(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                            const uint32_t* absl_nullable /*stack*/) {
  MapSetInRange(static_cast<int>(static_cast<int16_t>(r0)),
                static_cast<int>(static_cast<int16_t>(r1)),
                static_cast<int>(static_cast<int16_t>(r2)),
                static_cast<int>(static_cast<int16_t>(r3)));
  return 0u;
}

uint32_t ThunkGetWorkingMoveTable(uint32_t /*r0*/, uint32_t /*r1*/,
                                  uint32_t /*r2*/, uint32_t /*r3*/,
                                  const uint32_t* absl_nullable /*stack*/) {
  return GetWorkingMoveTable().addr;
}

}  // namespace

void MapInit(GbaPtr<uint8_t> buffer, GbaPtr<GbaPtr<GbaPtr<uint8_t>>> map_out,
             int width, int height) {
  // Mirror FE7 US's scratch pointer at IWRAM 0x03000438 for byte-exact parity.
  *GbaPtr<uint32_t>(kMapInitScratchAddr) = buffer.addr;
  const int stride = width + 2;
  const int rows = height + 4;
  GbaPtr<GbaPtr<uint8_t>> row_ptrs(buffer.addr);
  GbaPtr<uint8_t> tile_cursor = buffer + static_cast<ptrdiff_t>(rows * 4);
  for (int y = 0; y < rows; ++y) {
    row_ptrs[y] = tile_cursor;
    tile_cursor = tile_cursor + stride;
  }
  // Offset by +2 rows so `map[-2]` and `map[-1]` act as top border sentinels.
  *map_out = row_ptrs + 2;
}

void MapFill(GbaPtr<GbaPtr<uint8_t>> map, int value) {
  const MapSize size = *GbaPtr<const MapSize>(kMapSizeAddr);
  int byte_count = (size.y + 4) * (size.x + 2);
  // FE7 US uses 16-bit CpuSet fill after rounding odd sizes down by 1 byte.
  if ((byte_count & 1) != 0) {
    byte_count -= 1;
  }
  const GbaPtr<uint8_t> raw_start = map[-2];
  if (byte_count > 0) {
    std::memset(raw_start.get(), value & 0xFF, static_cast<size_t>(byte_count));
  }
  SetWorkingMap(map);
}

void MapFillEdges(GbaPtr<GbaPtr<uint8_t>> map, uint8_t value) {
  const MapSize size = *GbaPtr<const MapSize>(kMapSizeAddr);
  for (int y = 0; y < size.y; ++y) {
    GbaPtr<uint8_t> row = map[y];
    row[0] = value;
    row[size.x - 1] = value;
  }
  for (int x = 0; x < size.x; ++x) {
    map[0][x] = value;
    map[size.y - 1][x] = value;
  }
}

void SetWorkingMap(GbaPtr<GbaPtr<uint8_t>> map) {
  *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kWorkingMapAddr) = map;
}

void SetWorkingMoveTable(GbaPtr<const int8_t> mov_table) {
  GbaPtr<uint8_t> dst(kWorkingMovTableAddr);
  GbaPtr<const uint8_t> src(mov_table.addr);
  for (int i = 0; i <= 0x40; ++i) {
    dst[i] = src[i];
  }
}

GbaPtr<int8_t> GetWorkingMoveTable() {
  return GbaPtr<int8_t>(kWorkingMovTableAddr);
}

void MapFloodCoreStep(int connect, int dx, int dy) {
  GbaPtr<MapFloodState> st(kMapFloodStAddr);
  const GbaPtr<MapFloodSquare> cur = st->active_queue;
  const int nx = static_cast<int>(cur->x) + dx;
  const int ny = static_cast<int>(cur->y) + dy;

  const GbaPtr<GbaPtr<uint8_t>> terrain_map =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr);
  const GbaPtr<GbaPtr<uint8_t>> work_map =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kWorkingMapAddr);
  const GbaPtr<const uint8_t> mov_table(kWorkingMovTableAddr);

  const uint8_t terrain = terrain_map[ny][nx];
  const int cur_cost = static_cast<int>(static_cast<int8_t>(
      work_map[static_cast<uint8_t>(cur->y)][static_cast<uint8_t>(cur->x)]));
  const int16_t new_cost =
      static_cast<int16_t>(cur_cost + static_cast<int>(mov_table[terrain]));

  if (new_cost >= static_cast<int>(work_map[ny][nx])) {
    return;
  }

  if (st->has_move_limit != 0) {
    const GbaPtr<GbaPtr<uint8_t>> unit_map =
        *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr);
    const uint8_t occupant = unit_map[ny][nx];
    // Faction bit 7 separates player/NPC (0x00/0x40) from enemies (0x80).
    if (occupant != 0u && ((occupant ^ st->mover_id) & 0x80u) != 0u) {
      return;
    }
  }

  if (new_cost > static_cast<int>(st->max_move)) {
    return;
  }

  const GbaPtr<MapFloodSquare> dst = st->next_queue;
  dst->x = static_cast<int8_t>(nx);
  dst->y = static_cast<int8_t>(ny);
  dst->connect = static_cast<uint8_t>(connect);
  dst->cost = static_cast<uint8_t>(new_cost);
  st->next_queue = dst + 1;
  work_map[ny][nx] = static_cast<uint8_t>(new_cost);
}

void MapFloodCoreRam() {
  GbaPtr<MapFloodState> st(kMapFloodStAddr);
  int toggle = 0;
  for (;;) {
    toggle ^= 1;
    if (toggle != 0) {
      st->active_queue = GbaPtr<MapFloodSquare>(kMapFloodBufBAddr);
      st->next_queue = GbaPtr<MapFloodSquare>(kMapFloodBufAAddr);
    } else {
      st->active_queue = GbaPtr<MapFloodSquare>(kMapFloodBufAAddr);
      st->next_queue = GbaPtr<MapFloodSquare>(kMapFloodBufBAddr);
    }

    if (st->active_queue->connect == 4u) {
      return;
    }

    while (st->active_queue->connect != 4u) {
      const uint8_t dir = st->active_queue->connect;
      // Expand cardinal neighbors, skipping the direction we entered from to
      // avoid redundant backtracking (matching ARM MapFloodCore 0x08000850).
      switch (dir) {
        case 0u:  // Came from east (moved left) -> expand N, S, W
          MapFloodCoreStep(3, 0, -1);
          MapFloodCoreStep(2, 0, +1);
          MapFloodCoreStep(0, -1, 0);
          break;
        case 1u:  // Came from west (moved right) -> expand N, S, E
          MapFloodCoreStep(3, 0, -1);
          MapFloodCoreStep(2, 0, +1);
          MapFloodCoreStep(1, +1, 0);
          break;
        case 2u:  // Came from north (moved down) -> expand S, W, E
          MapFloodCoreStep(2, 0, +1);
          MapFloodCoreStep(0, -1, 0);
          MapFloodCoreStep(1, +1, 0);
          break;
        case 3u:  // Came from south (moved up) -> expand N, W, E
          MapFloodCoreStep(3, 0, -1);
          MapFloodCoreStep(0, -1, 0);
          MapFloodCoreStep(1, +1, 0);
          break;
        default:  // Root node (5) -> expand all 4 directions
          MapFloodCoreStep(3, 0, -1);
          MapFloodCoreStep(2, 0, +1);
          MapFloodCoreStep(0, -1, 0);
          MapFloodCoreStep(1, +1, 0);
          break;
      }
      st->next_queue->connect = 4u;
      st->active_queue = st->active_queue + 1;
    }
  }
}

void BeginMapFlood(int x, int y, int movement, int unit_id) {
  GbaPtr<MapFloodState> st(kMapFloodStAddr);
  st->next_queue = GbaPtr<MapFloodSquare>(kMapFloodBufBAddr);
  st->active_queue = GbaPtr<MapFloodSquare>(kMapFloodBufAAddr);
  st->max_move = static_cast<uint8_t>(movement);
  if (unit_id == 0) {
    st->has_move_limit = 0;
  } else {
    st->has_move_limit = 1;
    st->mover_id = static_cast<uint8_t>(unit_id);
  }
  st->unk_0b = 0x78u;

  const GbaPtr<GbaPtr<uint8_t>> work_map =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kWorkingMapAddr);
  MapFill(work_map, -1);

  const GbaPtr<MapFloodSquare> seed = st->next_queue;
  seed->x = static_cast<int8_t>(x);
  seed->y = static_cast<int8_t>(y);
  seed->connect = 5u;
  seed->cost = 0u;
  work_map[y][x] = 0u;

  st->next_queue = seed + 1;
  st->next_queue->connect = 4u;
  MapFloodCoreRam();
}

GbaPtr<const int8_t> GetUnitMovementCost(GbaPtr<const Unit> unit) {
  if ((unit->flags & kUnitFlagInBallista) != 0u) {
    return GbaPtr<const int8_t>(kBallistaMoveTableAddr);
  }
  const uint8_t weather = *GbaPtr<const uint8_t>(kMapPlayStatusAddr + 0x15u);
  if (weather == 1u || weather == 2u) {
    return unit->jinfo->mov_table[2];
  }
  if (weather == 4u) {
    return unit->jinfo->mov_table[1];
  }
  return unit->jinfo->mov_table[0];
}

void MapFloodUnit(GbaPtr<const Unit> unit) {
  SetWorkingMoveTable(GetUnitMovementCost(unit));
  SetWorkingMap(*GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr));
  BeginMapFlood(unit->x, unit->y, unit->jinfo->base_mov + unit->bonus_mov,
                unit->id);
}

void MapFloodUnitMovement(GbaPtr<const Unit> unit, int movement) {
  SetWorkingMoveTable(GetUnitMovementCost(unit));
  SetWorkingMap(*GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr));
  BeginMapFlood(unit->x, unit->y, movement, unit->id);
}

void MapFloodUnitExtended(GbaPtr<const Unit> unit) {
  SetWorkingMoveTable(GetUnitMovementCost(unit));
  SetWorkingMap(*GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr));
  BeginMapFlood(unit->x, unit->y, 0x7C, 0);
}

void MapFloodRangeUnitless(int x, int y, GbaPtr<const int8_t> mov_table) {
  SetWorkingMoveTable(mov_table);
  SetWorkingMap(*GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapRangeAddr));
  BeginMapFlood(x, y, 0x7C, 0);
}

void MapFloodExtended(int x, int y, GbaPtr<const int8_t> mov_table) {
  SetWorkingMoveTable(mov_table);
  SetWorkingMap(*GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr));
  BeginMapFlood(x, y, 0x7C, 0);
}

void MapFloodOnWorkingMap(GbaPtr<const Unit> unit, int x, int y, int movement) {
  SetWorkingMoveTable(GetUnitMovementCost(unit));
  BeginMapFlood(x, y, movement, unit->id);
}

void MapAddInRange(int x, int y, int range, int value) {
  const MapSize size = *GbaPtr<const MapSize>(kMapSizeAddr);
  const GbaPtr<GbaPtr<uint8_t>> work_map =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kWorkingMapAddr);

  // Lower half of diamond (including center row `y`).
  int span = range;
  for (int iy = y; iy <= y + range && iy < size.y; ++iy, --span) {
    int x_min = x - span;
    int count = span * 2 + 1;
    if (x_min < 0) {
      count += x_min;
      x_min = 0;
    }
    int x_end = x_min + count;
    if (x_end > size.x) {
      x_end = size.x;
    }
    GbaPtr<uint8_t> row = work_map[iy];
    for (int ix = x_min; ix < x_end; ++ix) {
      row[ix] = static_cast<uint8_t>(row[ix] + value);
    }
  }

  // Upper half of diamond (`y - 1` up to `y - range`).
  span = range - 1;
  for (int iy = y - 1; iy >= y - range && iy >= 0; --iy, --span) {
    int x_min = x - span;
    int count = span * 2 + 1;
    if (x_min < 0) {
      count += x_min;
      x_min = 0;
    }
    int x_end = x_min + count;
    if (x_end > size.x) {
      x_end = size.x;
    }
    GbaPtr<uint8_t> row = work_map[iy];
    for (int ix = x_min; ix < x_end; ++ix) {
      row[ix] = static_cast<uint8_t>(row[ix] + value);
    }
  }
}

void MapSetInRange(int x, int y, int min_range, int max_range) {
  MapAddInRange(x, y, max_range, 1);
  MapAddInRange(x, y, min_range - 1, -1);
}

uint8_t GetBaseTerrainAt(int x, int y) {
  const GbaPtr<const GbaPtr<const uint16_t>> raw_rows(kMapRawTilesAddr);
  const uint16_t tile = raw_rows[y][x];
  const GbaPtr<const uint8_t> metatile_terrain(kMapMetatileTerrainAddr);
  return metatile_terrain[tile >> 2];
}

void RegisterMapFunctions() {
  RegisterNative(kAddrGetUnitMovementCost,
                 [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return GetUnitMovementCost(GbaPtr<const Unit>(r0)).addr;
                 });
  RegisterNative(kAddrGetBaseTerrainAt,
                 [](uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return GetBaseTerrainAt(static_cast<int32_t>(r0),
                                           static_cast<int32_t>(r1));
                 });
  RegisterNative(kAddrMapFloodCoreRam, &ThunkMapFloodCoreRam);
  RegisterNative(kAddrMapInit, &ThunkMapInit);
  RegisterNative(kAddrMapFill, &ThunkMapFill);
  RegisterNative(kAddrMapFillEdges, &ThunkMapFillEdges);
  RegisterNative(kAddrMapFloodUnit, &ThunkMapFloodUnit);
  RegisterNative(kAddrMapFloodUnitMovement, &ThunkMapFloodUnitMovement);
  RegisterNative(kAddrMapFloodUnitExtended, &ThunkMapFloodUnitExtended);
  RegisterNative(kAddrMapFloodRangeUnitless, &ThunkMapFloodRangeUnitless);
  RegisterNative(kAddrMapFloodExtended, &ThunkMapFloodExtended);
  RegisterNative(kAddrMapFloodOnWorkingMap, &ThunkMapFloodOnWorkingMap);
  RegisterNative(kAddrSetWorkingMoveTable, &ThunkSetWorkingMoveTable);
  RegisterNative(kAddrBeginMapFlood, &ThunkBeginMapFlood);
  RegisterNative(kAddrMapFloodCoreStep, &ThunkMapFloodCoreStep);
  RegisterNative(kAddrMapAddInRange, &ThunkMapAddInRange);
  RegisterNative(kAddrSetWorkingMap, &ThunkSetWorkingMap);
  RegisterNative(kAddrMapSetInRange, &ThunkMapSetInRange);
  RegisterNative(kAddrGetWorkingMoveTable, &ThunkGetWorkingMoveTable);
}

}  // namespace fe7
