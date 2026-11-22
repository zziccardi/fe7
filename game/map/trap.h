// FE7 US (AE7E) map traps, ballistas, light runes, obstacles, and fog/entity
// map refresh routines.
//
// Implements 27 functions (ROM 0x08019868..0x08019ABC and
// 0x0802BA48..0x0802C318): `Trap` lifecycle (`InitTraps`, `GetTrap`,
// `GetTrapAt`, `GetSpecificTrapAt`, `AddTrap`, `AddDamagingTrap`,
// `RemoveTrap`), specialized traps (fire, gas, arrow, trap6/8/9, map-change,
// light rune, obstacles), roof hiding (`UnitHideIfUnderRoof`), and Fog of War
// + entity map synchronization (`RefreshUnitsOnMaps`,
// `RefreshTorchlightOnFogMap`, `RefreshMinesOnHiddenMap`,
// `RefreshEntityMaps`).

#ifndef FE7_GAME_MAP_TRAP_H_
#define FE7_GAME_MAP_TRAP_H_

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "game/unit.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// Single map trap/obstacle/ballista/torch entry (`gTrapArray` at EWRAM
// 0x0203A518, 64 active slots + 1 terminator slot, 8 bytes each).
struct Trap {
  uint8_t x;        // 0x00
  uint8_t y;        // 0x01
  uint8_t type;     // 0x02: TrapType (0 = empty/end sentinel)
  uint8_t extra;    // 0x03: Obstacle HP / ballista ammo / map-change ID / range
  uint8_t data[4];  // 0x04..0x07: Countdown, interval, damage, direction
};

static_assert(sizeof(Trap) == 8);
static_assert(offsetof(Trap, type) == 2);
static_assert(offsetof(Trap, data) == 4);
static_assert(std::is_trivially_copyable_v<Trap>);

// Trap type IDs stored in `Trap::type` (offset 0x02).
enum class TrapType : uint8_t {
  kNone = 0,
  kBallista = 1,
  kObstacle = 2,
  kMapChange = 3,
  kFire = 4,
  kGas = 5,
  kTrap6 = 6,
  kArrow = 7,
  kTrap8 = 8,
  kTrap9 = 9,
  kTorchlight = 10,
  kMine = 11,
  kLightRune = 12,
};

// Fixed FE7 US EWRAM addresses for fog/hidden maps and the trap pool.
inline constexpr uint32_t kMapFogAddr = 0x0202E3ECu;
inline constexpr uint32_t kMapHiddenAddr = 0x0202E3F0u;
inline constexpr uint32_t kTrapArrayAddr = 0x0203A518u;
inline constexpr int kMaxTrapCount = 64;

// FE7 US ROM addresses for the 27 decompiled trap & fog/entity map functions.
inline constexpr uint32_t kAddrRefreshUnitsOnMaps = 0x08019868u;
inline constexpr uint32_t kAddrRefreshTorchlightOnFogMap = 0x08019A3Cu;
inline constexpr uint32_t kAddrRefreshMinesOnHiddenMap = 0x08019A68u;
inline constexpr uint32_t kAddrRefreshEntityMaps = 0x08019ABCu;
inline constexpr uint32_t kAddrInitTraps = 0x0802BA48u;
inline constexpr uint32_t kAddrGetTrapAt = 0x0802BA70u;
inline constexpr uint32_t kAddrGetSpecificTrapAt = 0x0802BA98u;
inline constexpr uint32_t kAddrAddTrap = 0x0802BACCu;
inline constexpr uint32_t kAddrAddDamagingTrap = 0x0802BAF4u;
inline constexpr uint32_t kAddrRemoveTrap = 0x0802BB10u;
inline constexpr uint32_t kAddrAddFireTrap = 0x0802BB24u;
inline constexpr uint32_t kAddrAddGasTrap = 0x0802BB40u;
inline constexpr uint32_t kAddrAddArrowTrap = 0x0802BB60u;
inline constexpr uint32_t kAddrAddTrap6 = 0x0802BB7Cu;
inline constexpr uint32_t kAddrAddTrap8 = 0x0802BB98u;
inline constexpr uint32_t kAddrAddTrap9 = 0x0802BBA8u;
inline constexpr uint32_t kAddrInitMapObstacles = 0x0802BBB8u;
inline constexpr uint32_t kAddrApplyLightRuneTerrain = 0x0802BC80u;
inline constexpr uint32_t kAddrGetTrapExtraAt = 0x0802BCBCu;
inline constexpr uint32_t kAddrAddMapChangeTrap = 0x0802BDD4u;
inline constexpr uint32_t kAddrRemoveMapChangeTrap = 0x0802BDE8u;
inline constexpr uint32_t kAddrUnitHideIfUnderRoof = 0x0802BE14u;
inline constexpr uint32_t kAddrTickDamagingTraps = 0x0802C158u;
inline constexpr uint32_t kAddrResetExpiredDamagingTraps = 0x0802C17Cu;
inline constexpr uint32_t kAddrAddLightRune = 0x0802C1E8u;
inline constexpr uint32_t kAddrApplyLightRuneTerrainAlt = 0x0802C2DCu;
inline constexpr uint32_t kAddrGetTrap = 0x0802C318u;

// --- Entity & Fog of War map synchronization ---
void RefreshUnitsOnMaps();
void RefreshTorchlightOnFogMap();
void RefreshMinesOnHiddenMap();
void RefreshEntityMaps();

// --- Trap lifecycle, queries, and specialized constructors ---
void InitTraps();
GbaPtr<Trap> GetTrap(int index);
GbaPtr<Trap> GetTrapAt(int x, int y);
GbaPtr<Trap> GetSpecificTrapAt(int x, int y, int type);
GbaPtr<Trap> AddTrap(int x, int y, int type, int extra);
GbaPtr<Trap> AddDamagingTrap(int x, int y, int type, int extra, int turn_delay,
                             int interval, int damage);
void RemoveTrap(GbaPtr<Trap> trap);
GbaPtr<Trap> AddFireTrap(int x, int y, int turn_delay, int interval);
GbaPtr<Trap> AddGasTrap(int x, int y, int facing, int turn_delay, int interval);
GbaPtr<Trap> AddArrowTrap(int x, int turn_delay, int interval);
GbaPtr<Trap> AddTrap6(int x, int y, int turn_delay, int interval);
GbaPtr<Trap> AddTrap8(int x, int y);
GbaPtr<Trap> AddTrap9(int x, int y, int extra);
void InitMapObstacles();
void ApplyLightRuneTerrain();
int GetTrapExtraAt(int x, int y);
void AddMapChangeTrap(int map_change_id);
void RemoveMapChangeTrap(int map_change_id);
void UnitHideIfUnderRoof(GbaPtr<Unit> unit);
void TickDamagingTraps();
void ResetExpiredDamagingTraps();
GbaPtr<Trap> AddLightRune(int x, int y);
void ApplyLightRuneTerrainAlt();

// Registers all 27 native trap & fog/entity map functions in the dispatch
// table.
void RegisterTrapFunctions();

}  // namespace fe7

#endif  // FE7_GAME_MAP_TRAP_H_
