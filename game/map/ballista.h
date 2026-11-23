// FE7 US (AE7E) ballista trap query, creation, and mount/dismount helpers.
//
// Implements 8 functions (ROM 0x08034698..0x080347F8): `GetBallistaAt`,
// `GetBallistaItemAtPos`, `GetBallistaItemAtIgnoreUses`, `AddBallista`,
// `UnitRideBallista`, `TryRemoveUnitFromBallista`, `IsBallistaTrap`, and
// `GetBallistaItemFromTrap`.

#ifndef FE7_GAME_MAP_BALLISTA_H_
#define FE7_GAME_MAP_BALLISTA_H_

#include <cstdint>

#include "game/map/trap.h"
#include "game/unit.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// FE7 US ROM addresses for the 8 decompiled ballista trap helpers.
inline constexpr uint32_t kAddrGetBallistaAt = 0x08034698u;
inline constexpr uint32_t kAddrGetBallistaItemAtPos = 0x080346C8u;
inline constexpr uint32_t kAddrGetBallistaItemAtIgnoreUses = 0x08034710u;
inline constexpr uint32_t kAddrAddBallista = 0x08034740u;
inline constexpr uint32_t kAddrUnitRideBallista = 0x08034770u;
inline constexpr uint32_t kAddrTryRemoveUnitFromBallista = 0x080347A8u;
inline constexpr uint32_t kAddrIsBallistaTrap = 0x080347E4u;
inline constexpr uint32_t kAddrGetBallistaItemFromTrap = 0x080347F8u;

// Returns the ballista `Trap` at `(x, y)` if it has remaining ammo (`data[2]`),
// else null. Matches `func_08034698`.
GbaPtr<Trap> GetBallistaAt(int x, int y);

// Returns the packed `(ammo << 8) | iid` ballista item at `(x, y)` if it has
// ammo, else 0. Matches `func_080346C8`.
int GetBallistaItemAtPos(int x, int y);

// Returns `0x100 + iid` for any ballista at `(x, y)` with nonzero `extra`
// (ignoring remaining ammo), else 0. Matches `func_08034710`.
int GetBallistaItemAtIgnoreUses(int x, int y);

// Spawns a new ballista trap at `(x, y)` with the given item ID and full uses.
// Matches `func_08034740`.
GbaPtr<Trap> AddBallista(int x, int y, int item);

// Mounts `unit` onto the ballista trap at `(unit->x, unit->y)` and refreshes
// unit sprites. Matches `func_08034770`.
void UnitRideBallista(GbaPtr<Unit> unit);

// Dismounts `unit` from its ballista (if `kUnitFlagInBallista` is set),
// syncing the trap's coordinates and refreshing unit sprites. Matches
// `func_080347A8`.
void TryRemoveUnitFromBallista(GbaPtr<Unit> unit);

// Returns true if `trap` is non-null and has `type == TrapType::kBallista`.
// Matches `func_080347E4`.
bool IsBallistaTrap(GbaPtr<const Trap> trap);

// Returns the packed `(ammo << 8) | iid` item from `trap` if it is a ballista,
// else 0. Matches `func_080347F8`.
int GetBallistaItemFromTrap(GbaPtr<const Trap> trap);

// Registers all 8 ballista trap helpers in the FE7 native dispatch table.
void RegisterBallistaFunctions();

}  // namespace fe7

#endif  // FE7_GAME_MAP_BALLISTA_H_
