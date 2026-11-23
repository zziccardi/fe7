// FE7 US (AE7E) map traps, ballistas, light runes, obstacles, and fog/entity
// map refresh routines.

#include "game/map/trap.h"

#include <cstdint>

#include "absl/base/nullability.h"
#include "game/map/map.h"
#include "game/phase/phase.h"
#include "game/unit.h"
#include "game/unit/unit.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

// Terrain IDs used for destructible walls/snags and roof tiles.
constexpr uint8_t kTerrainWallBreakable = 0x1Bu;
constexpr uint8_t kTerrainRoof = 0x22u;
constexpr uint8_t kTerrainSnag = 0x33u;

// Unit attribute bit `0x04000000` (`kUnitAttrMagicSeal`, `0x80 << 19`) reduces
// fog visibility by 1 within range 10 in `RefreshUnitsOnMaps`.
constexpr uint32_t kAttrFogReduce = 0x80u << 19;

GbaPtr<GbaPtr<uint8_t>> MapUnitPtr() {
  return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr);
}

GbaPtr<GbaPtr<uint8_t>> MapTerrainPtr() {
  return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr);
}

GbaPtr<GbaPtr<uint8_t>> MapFogPtr() {
  return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapFogAddr);
}

GbaPtr<GbaPtr<uint8_t>> MapHiddenPtr() {
  return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapHiddenAddr);
}

}  // namespace

void RefreshUnitsOnMaps() {
  GbaPtr<GbaPtr<uint8_t>> unit_map = MapUnitPtr();
  GbaPtr<GbaPtr<uint8_t>> fog_map = MapFogPtr();
  GbaPtr<GbaPtr<uint8_t>> hidden_map = MapHiddenPtr();
  const uint8_t fog_vision = *GbaPtr<const uint8_t>(kMapPlayStatusAddr + 0x0Du);

  // Pass 1: Blue (1..0x3F) and Green (0x41..0x7F) units stamp `gMapUnit` and
  // contribute fog vision when chapter fog is active.
  for (int id = 1; id <= 0x7F; ++id) {
    GbaPtr<Unit> unit = GetUnit(id);
    if (!unit || !unit->pinfo || (unit->flags & kUnitFlagHidden) != 0u) {
      continue;
    }
    unit_map[unit->y][unit->x] = static_cast<uint8_t>(id);
    if (fog_vision != 0u) {
      const int vision = GetUnitVision(GbaPtr<const Unit>(unit.addr));
      MapAddInRange(unit->x, unit->y, vision, 1);
    }
  }

  // Pass 2: Red (0x81..0xC5) units. On non-Red phases, enemies outside player
  // fog vision are written to `gMapHidden` (bit 0) and flagged
  // `kUnitFlagFogHidden`.
  const uint8_t active_faction =
      *GbaPtr<const uint8_t>(kMapPlayStatusAddr + 0x0Fu);
  if (active_faction != kFactionRed) {
    for (int id = 0x81; id <= 0xC5; ++id) {
      GbaPtr<Unit> unit = GetUnit(id);
      if (!unit || !unit->pinfo || (unit->flags & kUnitFlagHidden) != 0u) {
        continue;
      }
      if (((unit->pinfo->attributes | unit->jinfo->attributes) &
           kAttrFogReduce) != 0u) {
        MapAddInRange(unit->x, unit->y, 10, -1);
      }
      const uint8_t cur_fog =
          *GbaPtr<const uint8_t>(kMapPlayStatusAddr + 0x0Du);
      if (cur_fog != 0u && fog_map[unit->y][unit->x] == 0u) {
        hidden_map[unit->y][unit->x] =
            static_cast<uint8_t>(hidden_map[unit->y][unit->x] | 1u);
        unit->flags |= kUnitFlagFogHidden;
      } else {
        unit_map[unit->y][unit->x] = static_cast<uint8_t>(id);
        if ((unit->flags & kUnitFlagFogHidden) != 0u) {
          unit->flags = (unit->flags & ~kUnitFlagFogHidden) | kUnitFlagFogSeen;
        }
      }
    }
  } else {
    for (int id = 0x81; id <= 0xC5; ++id) {
      GbaPtr<Unit> unit = GetUnit(id);
      if (!unit || !unit->pinfo || (unit->flags & kUnitFlagHidden) != 0u) {
        continue;
      }
      if (((unit->pinfo->attributes | unit->jinfo->attributes) &
           kAttrFogReduce) != 0u) {
        MapAddInRange(unit->x, unit->y, 10, -1);
      }
      const uint8_t cur_fog =
          *GbaPtr<const uint8_t>(kMapPlayStatusAddr + 0x0Du);
      if (cur_fog != 0u) {
        if (fog_map[unit->y][unit->x] == 0u) {
          unit->flags |= kUnitFlagFogHidden;
        } else {
          unit->flags &= ~kUnitFlagFogHidden;
        }
      }
      unit_map[unit->y][unit->x] = static_cast<uint8_t>(id);
    }
  }
}

void RefreshTorchlightOnFogMap() {
  for (GbaPtr<Trap> trap = GetTrap(0); trap->type != 0u; trap = trap + 1) {
    if (trap->type == static_cast<uint8_t>(TrapType::kTorchlight)) {
      MapAddInRange(trap->x, trap->y, trap->extra, 1);
    }
  }
}

void RefreshMinesOnHiddenMap() {
  GbaPtr<GbaPtr<uint8_t>> unit_map = MapUnitPtr();
  GbaPtr<GbaPtr<uint8_t>> hidden_map = MapHiddenPtr();
  for (GbaPtr<Trap> trap = GetTrap(0); trap->type != 0u; trap = trap + 1) {
    if (trap->type == static_cast<uint8_t>(TrapType::kMine) &&
        unit_map[trap->y][trap->x] == 0u) {
      hidden_map[trap->y][trap->x] =
          static_cast<uint8_t>(hidden_map[trap->y][trap->x] | 2u);
    }
  }
}

void RefreshEntityMaps() {
  MapFill(MapUnitPtr(), 0);
  MapFill(MapHiddenPtr(), 0);
  const uint8_t fog_vision = *GbaPtr<const uint8_t>(kMapPlayStatusAddr + 0x0Du);
  // MapFill sets `gWorkingMap = gMapFog` so subsequent `MapAddInRange` calls
  // inside `RefreshTorchlightOnFogMap` and `RefreshUnitsOnMaps` write to fog.
  MapFill(MapFogPtr(), fog_vision == 0u ? 1 : 0);
  RefreshTorchlightOnFogMap();
  RefreshUnitsOnMaps();
  RefreshMinesOnHiddenMap();
}

void InitTraps() {
  GbaPtr<Trap> traps(kTrapArrayAddr);
  for (int i = kMaxTrapCount - 1; i >= 0; --i) {
    traps[i].type = 0u;
  }
  traps[kMaxTrapCount].type = 0u;
}

GbaPtr<Trap> GetTrap(int index) {
  return GbaPtr<Trap>(kTrapArrayAddr +
                      static_cast<uint32_t>(index) * sizeof(Trap));
}

GbaPtr<Trap> GetTrapAt(int x, int y) {
  for (GbaPtr<Trap> trap(kTrapArrayAddr); trap->type != 0u; trap = trap + 1) {
    if (trap->x == x && trap->y == y) {
      return trap;
    }
  }
  return GbaPtr<Trap>(0);
}

GbaPtr<Trap> GetSpecificTrapAt(int x, int y, int type) {
  for (GbaPtr<Trap> trap(kTrapArrayAddr); trap->type != 0u; trap = trap + 1) {
    if (trap->x == x && trap->y == y && trap->type == type) {
      return trap;
    }
  }
  return GbaPtr<Trap>(0);
}

GbaPtr<Trap> AddTrap(int x, int y, int type, int extra) {
  GbaPtr<Trap> trap(kTrapArrayAddr);
  while (trap->type != 0u) {
    trap = trap + 1;
  }
  trap->x = static_cast<uint8_t>(x);
  trap->y = static_cast<uint8_t>(y);
  trap->type = static_cast<uint8_t>(type);
  trap->extra = static_cast<uint8_t>(extra);
  return trap;
}

GbaPtr<Trap> AddDamagingTrap(int x, int y, int type, int extra, int turn_delay,
                             int interval, int damage) {
  GbaPtr<Trap> trap = AddTrap(x, y, type, extra);
  trap->data[0] = static_cast<uint8_t>(turn_delay);
  trap->data[1] = static_cast<uint8_t>(interval);
  trap->data[2] = static_cast<uint8_t>(turn_delay);
  trap->data[3] = static_cast<uint8_t>(damage);
  return trap;
}

void RemoveTrap(GbaPtr<Trap> trap) {
  while (trap->type != 0u) {
    *trap = trap[1];
    trap = trap + 1;
  }
}

GbaPtr<Trap> AddFireTrap(int x, int y, int turn_delay, int interval) {
  return AddDamagingTrap(x, y, static_cast<int>(TrapType::kFire), 0, turn_delay,
                         interval, 10);
}

GbaPtr<Trap> AddGasTrap(int x, int y, int facing, int turn_delay,
                        int interval) {
  return AddDamagingTrap(x, y, static_cast<int>(TrapType::kGas), facing,
                         turn_delay, interval, 3);
}

GbaPtr<Trap> AddArrowTrap(int x, int turn_delay, int interval) {
  return AddDamagingTrap(x, 0, static_cast<int>(TrapType::kArrow), 0,
                         turn_delay, interval, 10);
}

GbaPtr<Trap> AddTrap6(int x, int y, int turn_delay, int interval) {
  return AddDamagingTrap(x, y, static_cast<int>(TrapType::kTrap6), 0,
                         turn_delay, interval, 0);
}

GbaPtr<Trap> AddTrap8(int x, int y) {
  return AddTrap(x, y, static_cast<int>(TrapType::kTrap8), 0);
}

GbaPtr<Trap> AddTrap9(int x, int y, int extra) {
  return AddTrap(x, y, static_cast<int>(TrapType::kTrap9), extra);
}

void InitMapObstacles() {
  const MapSize sz = *GbaPtr<const MapSize>(kMapSizeAddr);
  GbaPtr<GbaPtr<uint8_t>> terrain = MapTerrainPtr();
  for (int y = sz.y - 1; y >= 0; --y) {
    for (int x = sz.x - 1; x >= 0; --x) {
      const uint8_t tid = terrain[y][x];
      if (tid == kTerrainWallBreakable) {
        if (terrain[y - 1][x] == kTerrainWallBreakable) {
          continue;
        }
        const int8_t ch = *GbaPtr<const int8_t>(kMapPlayStatusAddr + 0x0Eu);
        const uint8_t wall_hp = GetChapterInfo(static_cast<int>(ch))[0x2C];
        AddTrap(x, y, static_cast<int>(TrapType::kObstacle), wall_hp);
      } else if (tid == kTerrainSnag) {
        AddTrap(x, y, static_cast<int>(TrapType::kObstacle), 20);
      }
    }
  }
}

void ApplyLightRuneTerrain() {
  GbaPtr<GbaPtr<uint8_t>> terrain = MapTerrainPtr();
  for (GbaPtr<Trap> trap(kTrapArrayAddr); trap->type != 0u; trap = trap + 1) {
    if (trap->type == static_cast<uint8_t>(TrapType::kLightRune)) {
      terrain[trap->y][trap->x] = 0u;
    }
  }
}

int GetTrapExtraAt(int x, int y) {
  GbaPtr<Trap> trap = GetTrapAt(x, y);
  return trap ? static_cast<int>(trap->extra) : 0;
}

void AddMapChangeTrap(int map_change_id) {
  AddTrap(0, 0, static_cast<int>(TrapType::kMapChange), map_change_id);
}

void RemoveMapChangeTrap(int map_change_id) {
  for (GbaPtr<Trap> trap(kTrapArrayAddr); trap->type != 0u; trap = trap + 1) {
    if (trap->type == static_cast<uint8_t>(TrapType::kMapChange) &&
        trap->extra == map_change_id) {
      RemoveTrap(trap);
    }
  }
}

void UnitHideIfUnderRoof(GbaPtr<Unit> unit) {
  GbaPtr<GbaPtr<uint8_t>> terrain = MapTerrainPtr();
  if (terrain[unit->y][unit->x] == kTerrainRoof) {
    unit->flags |= (kUnitFlagHidden | kUnitFlagUnderARoof);
  }
}

void TickDamagingTraps() {
  for (GbaPtr<Trap> trap(kTrapArrayAddr); trap->type != 0u; trap = trap + 1) {
    if (trap->type >= static_cast<uint8_t>(TrapType::kFire) &&
        trap->type <= static_cast<uint8_t>(TrapType::kArrow)) {
      trap->data[2] = static_cast<uint8_t>(trap->data[2] - 1u);
    }
  }
}

void ResetExpiredDamagingTraps() {
  for (GbaPtr<Trap> trap(kTrapArrayAddr); trap->type != 0u; trap = trap + 1) {
    if (trap->type >= static_cast<uint8_t>(TrapType::kFire) &&
        trap->type <= static_cast<uint8_t>(TrapType::kArrow) &&
        static_cast<int8_t>(trap->data[2]) == 0) {
      trap->data[2] = trap->data[1];
    }
  }
}

GbaPtr<Trap> AddLightRune(int x, int y) {
  GbaPtr<GbaPtr<uint8_t>> terrain = MapTerrainPtr();
  const uint8_t prev_tid = terrain[y][x];
  GbaPtr<Trap> trap =
      AddTrap(x, y, static_cast<int>(TrapType::kLightRune), prev_tid);
  trap->data[2] = 3u;
  terrain[y][x] = 0u;
  return trap;
}

void DisableAllLightRunes() {
  for (GbaPtr<Trap> trap(kTrapArrayAddr); trap->type != 0u; trap = trap + 1) {
    if (trap->type == static_cast<uint8_t>(TrapType::kLightRune)) {
      const uint8_t base_terrain = GetBaseTerrainAt(trap->x, trap->y);
      MapTerrainPtr()[trap->y][trap->x] = base_terrain;
    }
  }
}

void ApplyLightRuneTerrainAlt() { ApplyLightRuneTerrain(); }

void RegisterTrapFunctions() {
  RegisterNative(kAddrRefreshUnitsOnMaps,
                 [](uint32_t, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   RefreshUnitsOnMaps();
                   return 0u;
                 });
  RegisterNative(kAddrRefreshTorchlightOnFogMap,
                 [](uint32_t, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   RefreshTorchlightOnFogMap();
                   return 0u;
                 });
  RegisterNative(kAddrRefreshMinesOnHiddenMap,
                 [](uint32_t, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   RefreshMinesOnHiddenMap();
                   return 0u;
                 });
  RegisterNative(kAddrRefreshEntityMaps,
                 [](uint32_t, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   RefreshEntityMaps();
                   return 0u;
                 });
  RegisterNative(kAddrInitTraps,
                 [](uint32_t, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   InitTraps();
                   return 0u;
                 });
  RegisterNative(kAddrGetTrap,
                 [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return GetTrap(static_cast<int32_t>(r0)).addr;
                 });
  RegisterNative(kAddrGetTrapAt,
                 [](uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return GetTrapAt(static_cast<int32_t>(r0),
                                    static_cast<int32_t>(r1))
                       .addr;
                 });
  RegisterNative(kAddrGetSpecificTrapAt,
                 [](uint32_t r0, uint32_t r1, uint32_t r2, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return GetSpecificTrapAt(static_cast<int32_t>(r0),
                                            static_cast<int32_t>(r1),
                                            static_cast<int32_t>(r2))
                       .addr;
                 });
  RegisterNative(
      kAddrAddTrap,
      [](uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
         const uint32_t* absl_nullable) -> uint32_t {
        return AddTrap(static_cast<int32_t>(r0), static_cast<int32_t>(r1),
                       static_cast<int32_t>(r2), static_cast<int32_t>(r3))
            .addr;
      });
  RegisterNative(kAddrAddDamagingTrap,
                 [](uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                    const uint32_t* absl_nullable stack_args) -> uint32_t {
                   return AddDamagingTrap(static_cast<int32_t>(r0),
                                          static_cast<int32_t>(r1),
                                          static_cast<int32_t>(r2),
                                          static_cast<int32_t>(r3),
                                          static_cast<int32_t>(stack_args[0]),
                                          static_cast<int32_t>(stack_args[1]),
                                          static_cast<int32_t>(stack_args[2]))
                       .addr;
                 });
  RegisterNative(kAddrRemoveTrap,
                 [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   RemoveTrap(GbaPtr<Trap>(r0));
                   return 0u;
                 });
  RegisterNative(
      kAddrAddFireTrap,
      [](uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
         const uint32_t* absl_nullable) -> uint32_t {
        return AddFireTrap(static_cast<int32_t>(r0), static_cast<int32_t>(r1),
                           static_cast<int32_t>(r2), static_cast<int32_t>(r3))
            .addr;
      });
  RegisterNative(
      kAddrAddGasTrap,
      [](uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
         const uint32_t* absl_nullable stack_args) -> uint32_t {
        return AddGasTrap(static_cast<int32_t>(r0), static_cast<int32_t>(r1),
                          static_cast<int32_t>(r2), static_cast<int32_t>(r3),
                          static_cast<int32_t>(stack_args[0]))
            .addr;
      });
  RegisterNative(kAddrAddArrowTrap,
                 [](uint32_t r0, uint32_t r1, uint32_t r2, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return AddArrowTrap(static_cast<int32_t>(r0),
                                       static_cast<int32_t>(r1),
                                       static_cast<int32_t>(r2))
                       .addr;
                 });
  RegisterNative(
      kAddrAddTrap6,
      [](uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
         const uint32_t* absl_nullable) -> uint32_t {
        return AddTrap6(static_cast<int32_t>(r0), static_cast<int32_t>(r1),
                        static_cast<int32_t>(r2), static_cast<int32_t>(r3))
            .addr;
      });
  RegisterNative(kAddrAddTrap8,
                 [](uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return AddTrap8(static_cast<int32_t>(r0),
                                   static_cast<int32_t>(r1))
                       .addr;
                 });
  RegisterNative(kAddrAddTrap9,
                 [](uint32_t r0, uint32_t r1, uint32_t r2, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return AddTrap9(static_cast<int32_t>(r0),
                                   static_cast<int32_t>(r1),
                                   static_cast<int32_t>(r2))
                       .addr;
                 });
  RegisterNative(kAddrInitMapObstacles,
                 [](uint32_t, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   InitMapObstacles();
                   return 0u;
                 });
  RegisterNative(kAddrApplyLightRuneTerrain,
                 [](uint32_t, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   ApplyLightRuneTerrain();
                   return 0u;
                 });
  RegisterNative(kAddrGetTrapExtraAt,
                 [](uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return static_cast<uint32_t>(GetTrapExtraAt(
                       static_cast<int32_t>(r0), static_cast<int32_t>(r1)));
                 });
  RegisterNative(kAddrAddMapChangeTrap,
                 [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   AddMapChangeTrap(static_cast<int32_t>(r0));
                   return 0u;
                 });
  RegisterNative(kAddrRemoveMapChangeTrap,
                 [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   RemoveMapChangeTrap(static_cast<int32_t>(r0));
                   return 0u;
                 });
  RegisterNative(kAddrUnitHideIfUnderRoof,
                 [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   UnitHideIfUnderRoof(GbaPtr<Unit>(r0));
                   return 0u;
                 });
  RegisterNative(kAddrTickDamagingTraps,
                 [](uint32_t, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   TickDamagingTraps();
                   return 0u;
                 });
  RegisterNative(kAddrResetExpiredDamagingTraps,
                 [](uint32_t, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   ResetExpiredDamagingTraps();
                   return 0u;
                 });
  RegisterNative(kAddrAddLightRune,
                 [](uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return AddLightRune(static_cast<int32_t>(r0),
                                       static_cast<int32_t>(r1))
                       .addr;
                 });
  RegisterNative(kAddrDisableAllLightRunes,
                 [](uint32_t, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   DisableAllLightRunes();
                   return 0u;
                 });
  RegisterNative(kAddrApplyLightRuneTerrainAlt,
                 [](uint32_t, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   ApplyLightRuneTerrainAlt();
                   return 0u;
                 });
}

}  // namespace fe7
