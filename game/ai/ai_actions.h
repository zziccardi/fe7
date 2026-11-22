// FE7 US (AE7E) Enemy AI special actions, patrol/target-lock script actions,
// ballista handling, and healing staff AI (`AiTryHealStaff`,
// `AiTryPhysicStaff`, `AiTryFortifyStaff`, `AiTrySpecialAction`).
//
// Implements 28 Enemy AI functions (ROM 0x08039F60..0x0803AF98, including
// `AiTrySpecialAction` at 0x0803AAD8).

#ifndef FE7_GAME_AI_AI_ACTIONS_H_
#define FE7_GAME_AI_AI_ACTIONS_H_

#include <cstdint>

#include "game/ai/ai.h"
#include "game/map/map_range.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// Fixed FE7 US ROM table & EWRAM addresses used by AI action handlers.
inline constexpr uint32_t kAiTargetPidScratchAddr = 0x0203A988u;
inline constexpr uint32_t kAiPatrolWaypointsPtrAddr = 0x08B972E8u;
inline constexpr uint32_t kAiStaffActionTableAddr = 0x081D3B74u;
inline constexpr uint32_t kAiAdjacentOffsetsAddr = 0x08B98AC8u;

// External helper ROM addresses called via GbaCall() (until decompiled).
inline constexpr uint32_t kAddrGetUnitLeaderPid = 0x08018C20u;
inline constexpr uint32_t kAddrMapClearOther = 0x0801B148u;
inline constexpr uint32_t kAddrSetWorkingBmMap = 0x0801B190u;
inline constexpr uint32_t kAddrMapAddInBoundedRange = 0x0801B19Cu;
inline constexpr uint32_t kAddrGetFactionBaseUid = 0x080238D8u;
inline constexpr uint32_t kAddrGetBallistaItemAt = 0x08034698u;
inline constexpr uint32_t kAddrAiPerformAttackInCurrentMovement = 0x080387B0u;
inline constexpr uint32_t kAddrAiMakeRangeMapForUnitAndMov = 0x080389B8u;
inline constexpr uint32_t kAddrAiMapFloodUnitExtended = 0x0803BEA0u;

// FE7 US ROM addresses for the 28 decompiled AI action & staff functions.
inline constexpr uint32_t kAddrAiTryAttackFromPos = 0x08039F60u;
inline constexpr uint32_t kAddrAiIsTileOccupiedByEnemy = 0x0803A090u;
inline constexpr uint32_t kAddrAiScriptActionCountEnemiesInRange = 0x0803A0C0u;
inline constexpr uint32_t kAddrAiScriptActionMoveOrAttackInRegion = 0x0803A204u;
inline constexpr uint32_t kAddrAiScriptActionLoadUnitCounter = 0x0803A3D4u;
inline constexpr uint32_t kAddrAiScriptActionClearAiStateFlagA = 0x0803A3F0u;
inline constexpr uint32_t kAddrAiScriptActionClearAiStateFlagB = 0x0803A420u;
inline constexpr uint32_t kAddrAiGetNextPatrolWaypoint = 0x0803A450u;
inline constexpr uint32_t kAddrAiScriptActionPatrolOrAttack = 0x0803A4D8u;
inline constexpr uint32_t kAddrAiScriptActionIsActiveUnitInRect = 0x0803A548u;
inline constexpr uint32_t kAddrAiScriptActionTalk = 0x0803A58Cu;
inline constexpr uint32_t kAddrAiScriptActionProbeLeaderTarget = 0x0803A5BCu;
inline constexpr uint32_t kAddrAiIsUnitMatchingTargetPid = 0x0803A680u;
inline constexpr uint32_t kAddrAiScriptActionProbePidTarget = 0x0803A6BCu;
inline constexpr uint32_t kAddrAiIsUnitMatchingCachedUid = 0x0803A71Cu;
inline constexpr uint32_t kAddrAiIsUnitInDirectionOfCachedTarget = 0x0803A754u;
inline constexpr uint32_t kAddrAiScriptActionAttackOrChaseCachedTarget =
    0x0803A7C8u;
inline constexpr uint32_t kAddrAiScriptActionStaffOrAttack = 0x0803A828u;
inline constexpr uint32_t kAddrAiScriptActionLootOrAttack = 0x0803A874u;
inline constexpr uint32_t kAddrAiScriptActionBallista = 0x0803A8C4u;
inline constexpr uint32_t kAddrAiScriptActionMoveToCoord = 0x0803AA40u;
inline constexpr uint32_t kAddrAiScriptActionWait = 0x0803AA60u;
inline constexpr uint32_t kAddrAiGetStaffActionIndex = 0x0803AA70u;
inline constexpr uint32_t kAddrAiFindBestAdjacentStaffPos = 0x0803AB80u;
inline constexpr uint32_t kAddrAiTryHealStaff = 0x0803AC50u;
inline constexpr uint32_t kAddrAiTryPhysicStaff = 0x0803ADC8u;
inline constexpr uint32_t kAddrAiTryFortifyStaff = 0x0803AF98u;

// Attempts to attack any enemy in weapon range from `(x, y)` that scores
// non-zero on `AiGetInRangeCombatPositionScoreComponent`. Matches `0x08039F60`.
bool AiTryAttackFromPos(int x, int y);

// Returns true if `(x, y)` holds a unit whose faction bit (`0x80`) differs
// from `gActiveUnitId`. Matches `0x0803A090`.
bool AiIsTileOccupiedByEnemy(int x, int y);

// Counts enemies within scaled movement/weapon range and writes the count to
// `gAiState[0x86 + params[2]]`. Matches `0x0803A0C0`.
bool AiScriptActionCountEnemiesInRange(GbaPtr<const uint8_t> params);

// Checks whether the unit's patrol/region target tile is reachable within
// scaled range, attacks within that region if possible, or moves towards the
// target tile. Matches `0x0803A204`.
bool AiScriptActionMoveOrAttackInRegion(GbaPtr<const uint8_t> params);

// Copies `gActiveUnit->ai_counter` (`+0x46`) into `gAiState[0x86]`. Matches
// `0x0803A3D4`.
bool AiScriptActionLoadUnitCounter(GbaPtr<const uint8_t> params);

// Iterates current-faction units and clears `gAiState[0x79]`. Matches
// `0x0803A3F0` and `0x0803A420`.
bool AiScriptActionClearAiStateFlagA(GbaPtr<const uint8_t> params);
bool AiScriptActionClearAiStateFlagB(GbaPtr<const uint8_t> params);

// Reads the next patrol waypoint for `gActiveUnit` from the ROM table at
// `*0x08B972E8`, advancing `gActiveUnit->ai_counter` if reachable in
// `gMapMovement`. Matches `0x0803A450`.
bool AiGetNextPatrolWaypoint(GbaPtr<AiPos> out_pos);

// Checks for enemies in weapon range (recording the count in `gAiState[0x86]`)
// or moves towards the next patrol waypoint. Matches `0x0803A4D8`.
bool AiScriptActionPatrolOrAttack(GbaPtr<const uint8_t> params);

// Sets `gAiState[0x86]` to 1 if `gActiveUnit` is inside `[x1..x2] x [y1..y2]`,
// else 0. Matches `0x0803A548`.
bool AiScriptActionIsActiveUnitInRect(GbaPtr<const uint8_t> params);

// Updates `gAiDecision` with a talk action (`action_id = 8`) between the two
// PIDs in `params[0]` and `params[1]`. Matches `0x0803A58C`.
bool AiScriptActionTalk(GbaPtr<const uint8_t> params);

// Temporarily switches `gActiveUnit` to the unit's leader, executes script A
// to probe its attack target UID, stores it in `gAiState[0x86]`, and restores
// `gActiveUnit`. Matches `0x0803A5BC`.
bool AiScriptActionProbeLeaderTarget(GbaPtr<const uint8_t> params);

// Target-lock predicates and actions (`0x0803A680..0x0803A7C8`).
bool AiIsUnitMatchingTargetPid(GbaPtr<const Unit> unit);
bool AiScriptActionProbePidTarget(GbaPtr<const uint8_t> params);
bool AiIsUnitMatchingCachedUid(GbaPtr<const Unit> unit);
bool AiIsUnitInDirectionOfCachedTarget(GbaPtr<const Unit> unit);
bool AiScriptActionAttackOrChaseCachedTarget(GbaPtr<const uint8_t> params);

// Probabilistic staff/attack, loot/attack, ballista, move, and wait script
// actions (`0x0803A828..0x0803AA60`).
bool AiScriptActionStaffOrAttack(GbaPtr<const uint8_t> params);
bool AiScriptActionLootOrAttack(GbaPtr<const uint8_t> params);
bool AiScriptActionBallista(GbaPtr<const uint8_t> params);
bool AiScriptActionMoveToCoord(GbaPtr<const uint8_t> params);
bool AiScriptActionWait(GbaPtr<const uint8_t> params);

// Staff action table lookup and dispatcher (`0x0803AA70`, `0x0803AAD8`).
int AiGetStaffActionIndex(uint16_t item);
bool AiTrySpecialAction(uint32_t pred_addr);

// Healing staff AI (`0x0803AB80..0x0803AF98`).
bool AiFindBestAdjacentStaffPos(int x, int y, GbaPtr<AiPos> out_pos);
void AiTryHealStaff(int slot, uint32_t pred_addr);
void AiTryPhysicStaff(int slot, uint32_t pred_addr);
void AiTryFortifyStaff(int slot);

// Registers all 28 AI action & staff functions in the FE7 native dispatch
// table.
void RegisterAiActionFunctions();

}  // namespace fe7

#endif  // FE7_GAME_AI_AI_ACTIONS_H_
