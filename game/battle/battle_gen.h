// FE7 US (AE7E) top-level battle simulation, real-combat, ballista-combat,
// display-stats, and post-battle game-state update entrypoints.
//
// Implements 9 functions (ROM 0x080281C8..0x0802848C):
// `BattleGenerateSimulationInternal`, `BattleGenerateRealInternal`,
// `BattleApplyGameStateUpdates`, `BattleGenerateSimulation`,
// `BattleGenerateReal`, `BattleGenerateBallistaSimulation`,
// `BattleGenerateBallistaReal`, `BattleGenerate`, and
// `BattleGenerateDisplayStats`.

#ifndef FE7_GAME_BATTLE_BATTLE_GEN_H_
#define FE7_GAME_BATTLE_BATTLE_GEN_H_

#include <cstdint>

#include "game/unit.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// External helper ROM addresses called via GbaCall().
inline constexpr uint32_t kAddrSetBattleUnitTerrainBonusesAuto = 0x08028728u;
inline constexpr uint32_t kAddrSetBattleUnitWeapon = 0x0802877Cu;
inline constexpr uint32_t kAddrSetBattleUnitWeaponBallista = 0x0802896Cu;
inline constexpr uint32_t kAddrComputeBattleObstacleStats = 0x0802A2D8u;
inline constexpr uint32_t kAddrBattleApplySupportBonusesAfter = 0x0802A490u;
inline constexpr uint32_t kAddrBattleApplyMiscUpdates = 0x0802A494u;
inline constexpr uint32_t kAddrBattleUnwindScripted = 0x0802A8A8u;
inline constexpr uint32_t kAddrDisableAllLightRunes = 0x0802C29Cu;
inline constexpr uint32_t kAddrRecordUnitBattleStats = 0x0809FD30u;
inline constexpr uint32_t kAddrRecordBattleStatsAfterCombat = 0x080A02B8u;

// FE7 US ROM addresses for the 9 decompiled battle generation entrypoints.
inline constexpr uint32_t kAddrBattleGenerateSimulationInternal = 0x080281C8u;
inline constexpr uint32_t kAddrBattleGenerateRealInternal = 0x08028298u;
inline constexpr uint32_t kAddrBattleApplyGameStateUpdates = 0x08028380u;
inline constexpr uint32_t kAddrBattleGenerateSimulation = 0x080283A4u;
inline constexpr uint32_t kAddrBattleGenerateReal = 0x080283DCu;
inline constexpr uint32_t kAddrBattleGenerateBallistaSimulation = 0x080283F0u;
inline constexpr uint32_t kAddrBattleGenerateBallistaReal = 0x08028410u;
inline constexpr uint32_t kAddrBattleGenerate = 0x08028424u;
inline constexpr uint32_t kAddrBattleGenerateDisplayStats = 0x0802848Cu;

// Core simulation & real-combat orchestration (`0x080281C8`, `0x08028298`).
void BattleGenerateSimulationInternal(GbaPtr<Unit> actor, GbaPtr<Unit> target,
                                      int x, int y, int item_slot);
void BattleGenerateRealInternal(GbaPtr<Unit> actor, GbaPtr<Unit> target);

// Commits post-battle unit/ballista/support state updates (`0x08028380`).
void BattleApplyGameStateUpdates();

// Public battle entrypoints (`0x080283A4..0x0802848C`).
void BattleGenerateSimulation(GbaPtr<Unit> actor, GbaPtr<Unit> target, int x,
                              int y, int item_slot);
void BattleGenerateReal(GbaPtr<Unit> actor, GbaPtr<Unit> target);
void BattleGenerateBallistaSimulation(GbaPtr<Unit> actor, GbaPtr<Unit> target,
                                      int x, int y);
void BattleGenerateBallistaReal(GbaPtr<Unit> actor, GbaPtr<Unit> target);
void BattleGenerate(GbaPtr<Unit> actor, GbaPtr<Unit> target);
void BattleGenerateDisplayStats(GbaPtr<Unit> unit, int8_t item_slot);

// Registers all 9 battle generation functions in the FE7 native dispatch table.
void RegisterBattleGenFunctions();

}  // namespace fe7

#endif  // FE7_GAME_BATTLE_BATTLE_GEN_H_
