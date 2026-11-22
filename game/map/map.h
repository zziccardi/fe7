// FE7 US (AE7E) 2D tile-map helpers, range diamonds, and BFS movement flooding.
//
// Implements 17 core map & pathfinding functions (ROM 0x08004408 and
// 0x0801906C..0x0801B1DC): padded row-pointer map initialization (`MapInit`),
// fast fill (`MapFill`, `MapFillEdges`), Manhattan-diamond range marking
// (`MapAddInRange`, `MapSetInRange`), and the double-buffered BFS movement
// flood (`MapFloodCoreRam`, `BeginMapFlood`, `MapFloodUnit*`,
// `MapFloodExtended`, `MapFloodRangeUnitless`, `MapFloodOnWorkingMap`).

#ifndef FE7_GAME_MAP_MAP_H_
#define FE7_GAME_MAP_MAP_H_

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "game/unit.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// 2D 16-bit map dimensions (`gMapSize` at EWRAM 0x0202E3D8).
struct MapSize {
  int16_t x;
  int16_t y;
};

static_assert(sizeof(MapSize) == 4);
static_assert(std::is_trivially_copyable_v<MapSize>);

// Single BFS queue node in `gMapFloodSquareBufA` / `gMapFloodSquareBufB`.
struct MapFloodSquare {
  int8_t x;         // 0x00
  int8_t y;         // 0x01
  uint8_t connect;  // 0x02: Incoming direction (0..3, 4=end sentinel, 5=root)
  uint8_t cost;     // 0x03: Cumulative movement cost
};

static_assert(sizeof(MapFloodSquare) == 4);
static_assert(std::is_trivially_copyable_v<MapFloodSquare>);

// Active BFS flood state (`gMapFloodSt` at IWRAM 0x030046A0, 12 bytes).
struct MapFloodState {
  GbaPtr<MapFloodSquare> active_queue;  // 0x00: Current read cursor
  GbaPtr<MapFloodSquare> next_queue;    // 0x04: Next-ring write cursor
  int8_t has_move_limit;                // 0x08: Nonzero enables unit blocking
  uint8_t max_move;                     // 0x09: Maximum movement budget
  uint8_t mover_id;                     // 0x0A: Unit ID for faction check
  uint8_t unk_0b;                       // 0x0B: Initialized to 0x78
};

static_assert(sizeof(MapFloodState) == 12);
static_assert(std::is_trivially_copyable_v<MapFloodState>);

// Fixed FE7 US EWRAM/IWRAM addresses for map buffers and BFS state.
inline constexpr uint32_t kMapSizeAddr = 0x0202E3D8u;
inline constexpr uint32_t kMapUnitAddr = 0x0202E3DCu;
inline constexpr uint32_t kMapTerrainAddr = 0x0202E3E0u;
inline constexpr uint32_t kMapMovementAddr = 0x0202E3E4u;
inline constexpr uint32_t kMapRangeAddr = 0x0202E3E8u;
inline constexpr uint32_t kMapInitScratchAddr = 0x03000438u;
inline constexpr uint32_t kWorkingMapAddr = 0x030041E0u;
inline constexpr uint32_t kMapFloodBufBAddr = 0x030041F0u;
inline constexpr uint32_t kWorkingMovTableAddr = 0x030043F0u;
inline constexpr uint32_t kMapFloodBufAAddr = 0x03004490u;
inline constexpr uint32_t kMapFloodStAddr = 0x030046A0u;

// External helper ROM address called via GbaCall().
inline constexpr uint32_t kAddrGetUnitMovementCost = 0x080187D4u;

// FE7 US ROM addresses for the 17 decompiled map & pathfinding functions.
inline constexpr uint32_t kAddrMapFloodCoreRam = 0x08004408u;
inline constexpr uint32_t kAddrMapInit = 0x0801906Cu;
inline constexpr uint32_t kAddrMapFill = 0x080190ACu;
inline constexpr uint32_t kAddrMapFillEdges = 0x08019108u;
inline constexpr uint32_t kAddrMapFloodUnit = 0x08019BA0u;
inline constexpr uint32_t kAddrMapFloodUnitMovement = 0x08019BE0u;
inline constexpr uint32_t kAddrMapFloodUnitExtended = 0x08019C1Cu;
inline constexpr uint32_t kAddrMapFloodRangeUnitless = 0x08019C50u;
inline constexpr uint32_t kAddrMapFloodExtended = 0x08019C80u;
inline constexpr uint32_t kAddrMapFloodOnWorkingMap = 0x08019CB0u;
inline constexpr uint32_t kAddrSetWorkingMoveTable = 0x08019CE0u;
inline constexpr uint32_t kAddrBeginMapFlood = 0x08019D00u;
inline constexpr uint32_t kAddrMapFloodCoreStep = 0x08019D84u;
inline constexpr uint32_t kAddrMapAddInRange = 0x0801A2D4u;
inline constexpr uint32_t kAddrSetWorkingMap = 0x0801B190u;
inline constexpr uint32_t kAddrMapSetInRange = 0x0801B19Cu;
inline constexpr uint32_t kAddrGetWorkingMoveTable = 0x0801B1DCu;

// Builds a padded row-pointer table in `buffer` for a `width x height` map and
// writes `row_ptrs + 2` into `*map_out`. Matches FE7 US `MapInit` (0x0801906C).
void MapInit(GbaPtr<uint8_t> buffer, GbaPtr<GbaPtr<GbaPtr<uint8_t>>> map_out,
             int width, int height);

// Fills all tiles (and padding rows) of `map` with `value` and sets
// `gWorkingMap = map`. Matches FE7 US `MapFill` (0x080190AC).
void MapFill(GbaPtr<GbaPtr<uint8_t>> map, int value);

// Fills the 1-tile outer perimeter of `map` with `value`.
// Matches FE7 US `func_08019108`.
void MapFillEdges(GbaPtr<GbaPtr<uint8_t>> map, uint8_t value);

// Sets `gWorkingMap = map`. Matches FE7 US `func_0801B190`.
void SetWorkingMap(GbaPtr<GbaPtr<uint8_t>> map);

// Copies 65 terrain movement costs into `gWorkingMovTable` (0x030043F0).
// Matches FE7 US `SetWorkingMoveTable` (0x08019CE0).
void SetWorkingMoveTable(GbaPtr<const int8_t> mov_table);

// Returns `GbaPtr<int8_t>(kWorkingMovTableAddr)`.
// Matches FE7 US `func_0801B1DC`.
GbaPtr<int8_t> GetWorkingMoveTable();

// Expands one neighbor `(cur.x + dx, cur.y + dy)` during BFS flooding.
// Matches FE7 US `func_08019D84` / ARM `MapFloodCoreStep`.
void MapFloodCoreStep(int connect, int dx, int dy);

// Runs the double-buffered BFS movement flood over `gMapFloodSt`.
// Matches FE7 US `MapFloodCoreRam` (0x08004408) / ARM `MapFloodCore`.
void MapFloodCoreRam();

// Initializes `gMapFloodSt`, fills `gWorkingMap` with 0xFF, seeds `(x, y)`,
// and runs `MapFloodCoreRam()`. Matches FE7 US `BeginMapFlood` (0x08019D00).
void BeginMapFlood(int x, int y, int movement, int unit_id);

// Convenience flood wrappers for units and terrain cost tables.
void MapFloodUnit(GbaPtr<const Unit> unit);
void MapFloodUnitMovement(GbaPtr<const Unit> unit, int movement);
void MapFloodUnitExtended(GbaPtr<const Unit> unit);
void MapFloodRangeUnitless(int x, int y, GbaPtr<const int8_t> mov_table);
void MapFloodExtended(int x, int y, GbaPtr<const int8_t> mov_table);
void MapFloodOnWorkingMap(GbaPtr<const Unit> unit, int x, int y, int movement);

// Adds `value` to all tiles within Manhattan distance `range` of `(x, y)` on
// `gWorkingMap`. Matches FE7 US `MapAddInRange` (0x0801A2D4).
void MapAddInRange(int x, int y, int range, int value);

// Marks tiles in the ring `[min_range, max_range]` around `(x, y)` on
// `gWorkingMap`. Matches FE7 US `func_0801B19C`.
void MapSetInRange(int x, int y, int min_range, int max_range);

// Play-status EWRAM address (weather at +0x15) and ROM ballista movement table.
inline constexpr uint32_t kMapPlayStatusAddr = 0x0202BBF8u;
inline constexpr uint32_t kBallistaMoveTableAddr = 0x08BE3C98u;

// Returns the 65-entry terrain movement cost table for `unit` accounting for
// ballista state and active chapter weather. Matches `GetUnitMovementCost`
// (0x080187D4).
GbaPtr<const int8_t> GetUnitMovementCost(GbaPtr<const Unit> unit);

// Registers all 18 native map & pathfinding functions in the dispatch table.
void RegisterMapFunctions();

}  // namespace fe7

#endif  // FE7_GAME_MAP_MAP_H_
