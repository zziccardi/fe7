// FE7 US (AE7E) map range-overlay, movement-script, magic-seal, and danger-zone
// helpers.
//
// Implements 13 map-range functions (ROM 0x08019E5C..0x0801B214):
//   - Move script backtracking, reversal, and action application
//   - 1-tile BFS movement-map / working-map extension
//   - Diamond range fill (`MapSetInRange`) and Magic Seal marking
//   - Complete attack/staff range, standing reach range, and danger zone
//   - Map range blink color phase helpers

#ifndef FE7_GAME_MAP_MAP_RANGE_H_
#define FE7_GAME_MAP_MAP_RANGE_H_

#include <cstdint>

#include "game/map/trap.h"
#include "game/unit.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// Fixed FE7 US EWRAM addresses for map layers & working move script.
inline constexpr uint32_t kMapOtherAddr = 0x0202E3F4u;
inline constexpr uint32_t kWorkingMoveScriptAddr = 0x02033E00u;

// External item-reach helpers called via GbaCall().
inline constexpr uint32_t kAddrGetUnitWeaponReach = 0x08016EBCu;
inline constexpr uint32_t kAddrGetUnitStaffReach = 0x08016FCCu;

// FE7 US ROM addresses for the 13 decompiled map-range & move-script functions.
inline constexpr uint32_t kAddrBuildBestMoveScript = 0x08019E5Cu;
inline constexpr uint32_t kAddrReverseMoveScript = 0x0801A010u;
inline constexpr uint32_t kAddrApplyMoveScriptToAction = 0x0801A044u;
inline constexpr uint32_t kAddrMapFloodRangeExtend = 0x0801A0FCu;
inline constexpr uint32_t kAddrMapFloodPostProcess = 0x0801A1E8u;
inline constexpr uint32_t kAddrMapFillInRange = 0x0801A3D8u;
inline constexpr uint32_t kAddrBuildUnitCompleteAttackRange = 0x0801A4D4u;
inline constexpr uint32_t kAddrBuildUnitStandingRangeForReach = 0x0801AC60u;
inline constexpr uint32_t kAddrBuildUnitCompleteStaffRange = 0x0801AE10u;
inline constexpr uint32_t kAddrGenerateDangerZoneRange = 0x0801B008u;
inline constexpr uint32_t kAddrMapMarkMagicSealRange = 0x0801B148u;
inline constexpr uint32_t kAddrGetMapRangeBlinkColorA = 0x0801B1E4u;
inline constexpr uint32_t kAddrGetMapRangeBlinkColorB = 0x0801B214u;

// Backtracks from `(x, y)` to a 0-cost origin on `gWorkingMap`, breaking ties
// with `RandNext`, and writes the forward move script to `out_buf`.
// Matches FE7 US `BuildBestMoveScript` (0x08019E5C).
void BuildBestMoveScript(int x, int y, GbaPtr<uint8_t> out_buf);

// Reverses `[begin, end)` in-place and terminates with `0x04`.
// Matches FE7 US `func_0801A010` (0x0801A010).
void ReverseMoveScript(GbaPtr<uint8_t> begin, GbaPtr<uint8_t> end);

// Walks `gWorkingMoveScript` from `(x, y)`, updating `gActionData` coordinates
// and truncating if `unit` triggers a mine or bumps a hidden unit.
// Matches FE7 US `func_0801A044` (0x0801A044).
void ApplyMoveScriptToAction(GbaPtr<const Unit> unit, int x, int y);

// Extends `gMapMovement` by 1 cardinal tile into negative cells using
// `gMapFloodSt->unk_0b`, then increments `unk_0b`.
// Matches FE7 US `func_0801A0FC` (0x0801A0FC).
void MapFloodRangeExtend();

// Extends `gWorkingMap` by 1 cardinal tile into negative cells using
// `gMapFloodSt->unk_0b`, then increments `unk_0b`.
// Matches FE7 US `func_0801A1E8` (0x0801A1E8).
void MapFloodPostProcess();

// Sets every cell of `gWorkingMap` within Manhattan distance `range` of
// `(x, y)` to `static_cast<uint8_t>(value)`.
// Matches FE7 US `func_0801A3D8` (0x0801A3D8).
void MapFillInRange(int x, int y, int range, int value);

// Marks `gWorkingMap` with the complete weapon (+ ballista) attack range of
// `unit` across all reachable tiles in `gMapMovement`, then restores
// `gWorkingMap = gMapMovement`.
// Matches FE7 US `func_0801A4D4` (0x0801A4D4).
void BuildUnitCompleteAttackRange(GbaPtr<Unit> unit);

// Marks `gWorkingMap` with `unit`'s standing attack/staff range for `reach`.
// Matches FE7 US `BuildUnitStandingRangeForReach` (0x0801AC60).
void BuildUnitStandingRangeForReach(GbaPtr<Unit> unit, int reach);

// Marks `gWorkingMap` with the complete staff range of `unit` across all
// reachable tiles in `gMapMovement`.
// Matches FE7 US `BuildUnitCompleteStaffRange` (0x0801AE10).
void BuildUnitCompleteStaffRange(GbaPtr<Unit> unit);

// Fills `gMapRange` with the combined attack (or staff, if `magic_only`) range
// of all visible enemy units in the non-active faction.
// Matches FE7 US `func_0801B008` (0x0801B008).
void GenerateDangerZoneRange(bool magic_only);

// Marks a 10-tile diamond on `gWorkingMap` with `value` around every enemy unit
// (0x81..0xBF) possessing the Magic Seal attribute (0x04000000).
// Matches FE7 US `func_0801B148` (0x0801B148).
void MapMarkMagicSealRange(int value);

// Returns 3 on odd game-clock seconds and 2 on even seconds.
// Matches FE7 US `func_0801B1E4` (0x0801B1E4).
int GetMapRangeBlinkColorA();

// Returns 2 on odd game-clock seconds and 3 on even seconds.
// Matches FE7 US `func_0801B214` (0x0801B214).
int GetMapRangeBlinkColorB();

// Registers all 13 map-range functions into the native dispatch table.
void RegisterMapRangeFunctions();

}  // namespace fe7

#endif  // FE7_GAME_MAP_MAP_RANGE_H_
