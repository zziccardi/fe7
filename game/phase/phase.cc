// FE7 US (AE7E) turn flow, phase switching, faction queries, and gold helpers.

#include "game/phase/phase.h"

#include <cstdint>

#include "absl/base/nullability.h"
#include "game/engine/proc.h"
#include "game/proc.h"
#include "game/unit.h"
#include "game/unit/unit.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

constexpr uint32_t kBmStatusLockOffset = 0x01u;
constexpr uint32_t kPlayStatusGoldOffset = 0x08u;
constexpr uint32_t kPlayStatusFactionOffset = 0x0Fu;
constexpr uint32_t kPlayStatusTurnOffset = 0x10u;
constexpr uint32_t kPlayStatusConfig2Offset = 0x41u;

// Unit flag masks matching FE7 US phase/unit-counting routines.
constexpr uint32_t kRefreshSkipDeployMask = 0x0001000Eu;
constexpr uint32_t kRefreshClearFlagsMask = 0xFFFFFBBDu;
constexpr uint32_t kClearBit8Mask = 0xFFFFFEFFu;
constexpr uint32_t kUnitUnableMask = 0x000100AEu;
constexpr uint32_t kCountByFactionSkipMask = 0x0001000Cu;

constexpr uint32_t kAttrSupply = 0x00000200u;
constexpr uint32_t kAttrUnselectable = 0x00100000u;
constexpr uint8_t kStatusSleep = 2u;
constexpr uint8_t kStatusBerserk = 4u;

uint8_t ReadActiveFaction() {
  return *GbaPtr<const uint8_t>(kPhasePlayStatusAddr +
                                kPlayStatusFactionOffset);
}

void WriteActiveFaction(uint8_t faction) {
  *GbaPtr<uint8_t>(kPhasePlayStatusAddr + kPlayStatusFactionOffset) = faction;
}

// --- Dispatch thunks ---

uint32_t ThunkLockGame(uint32_t, uint32_t, uint32_t, uint32_t,
                       const uint32_t* absl_nullable) {
  LockGame();
  return 0;
}

uint32_t ThunkReleaseGame(uint32_t, uint32_t, uint32_t, uint32_t,
                          const uint32_t* absl_nullable) {
  ReleaseGame();
  return 0;
}

uint32_t ThunkGetGameLock(uint32_t, uint32_t, uint32_t, uint32_t,
                          const uint32_t* absl_nullable) {
  return GetGameLock();
}

uint32_t ThunkSwitchPhases(uint32_t, uint32_t, uint32_t, uint32_t,
                           const uint32_t* absl_nullable) {
  SwitchPhases();
  return 0;
}

uint32_t ThunkBmMainChangePhase(uint32_t, uint32_t, uint32_t, uint32_t,
                                const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(BmMainChangePhase());
}

uint32_t ThunkBmMainCheckVictory(uint32_t, uint32_t, uint32_t, uint32_t,
                                 const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(BmMainCheckVictory());
}

uint32_t ThunkBmMainStartPhase(uint32_t r0, uint32_t, uint32_t, uint32_t,
                               const uint32_t* absl_nullable) {
  BmMainStartPhase(GbaPtr<Proc>(r0));
  return 0;
}

uint32_t ThunkBmMainResumePlayerPhase(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                      const uint32_t* absl_nullable) {
  BmMainResumePlayerPhase(GbaPtr<Proc>(r0));
  return 0;
}

uint32_t ThunkBmMainUpdateTraps(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(BmMainUpdateTraps(GbaPtr<Proc>(r0)));
}

uint32_t ThunkRefreshFactionUnits(uint32_t, uint32_t, uint32_t, uint32_t,
                                  const uint32_t* absl_nullable) {
  RefreshFactionUnits();
  return 0;
}

uint32_t ThunkClearAllUnitsBit8(uint32_t, uint32_t, uint32_t, uint32_t,
                                const uint32_t* absl_nullable) {
  ClearAllUnitsBit8();
  return 0;
}

uint32_t ThunkPlayerPhaseHandleAutoEnd(uint32_t r0, uint32_t, uint32_t,
                                       uint32_t,
                                       const uint32_t* absl_nullable) {
  PlayerPhaseHandleAutoEnd(GbaPtr<Proc>(r0));
  return 0;
}

uint32_t ThunkGetPhaseAbleUnitCount(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                    const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(GetPhaseAbleUnitCount(static_cast<int32_t>(r0)));
}

uint32_t ThunkCountUnitsInState(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                                const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(CountUnitsInState(static_cast<int32_t>(r0), r1));
}

uint32_t ThunkAreUnitIdsAllied(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                               const uint32_t* absl_nullable) {
  return AreUnitIdsAllied(static_cast<int32_t>(r0), static_cast<int32_t>(r1))
             ? 1u
             : 0u;
}

uint32_t ThunkIsSameAllegiance(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                               const uint32_t* absl_nullable) {
  return IsSameAllegiance(static_cast<int32_t>(r0), static_cast<int32_t>(r1))
             ? 1u
             : 0u;
}

uint32_t ThunkGetCurrentPhase(uint32_t, uint32_t, uint32_t, uint32_t,
                              const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(GetCurrentPhase());
}

uint32_t ThunkGetNonActiveFaction(uint32_t, uint32_t, uint32_t, uint32_t,
                                  const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(GetNonActiveFaction());
}

uint32_t ThunkGetGold(uint32_t, uint32_t, uint32_t, uint32_t,
                      const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(GetGold());
}

uint32_t ThunkSetGold(uint32_t r0, uint32_t, uint32_t, uint32_t,
                      const uint32_t* absl_nullable) {
  SetGold(static_cast<int32_t>(r0));
  return 0;
}

uint32_t ThunkAddGold(uint32_t r0, uint32_t, uint32_t, uint32_t,
                      const uint32_t* absl_nullable) {
  AddGold(static_cast<int32_t>(r0));
  return 0;
}

uint32_t ThunkCountUnitsByFaction(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                  const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(CountUnitsByFaction(static_cast<int32_t>(r0)));
}

}  // namespace

void LockGame() {
  GbaPtr<uint8_t> lock(kPhaseBmStatusAddr + kBmStatusLockOffset);
  *lock = static_cast<uint8_t>(*lock + 1u);
}

void ReleaseGame() {
  GbaPtr<uint8_t> lock(kPhaseBmStatusAddr + kBmStatusLockOffset);
  *lock = static_cast<uint8_t>(*lock - 1u);
}

uint8_t GetGameLock() {
  return *GbaPtr<const uint8_t>(kPhaseBmStatusAddr + kBmStatusLockOffset);
}

void SwitchPhases() {
  switch (ReadActiveFaction()) {
    case kFactionBlue:
      WriteActiveFaction(kFactionRed);
      break;
    case kFactionRed:
      WriteActiveFaction(kFactionGreen);
      break;
    case kFactionGreen: {
      WriteActiveFaction(kFactionBlue);
      GbaPtr<uint16_t> turn(kPhasePlayStatusAddr + kPlayStatusTurnOffset);
      if (*turn < kMaxTurnCount) {
        *turn = static_cast<uint16_t>(*turn + 1u);
      }
      GbaCall(kAddrProcessSupportGains);
      break;
    }
    default:
      break;
  }
}

int BmMainChangePhase() {
  RefreshFactionUnits();
  GbaCall(kAddrRefreshUnitSprites);
  SwitchPhases();
  const int8_t has_event =
      static_cast<int8_t>(GbaCall(kAddrCheckTurnEvents) & 0xFFu);
  if (has_event == 1) {
    GbaCall(kAddrRunTurnEvents);
    return 0;
  }
  return 1;
}

int BmMainCheckVictory() {
  const int8_t has_event =
      static_cast<int8_t>(GbaCall(kAddrCheckVictoryEvents) & 0xFFu);
  if (has_event == 1) {
    GbaCall(kAddrRunVictoryEvents);
    return 0;
  }
  return 1;
}

void BmMainStartPhase(GbaPtr<Proc> proc) {
  switch (ReadActiveFaction()) {
    case kFactionBlue:
      SpawnProcLocking(GbaPtr<const ProcScr>(kProcScrPlayerPhaseAddr), proc);
      break;
    case kFactionRed:
    case kFactionGreen:
      SpawnProcLocking(GbaPtr<const ProcScr>(kProcScrCpPhaseAddr), proc);
      break;
    default:
      break;
  }
  ProcBreak(proc);
}

void BmMainResumePlayerPhase(GbaPtr<Proc> proc) {
  GbaPtr<Proc> child =
      SpawnProcLocking(GbaPtr<const ProcScr>(kProcScrPlayerPhaseAddr), proc);
  ProcGoto(child, 7);
  ProcBreak(proc);
}

int BmMainUpdateTraps(GbaPtr<Proc> proc) {
  if (ReadActiveFaction() == kFactionGreen) {
    SpawnProcLocking(GbaPtr<const ProcScr>(kProcScrTrapUpdateAddr), proc);
    GbaCall(kAddrUpdateTrapsCallback);
    return 0;
  }
  return 1;
}

void RefreshFactionUnits() {
  if (ReadActiveFaction() == kFactionBlue) {
    for (int id = 1; id < 0x40; ++id) {
      GbaPtr<Unit> unit = GetUnit(id);
      if (!unit || !unit->pinfo) {
        continue;
      }
      if (((unit->pinfo->attributes | unit->jinfo->attributes) & kAttrSupply) !=
          0u) {
        continue;
      }
      if ((unit->flags & kRefreshSkipDeployMask) != 0u) {
        continue;
      }
      GbaCall(kAddrRecordUnitTurnDeployment, unit->pinfo->id);
    }
  }

  for (int id = static_cast<int>(ReadActiveFaction()) + 1;
       id < static_cast<int>(ReadActiveFaction()) + 0x40; ++id) {
    GbaPtr<Unit> unit = GetUnit(id);
    if (!unit || !unit->pinfo) {
      continue;
    }
    unit->flags &= kRefreshClearFlagsMask;
  }
}

void ClearAllUnitsBit8() {
  for (int id = 1; id <= 0xBF; ++id) {
    GbaPtr<Unit> unit = GetUnit(id);
    if (!unit || !unit->pinfo) {
      continue;
    }
    unit->flags &= kClearBit8Mask;
  }
}

void PlayerPhaseHandleAutoEnd(GbaPtr<Proc> proc) {
  const uint8_t config2 =
      *GbaPtr<const uint8_t>(kPhasePlayStatusAddr + kPlayStatusConfig2Offset);
  // Bit 6 (`0x40`) disables auto-end-turn when set.
  if ((config2 & 0x40u) != 0u) {
    return;
  }
  if (GetPhaseAbleUnitCount(ReadActiveFaction()) == 0) {
    ProcGoto(proc, 3);
  }
}

int GetPhaseAbleUnitCount(int faction) {
  int count = 0;
  for (int id = faction + 1; id < faction + 0x40; ++id) {
    GbaPtr<Unit> unit = GetUnit(id);
    if (!unit || !unit->pinfo) {
      continue;
    }
    if ((unit->flags & kUnitUnableMask) != 0u) {
      continue;
    }
    const uint8_t status = unit->status & 0x0Fu;
    if (status == kStatusSleep || status == kStatusBerserk) {
      continue;
    }
    if (((unit->pinfo->attributes | unit->jinfo->attributes) &
         kAttrUnselectable) != 0u) {
      continue;
    }
    ++count;
  }
  return count;
}

int CountUnitsInState(int faction, uint32_t state_mask) {
  int count = 0;
  for (int id = faction + 1; id < faction + 0x40; ++id) {
    GbaPtr<Unit> unit = GetUnit(id);
    if (!unit || !unit->pinfo) {
      continue;
    }
    if ((unit->flags & state_mask) == 0u) {
      ++count;
    }
  }
  return count;
}

bool AreUnitIdsAllied(int left, int right) {
  return (left & kFactionRed) == (right & kFactionRed);
}

bool IsSameAllegiance(int left, int right) {
  return (left & 0xC0) == (right & 0xC0);
}

int GetCurrentPhase() {
  return static_cast<int>(ReadActiveFaction() & kFactionRed);
}

int GetNonActiveFaction() {
  return static_cast<int>((ReadActiveFaction() & kFactionRed) ^ kFactionRed);
}

int32_t GetGold() {
  return *GbaPtr<const int32_t>(kPhasePlayStatusAddr + kPlayStatusGoldOffset);
}

void SetGold(int32_t amount) {
  GbaPtr<int32_t> gold(kPhasePlayStatusAddr + kPlayStatusGoldOffset);
  *gold = amount > kMaxGold ? kMaxGold : amount;
}

void AddGold(int32_t delta) {
  GbaPtr<int32_t> gold(kPhasePlayStatusAddr + kPlayStatusGoldOffset);
  const int32_t total = static_cast<int32_t>(static_cast<uint32_t>(*gold) +
                                             static_cast<uint32_t>(delta));
  *gold = total > kMaxGold ? kMaxGold : total;
}

int CountUnitsByFaction(int faction) {
  int count = 0;
  for (int id = faction + 1; id < faction + 0x40; ++id) {
    GbaPtr<Unit> unit = GetUnit(id);
    if (!unit || !unit->pinfo) {
      continue;
    }
    if ((unit->flags & kCountByFactionSkipMask) == 0u) {
      ++count;
    }
  }
  return count;
}

void RegisterPhaseFunctions() {
  RegisterNative(kAddrLockGame, ThunkLockGame);
  RegisterNative(kAddrReleaseGame, ThunkReleaseGame);
  RegisterNative(kAddrGetGameLock, ThunkGetGameLock);
  RegisterNative(kAddrSwitchPhases, ThunkSwitchPhases);
  RegisterNative(kAddrBmMainChangePhase, ThunkBmMainChangePhase);
  RegisterNative(kAddrBmMainCheckVictory, ThunkBmMainCheckVictory);
  RegisterNative(kAddrBmMainStartPhase, ThunkBmMainStartPhase);
  RegisterNative(kAddrBmMainResumePlayerPhase, ThunkBmMainResumePlayerPhase);
  RegisterNative(kAddrBmMainUpdateTraps, ThunkBmMainUpdateTraps);
  RegisterNative(kAddrRefreshFactionUnits, ThunkRefreshFactionUnits);
  RegisterNative(kAddrClearAllUnitsBit8, ThunkClearAllUnitsBit8);
  RegisterNative(kAddrPlayerPhaseHandleAutoEnd, ThunkPlayerPhaseHandleAutoEnd);
  RegisterNative(kAddrGetPhaseAbleUnitCount, ThunkGetPhaseAbleUnitCount);
  RegisterNative(kAddrCountUnitsInState, ThunkCountUnitsInState);
  RegisterNative(kAddrAreUnitIdsAllied, ThunkAreUnitIdsAllied);
  RegisterNative(kAddrIsSameAllegiance, ThunkIsSameAllegiance);
  RegisterNative(kAddrGetCurrentPhase, ThunkGetCurrentPhase);
  RegisterNative(kAddrGetNonActiveFaction, ThunkGetNonActiveFaction);
  RegisterNative(kAddrGetGold, ThunkGetGold);
  RegisterNative(kAddrSetGold, ThunkSetGold);
  RegisterNative(kAddrAddGold, ThunkAddGold);
  RegisterNative(kAddrCountUnitsByFaction, ThunkCountUnitsByFaction);
}

}  // namespace fe7
