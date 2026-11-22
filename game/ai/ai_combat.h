// FE7 US (AE7E) Enemy AI combat-simulation loop, attack/steal/ballista
// execution, recovery-tile search, healing-item usage, and escape execution.
//
// Implements 18 Enemy AI functions (ROM 0x08038558..0x08038FA4 and
// 0x08039534..0x08039EFC).

#ifndef FE7_GAME_AI_AI_COMBAT_H_
#define FE7_GAME_AI_AI_COMBAT_H_

#include <cstdint>

#include "game/ai/ai.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// Fixed FE7 US ROM table & external-helper addresses used by AI combat.
inline constexpr uint32_t kAiBallistaItemIdsAddr = 0x081D3B64u;
inline constexpr uint32_t kAiRecoveryTerrainsAddr = 0x08B98AC4u;

inline constexpr uint32_t kAddrBattleGenerateSimulation = 0x080283A4u;
inline constexpr uint32_t kAddrBattleGenerateBallistaSimulation = 0x080283F0u;
inline constexpr uint32_t kAddrGetEmptyBallistaItemAt = 0x080346C8u;
inline constexpr uint32_t kAddrRemoveUnitFromBallista = 0x080347A8u;

// FE7 US ROM addresses for the 18 decompiled AI combat & recovery functions.
// (kAddrAiPerformAttack = 0x08038558u, kAddrAiPerformEscape = 0x08039894u,
//  kAddrAiPerformAttackInCurrentMovement = 0x080387B0u, and
//  kAddrAiMakeRangeMapForUnitAndMov = 0x080389B8u are declared in
//  ai_opcodes.h / ai_actions.h.)
inline constexpr uint32_t kAddrAiFillTargetWeaponRangeMap = 0x08038964u;
inline constexpr uint32_t kAddrAiSimulateBallistaAttack = 0x08038A7Cu;
inline constexpr uint32_t kAddrAiGetStealPositionScore = 0x08038BECu;
inline constexpr uint32_t kAddrAiTryStealAction = 0x08038C44u;
inline constexpr uint32_t kAddrAiSimulateBestBattleAgainstTarget = 0x08038DA4u;
inline constexpr uint32_t kAddrAiSimulateBestBallistaBattleAgainstTarget =
    0x08038E64u;
inline constexpr uint32_t kAddrAiGetCombatPositionScore = 0x08038F38u;
inline constexpr uint32_t kAddrAiIsCombatSimInvalid = 0x08038FA0u;
inline constexpr uint32_t kAddrAiSimulateBattleAndComputeScore = 0x08038FA4u;
inline constexpr uint32_t kAddrAiFindBestRecoveryTile = 0x08039534u;
inline constexpr uint32_t kAddrAiTryUseHealingItem = 0x080397DCu;
inline constexpr uint32_t kAddrAiTryPostMoveSpecialAction = 0x08039D74u;
inline constexpr uint32_t kAddrAiTryPostMoveActionOrAttack = 0x08039DA0u;
inline constexpr uint32_t kAddrAiTryStealFromPos = 0x08039EFCu;

// Top-level AI attack evaluation loops (`0x08038558`, `0x080387B0`).
void AiPerformAttack(uint32_t pred_addr);
void AiPerformAttackInCurrentMovement(uint32_t pred_addr);

// Range-map & ballista/steal simulation helpers (`0x08038964..0x08038FA4`).
void AiFillTargetWeaponRangeMap(GbaPtr<const Unit> target, uint16_t item);
void AiMakeRangeMapForUnitAndMov(GbaPtr<const Unit> unit, uint16_t mov,
                                 uint16_t item);
bool AiSimulateBallistaAttack(uint32_t pred_addr,
                              GbaPtr<AiCombatSimulationSt> out);
uint8_t AiGetStealPositionScore(int x, int y);
bool AiTryStealAction();
bool AiSimulateBestBattleAgainstTarget(GbaPtr<AiCombatSimulationSt> st);
bool AiSimulateBestBallistaBattleAgainstTarget(GbaPtr<AiCombatSimulationSt> st,
                                               uint16_t item);
uint32_t AiGetCombatPositionScore(int x, int y,
                                  GbaPtr<const AiCombatSimulationSt> st);
bool AiIsCombatSimInvalid(GbaPtr<const AiCombatSimulationSt> st);
bool AiSimulateBattleAndComputeScore(GbaPtr<AiCombatSimulationSt> st);

// Recovery, healing-item, escape, and post-move helpers
// (`0x08039534..0x08039EFC`).
bool AiFindBestRecoveryTile(GbaPtr<AiPos> out_pos);
bool AiTryUseHealingItem();
bool AiPerformEscape();
void AiTryPostMoveSpecialAction();
void AiTryPostMoveActionOrAttack();
bool AiTryStealFromPos(int x, int y);

// Registers all 18 AI combat & recovery functions in the FE7 native dispatch
// table.
void RegisterAiCombatFunctions();

}  // namespace fe7

#endif  // FE7_GAME_AI_AI_COMBAT_H_
