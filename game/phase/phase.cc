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
  if (CheckTurnEvents() == 1) {
    GbaCall(kAddrRunTurnEvents);
    return 0;
  }
  return 1;
}

int BmMainCheckVictory() {
  if (CheckVictoryEvents() == 1) {
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
      RecordUnitTurnDeployment(unit->pinfo->id);
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

GbaPtr<const uint8_t> GetChapterInfo(int chapter_id) {
  const uint32_t addr =
      kChapterInfoTableAddr + static_cast<uint32_t>(chapter_id) * 0x98u;
  return GbaPtr<const uint8_t>(addr);
}

GbaPtr<const uint32_t> GetChapterEventInfo(int chapter_id) {
  const uint8_t asset_idx = GetChapterInfo(chapter_id)[0x78];
  const uint32_t ptr_addr =
      kChapterEventAssetTableAddr + static_cast<uint32_t>(asset_idx) * 4u;
  return *GbaPtr<const GbaPtr<const uint32_t>>(ptr_addr);
}

int CheckChapterFlag(int flag) {
  if (flag == 0) {
    return 0;
  }
  const int idx = flag - 1;
  const int byte_idx = idx / 8;
  const int bit_idx = idx - byte_idx * 8;
  const uint8_t byte_val = *GbaPtr<const uint8_t>(
      kChapterFlagsAddr + static_cast<uint32_t>(byte_idx));
  const uint8_t mask = *GbaPtr<const uint8_t>(kFlagBitMaskTableAddr +
                                              static_cast<uint32_t>(bit_idx));
  return (byte_val & mask) != 0u ? 1 : 0;
}

int CheckPermanentFlag(int flag) {
  if (flag <= 0x64) {
    return 0;
  }
  const int idx = flag - 0x65;
  const int byte_idx = idx / 8;
  const int bit_idx = idx - byte_idx * 8;
  const uint8_t byte_val = *GbaPtr<const uint8_t>(
      kPermanentFlagsAddr + static_cast<uint32_t>(byte_idx));
  const uint8_t mask = *GbaPtr<const uint8_t>(kFlagBitMaskTableAddr +
                                              static_cast<uint32_t>(bit_idx));
  return (byte_val & mask) != 0u ? 1 : 0;
}

int GetFlag(int flag) {
  return flag <= 0x63 ? CheckChapterFlag(flag) : CheckPermanentFlag(flag);
}

int EventListEndHandler(GbaPtr<EventListInfo> /*info*/) { return 1; }

GbaPtr<EventListInfo> SearchEventList(GbaPtr<EventListInfo> info) {
  info->script_addr = 0;
  info->flag = 0;
  for (;;) {
    const GbaPtr<const uint16_t> entry(info->list_ptr);
    const uint16_t kind = entry[0];
    const uint16_t flag = entry[1];
    if (GetFlag(static_cast<int>(flag)) == 0) {
      const uint32_t handler_addr = *GbaPtr<const uint32_t>(
          kEventListHandlerTableAddr + static_cast<uint32_t>(kind) * 8u);
      if (GbaCall(handler_addr, info.addr) == 1u) {
        break;
      }
    }
    const uint32_t word_size = *GbaPtr<const uint32_t>(
        kEventListHandlerTableAddr + static_cast<uint32_t>(kind) * 8u + 4u);
    info->list_ptr += word_size * 4u;
  }
  return info->script_addr != 0u ? info : GbaPtr<EventListInfo>(0);
}

int CheckTurnEvents() {
  GbaPtr<EventListInfo> info(kEventInfoScratchAddr);
  const int8_t ch = *GbaPtr<const int8_t>(kPhasePlayStatusAddr + 0x0Eu);
  info->list_ptr = GetChapterEventInfo(static_cast<int>(ch))[0];
  return SearchEventList(info) ? 1 : 0;
}

int CheckVictoryEvents() {
  GbaPtr<EventListInfo> info(kEventInfoScratchAddr);
  const int8_t raw_ch = *GbaPtr<const int8_t>(kPhasePlayStatusAddr + 0x0Eu);
  const uint16_t ch = static_cast<uint16_t>(static_cast<int16_t>(raw_ch));
  if (ch > 0x0Bu) {
    return 0;
  }
  info->list_ptr = *GbaPtr<const uint32_t>(
      kVictoryEventTableAddr + static_cast<uint32_t>(ch) * 16u + 0x0Cu);
  return SearchEventList(info) ? 1 : 0;
}

void RegisterPhaseFunctions() {
  RegisterNative(kAddrGetChapterInfo,
                 [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return GetChapterInfo(static_cast<int32_t>(r0)).addr;
                 });
  RegisterNative(kAddrGetChapterEventInfo,
                 [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return GetChapterEventInfo(static_cast<int32_t>(r0)).addr;
                 });
  RegisterNative(kAddrSearchEventList,
                 [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return SearchEventList(GbaPtr<EventListInfo>(r0)).addr;
                 });
  RegisterNative(kAddrEventListEndHandler,
                 [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return static_cast<uint32_t>(
                       EventListEndHandler(GbaPtr<EventListInfo>(r0)));
                 });
  RegisterNative(kAddrCheckTurnEvents,
                 [](uint32_t, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return static_cast<uint32_t>(CheckTurnEvents());
                 });
  RegisterNative(kAddrCheckVictoryEvents,
                 [](uint32_t, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return static_cast<uint32_t>(CheckVictoryEvents());
                 });
  RegisterNative(kAddrCheckChapterFlag,
                 [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return static_cast<uint32_t>(
                       CheckChapterFlag(static_cast<int32_t>(r0)));
                 });
  RegisterNative(kAddrCheckPermanentFlag,
                 [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return static_cast<uint32_t>(
                       CheckPermanentFlag(static_cast<int32_t>(r0)));
                 });
  RegisterNative(
      kAddrGetFlag,
      [](uint32_t r0, uint32_t, uint32_t, uint32_t,
         const uint32_t* absl_nullable) -> uint32_t {
        return static_cast<uint32_t>(GetFlag(static_cast<int32_t>(r0)));
      });
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
