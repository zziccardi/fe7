#include "game/map/ballista.h"

#include <cstdint>

#include "absl/base/nullability.h"
#include "game/item/item.h"
#include "game/map/trap.h"
#include "game/phase/phase.h"
#include "game/unit.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// ---------------------------------------------------------------------------
// 0x08034698: GetBallistaAt
// ---------------------------------------------------------------------------
GbaPtr<Trap> GetBallistaAt(int x, int y) {
  GbaPtr<Trap> trap = GetTrapAt(x, y);
  if (!IsBallistaTrap(GbaPtr<const Trap>(trap.addr))) {
    return GbaPtr<Trap>(0);
  }
  if (static_cast<int8_t>(trap->data[2]) == 0) {
    return GbaPtr<Trap>(0);
  }
  return trap;
}

// ---------------------------------------------------------------------------
// 0x080346C8: GetBallistaItemAtPos
// ---------------------------------------------------------------------------
int GetBallistaItemAtPos(int x, int y) {
  GbaPtr<Trap> trap = GetTrapAt(x, y);
  if (!IsBallistaTrap(GbaPtr<const Trap>(trap.addr))) {
    return 0;
  }
  const int8_t ammo = static_cast<int8_t>(trap->data[2]);
  if (ammo == 0) {
    return 0;
  }
  return static_cast<int>(
      (static_cast<uint32_t>(static_cast<int32_t>(ammo)) << 8) |
      static_cast<uint32_t>(trap->extra));
}

// ---------------------------------------------------------------------------
// 0x08034710: GetBallistaItemAtIgnoreUses
// ---------------------------------------------------------------------------
int GetBallistaItemAtIgnoreUses(int x, int y) {
  GbaPtr<Trap> trap = GetTrapAt(x, y);
  if (!IsBallistaTrap(GbaPtr<const Trap>(trap.addr))) {
    return 0;
  }
  if (trap->extra == 0u) {
    return 0;
  }
  return static_cast<int>(trap->extra) + 0x100;
}

// ---------------------------------------------------------------------------
// 0x08034740: AddBallista
// ---------------------------------------------------------------------------
GbaPtr<Trap> AddBallista(int x, int y, int item) {
  GbaPtr<Trap> trap = AddTrap(x, y, static_cast<int>(TrapType::kBallista), 0);
  trap->extra = static_cast<uint8_t>(GetItemIid(item));
  trap->data[2] = static_cast<uint8_t>(GetItemUses(CreateItem(item)));
  trap->data[1] = 0u;
  return trap;
}

// ---------------------------------------------------------------------------
// 0x08034770: UnitRideBallista
// ---------------------------------------------------------------------------
void UnitRideBallista(GbaPtr<Unit> unit) {
  GbaPtr<Trap> trap = GetTrapAt(unit->x, unit->y);
  trap->data[1] = 1u;
  GbaCall(kAddrRefreshUnitSprites);
  unit->flags |= kUnitFlagInBallista;
  const int32_t diff = static_cast<int32_t>(trap.addr - GetTrap(0).addr);
  unit->ballista_trap = static_cast<uint8_t>(diff >> 3);
}

// ---------------------------------------------------------------------------
// 0x080347A8: TryRemoveUnitFromBallista
// ---------------------------------------------------------------------------
void TryRemoveUnitFromBallista(GbaPtr<Unit> unit) {
  if ((unit->flags & kUnitFlagInBallista) == 0u) {
    return;
  }
  GbaPtr<Trap> trap = GetTrap(unit->ballista_trap);
  unit->flags &= ~kUnitFlagInBallista;
  trap->data[1] = 0u;
  unit->ballista_trap = 0u;
  trap->x = static_cast<uint8_t>(unit->x);
  trap->y = static_cast<uint8_t>(unit->y);
  GbaCall(kAddrRefreshUnitSprites);
}

// ---------------------------------------------------------------------------
// 0x080347E4: IsBallistaTrap
// ---------------------------------------------------------------------------
bool IsBallistaTrap(GbaPtr<const Trap> trap) {
  return trap && trap->type == static_cast<uint8_t>(TrapType::kBallista);
}

// ---------------------------------------------------------------------------
// 0x080347F8: GetBallistaItemFromTrap
// ---------------------------------------------------------------------------
int GetBallistaItemFromTrap(GbaPtr<const Trap> trap) {
  if (!IsBallistaTrap(trap)) {
    return 0;
  }
  const int8_t ammo = static_cast<int8_t>(trap->data[2]);
  return static_cast<int>(
      (static_cast<uint32_t>(static_cast<int32_t>(ammo)) << 8) |
      static_cast<uint32_t>(trap->extra));
}

// ---------------------------------------------------------------------------
// Native dispatch thunks
// ---------------------------------------------------------------------------
namespace {

uint32_t ThunkGetBallistaAt(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                            const uint32_t* absl_nullable) {
  return GetBallistaAt(static_cast<int>(r0), static_cast<int>(r1)).addr;
}
uint32_t ThunkGetBallistaItemAtPos(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                                   const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(
      GetBallistaItemAtPos(static_cast<int>(r0), static_cast<int>(r1)));
}
uint32_t ThunkGetBallistaItemAtIgnoreUses(uint32_t r0, uint32_t r1, uint32_t,
                                          uint32_t,
                                          const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(
      GetBallistaItemAtIgnoreUses(static_cast<int>(r0), static_cast<int>(r1)));
}
uint32_t ThunkAddBallista(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t,
                          const uint32_t* absl_nullable) {
  return AddBallista(static_cast<int>(r0), static_cast<int>(r1),
                     static_cast<int>(r2))
      .addr;
}
uint32_t ThunkUnitRideBallista(uint32_t r0, uint32_t, uint32_t, uint32_t,
                               const uint32_t* absl_nullable) {
  UnitRideBallista(GbaPtr<Unit>(r0));
  return 0u;
}
uint32_t ThunkTryRemoveUnitFromBallista(uint32_t r0, uint32_t, uint32_t,
                                        uint32_t,
                                        const uint32_t* absl_nullable) {
  TryRemoveUnitFromBallista(GbaPtr<Unit>(r0));
  return 0u;
}
uint32_t ThunkIsBallistaTrap(uint32_t r0, uint32_t, uint32_t, uint32_t,
                             const uint32_t* absl_nullable) {
  return IsBallistaTrap(GbaPtr<const Trap>(r0)) ? 1u : 0u;
}
uint32_t ThunkGetBallistaItemFromTrap(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                      const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(GetBallistaItemFromTrap(GbaPtr<const Trap>(r0)));
}

}  // namespace

void RegisterBallistaFunctions() {
  RegisterNative(kAddrGetBallistaAt, &ThunkGetBallistaAt);
  RegisterNative(kAddrGetBallistaItemAtPos, &ThunkGetBallistaItemAtPos);
  RegisterNative(kAddrGetBallistaItemAtIgnoreUses,
                 &ThunkGetBallistaItemAtIgnoreUses);
  RegisterNative(kAddrAddBallista, &ThunkAddBallista);
  RegisterNative(kAddrUnitRideBallista, &ThunkUnitRideBallista);
  RegisterNative(kAddrTryRemoveUnitFromBallista,
                 &ThunkTryRemoveUnitFromBallista);
  RegisterNative(kAddrIsBallistaTrap, &ThunkIsBallistaTrap);
  RegisterNative(kAddrGetBallistaItemFromTrap, &ThunkGetBallistaItemFromTrap);
}

}  // namespace fe7
