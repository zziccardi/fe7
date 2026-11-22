// FE7 US (AE7E) turn flow, phase switching, faction queries, and gold helpers.
//
// Implements 22 turn-flow & phase-management functions (ROM 0x08015308..
// 0x08015458, 0x08018268, 0x080183F4, 0x0801D3AC, 0x08023810..0x08023928,
// and 0x080868B8): game lock counters, Blue->Red->Green phase rotation and turn
// counter advancement, faction unit refresh, auto-end-turn checks, alliance and
// unit-count queries, and party gold management.

#ifndef FE7_GAME_PHASE_PHASE_H_
#define FE7_GAME_PHASE_PHASE_H_

#include <cstdint>
#include <type_traits>

#include "game/proc.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// Fixed FE7 US EWRAM addresses for game-state and play-status blocks.
inline constexpr uint32_t kPhaseBmStatusAddr = 0x0202BBB8u;
inline constexpr uint32_t kPhasePlayStatusAddr = 0x0202BBF8u;

// Faction IDs stored in `gPlayStatus.faction` (offset 0x0F).
inline constexpr uint8_t kFactionBlue = 0x00u;
inline constexpr uint8_t kFactionGreen = 0x40u;
inline constexpr uint8_t kFactionRed = 0x80u;

// Maximum party gold (`999999`) and maximum displayed turn count (`999`).
inline constexpr int32_t kMaxGold = 999999;
inline constexpr uint16_t kMaxTurnCount = 999u;

// External ROM proc scripts and callbacks invoked via GbaCall() / SpawnProc*.
inline constexpr uint32_t kProcScrPlayerPhaseAddr = 0x08B93374u;
inline constexpr uint32_t kProcScrTrapUpdateAddr = 0x08B94578u;
inline constexpr uint32_t kProcScrCpPhaseAddr = 0x08B96E80u;

inline constexpr uint32_t kAddrRefreshUnitSprites = 0x08025724u;
inline constexpr uint32_t kAddrProcessSupportGains = 0x08026898u;
inline constexpr uint32_t kAddrUpdateTrapsCallback = 0x0802C24Cu;
inline constexpr uint32_t kAddrRunTurnEvents = 0x080789B8u;
inline constexpr uint32_t kAddrRunVictoryEvents = 0x080790C4u;

// FE7 US ROM addresses for the 22 decompiled turn-flow & phase functions.
inline constexpr uint32_t kAddrLockGame = 0x08015308u;
inline constexpr uint32_t kAddrReleaseGame = 0x08015318u;
inline constexpr uint32_t kAddrGetGameLock = 0x08015328u;
inline constexpr uint32_t kAddrSwitchPhases = 0x08015334u;
inline constexpr uint32_t kAddrBmMainChangePhase = 0x0801539Cu;
inline constexpr uint32_t kAddrBmMainCheckVictory = 0x080153C4u;
inline constexpr uint32_t kAddrBmMainStartPhase = 0x080153E0u;
inline constexpr uint32_t kAddrBmMainResumePlayerPhase = 0x08015434u;
inline constexpr uint32_t kAddrBmMainUpdateTraps = 0x08015458u;
inline constexpr uint32_t kAddrRefreshFactionUnits = 0x08018268u;
inline constexpr uint32_t kAddrClearAllUnitsBit8 = 0x080183F4u;
inline constexpr uint32_t kAddrPlayerPhaseHandleAutoEnd = 0x0801D3ACu;
inline constexpr uint32_t kAddrGetPhaseAbleUnitCount = 0x08023810u;
inline constexpr uint32_t kAddrCountUnitsInState = 0x08023874u;
inline constexpr uint32_t kAddrAreUnitIdsAllied = 0x080238B0u;
inline constexpr uint32_t kAddrIsSameAllegiance = 0x080238C4u;
inline constexpr uint32_t kAddrGetCurrentPhase = 0x080238D8u;
inline constexpr uint32_t kAddrGetNonActiveFaction = 0x080238ECu;
inline constexpr uint32_t kAddrGetGold = 0x08023904u;
inline constexpr uint32_t kAddrSetGold = 0x08023910u;
inline constexpr uint32_t kAddrAddGold = 0x08023928u;
inline constexpr uint32_t kAddrCountUnitsByFaction = 0x080868B8u;

// Increments `gBmStatus.game_lock` (offset +0x01). Matches FE7 US `LockGame`.
void LockGame();

// Decrements `gBmStatus.game_lock` (offset +0x01). Matches FE7 US
// `ReleaseGame`.
void ReleaseGame();

// Returns `gBmStatus.game_lock`. Matches FE7 US `GetGameLock` (0x08015328).
uint8_t GetGameLock();

// Rotates `gPlayStatus.faction` (`Blue -> Red -> Green -> Blue`); on wrap to
// Blue, increments `gPlayStatus.turn` (capped at 999) and processes support
// gains. Matches FE7 US `func_08015334`.
void SwitchPhases();

// Refreshes the ending faction's units, updates unit sprites, rotates phases,
// and runs turn-start events if any trigger. Returns 0 if events ran, else 1.
// Matches FE7 US `func_0801539C`.
int BmMainChangePhase();

// Checks victory/defeat conditions and runs the event if triggered. Returns 0
// if an event started, else 1. Matches FE7 US `func_080153C4`.
int BmMainCheckVictory();

// Spawns `PlayerPhase` (Blue) or `CpPhase` (Red/Green) locking `proc`, then
// breaks `proc`'s current repeat loop. Matches FE7 US `func_080153E0`.
void BmMainStartPhase(GbaPtr<Proc> proc);

// Resumes `PlayerPhase` at label 7 (mid-turn resume from suspend), locking
// `proc` and breaking its repeat loop. Matches FE7 US `func_08015434`.
void BmMainResumePlayerPhase(GbaPtr<Proc> proc);

// On Green phase, spawns the trap-update proc locking `proc` and triggers trap
// tick processing (returning 0); otherwise returns 1. Matches `func_08015458`.
int BmMainUpdateTraps(GbaPtr<Proc> proc);

// Clears turn-ended/moved/cantoing flags for the active faction (and records
// player unit deployment stats on Blue phase). Matches FE7 US `func_08018268`.
void RefreshFactionUnits();

// Clears state flag bit 8 (`0x100`) on all units `1..0xBF`.
// Matches FE7 US `func_080183F4`.
void ClearAllUnitsBit8();

// If auto-end-turn is enabled and no active faction units can act, jumps `proc`
// to label 3 (end phase). Matches FE7 US `PlayerPhase_HandleAutoEnd`.
void PlayerPhaseHandleAutoEnd(GbaPtr<Proc> proc);

// Counts units in `faction` that are alive, deployed, selectable, and not
// asleep/berserked/moved. Matches FE7 US `func_08023810`.
int GetPhaseAbleUnitCount(int faction);

// Counts valid units in `faction` whose `flags & state_mask == 0`.
// Matches FE7 US `func_08023874`.
int CountUnitsInState(int faction, uint32_t state_mask);

// Returns true if unit IDs `left` and `right` are allied (same bit 7: Blue and
// Green are allied against Red). Matches FE7 US `AreUnitIdsAllied`.
bool AreUnitIdsAllied(int left, int right);

// Returns true if unit IDs `left` and `right` belong to the exact same faction
// (bits 6..7 match). Matches FE7 US `func_080238C4`.
bool IsSameAllegiance(int left, int right);

// Returns `gPlayStatus.faction & kFactionRed` (`0x00` or `0x80`).
// Matches FE7 US `func_080238D8`.
int GetCurrentPhase();

// Returns `(gPlayStatus.faction & kFactionRed) ^ kFactionRed`.
// Matches FE7 US `func_080238EC`.
int GetNonActiveFaction();

// Returns the party's current gold (`gPlayStatus + 0x08`).
// Matches FE7 US `GetGold` (0x08023904).
int32_t GetGold();

// Sets the party's gold, clamping to `<= 999999`. Matches FE7 US `SetGold`.
void SetGold(int32_t amount);

// Adds `delta` to the party's gold, clamping to `<= 999999`.
// Matches FE7 US `func_08023928`.
void AddGold(int32_t delta);

// Counts valid units in `faction` that are neither dead, undeployed, nor
// hidden by bit 16 (`flags & 0x0001000C == 0`). Matches `CountUnitsByFaction`.
int CountUnitsByFaction(int faction);

// Event-list search cursor passed to `SearchEventList` (0x1C bytes).
struct EventListInfo {
  uint32_t list_ptr;     // 0x00: Current event list entry ROM address
  uint32_t script_addr;  // 0x04: Matched event script ROM address (0 = none)
  uint32_t flag;         // 0x08: Matched event flag ID
  uint32_t unk_0c[4];    // 0x0C..0x1B: Condition-specific scratch fields
};

static_assert(sizeof(EventListInfo) == 0x1C);
static_assert(std::is_trivially_copyable_v<EventListInfo>);

// Fixed FE7 US ROM/IWRAM addresses for chapter tables, event dispatch, and
// chapter/permanent event flag bitsets.
inline constexpr uint32_t kChapterInfoTableAddr = 0x08C9A200u;
inline constexpr uint32_t kChapterEventAssetTableAddr = 0x08C9C9C8u;
inline constexpr uint32_t kEventListHandlerTableAddr = 0x08C9E9A4u;
inline constexpr uint32_t kVictoryEventTableAddr = 0x08C9EA2Cu;
inline constexpr uint32_t kFlagBitMaskTableAddr = 0x08C9EAECu;
inline constexpr uint32_t kPermanentFlagsAddr = 0x03004AD0u;
inline constexpr uint32_t kChapterFlagsAddr = 0x03004AD8u;
inline constexpr uint32_t kEventInfoScratchAddr = 0x03007F00u;

// FE7 US ROM addresses for the 9 decompiled chapter-event & flag functions.
inline constexpr uint32_t kAddrGetChapterInfo = 0x08031574u;
inline constexpr uint32_t kAddrGetChapterEventInfo = 0x080315BCu;
inline constexpr uint32_t kAddrSearchEventList = 0x0807812Cu;
inline constexpr uint32_t kAddrEventListEndHandler = 0x080781B0u;
inline constexpr uint32_t kAddrCheckTurnEvents = 0x08078988u;
inline constexpr uint32_t kAddrCheckVictoryEvents = 0x08079104u;
inline constexpr uint32_t kAddrCheckChapterFlag = 0x08079798u;
inline constexpr uint32_t kAddrCheckPermanentFlag = 0x08079858u;
inline constexpr uint32_t kAddrGetFlag = 0x080798F8u;

// Returns a ROM pointer to the 0x98-byte chapter data entry for `chapter_id`.
// Matches FE7 US `GetChapterInfo` (0x08031574).
GbaPtr<const uint8_t> GetChapterInfo(int chapter_id);

// Returns a ROM pointer to the chapter's event-list pointer table.
// Matches FE7 US `func_080315BC`.
GbaPtr<const uint32_t> GetChapterEventInfo(int chapter_id);

// Walks `info->list_ptr` until a handler returns 1 (match or end-of-list),
// returning `info` if `info->script_addr != 0`, else null. Matches
// `func_0807812C`.
GbaPtr<EventListInfo> SearchEventList(GbaPtr<EventListInfo> info);

// Event-list end-of-table sentinel handler (always returns 1). Matches
// `func_080781B0`.
int EventListEndHandler(GbaPtr<EventListInfo> info);

// Returns 1 if the current chapter has a pending turn event, else 0.
// Matches FE7 US `func_08078988`.
int CheckTurnEvents();

// Returns 1 if the current chapter (0..11) has a pending victory event, else 0.
// Matches FE7 US `func_08079104`.
int CheckVictoryEvents();

// Checks a chapter-scoped event flag (`1..0x63`). Matches `func_08079798`.
int CheckChapterFlag(int flag);

// Checks a permanent event flag (`>= 0x65`). Matches `func_08079858`.
int CheckPermanentFlag(int flag);

// Checks chapter (`<= 0x63`) or permanent (`> 0x63`) event flag `flag`.
// Matches FE7 US `GetFlag` (0x080798F8).
int GetFlag(int flag);

// Registers all 31 turn-flow, phase, event, and flag functions in the dispatch
// table.
void RegisterPhaseFunctions();

}  // namespace fe7

#endif  // FE7_GAME_PHASE_PHASE_H_
