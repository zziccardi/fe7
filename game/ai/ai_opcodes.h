// FE7 US (AE7E) Enemy AI script opcode handlers (`ops 0x04..0x1B`), berserk
// helpers, obstacle-attack scoring, and movement/target-search helpers.
//
// Implements 41 Enemy AI functions (ROM 0x0803589C..0x08036CEC and
// 0x08037A68..0x08038548), completing the entire 28-opcode AI script engine
// (`0x081D3678`) and all movement/terrain/target-search helpers in
// `0x080357EC..0x08038558`.

#ifndef FE7_GAME_AI_AI_OPCODES_H_
#define FE7_GAME_AI_AI_OPCODES_H_

#include <cstdint>

#include "game/ai/ai.h"
#include "game/map/map_range.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// Fixed FE7 US ROM table & external-helper addresses used by AI opcodes.
inline constexpr uint32_t kAiCardinalOffsetsAddr = 0x081D3670u;
inline constexpr uint32_t kAiLootTerrainsNoKeyAddr = 0x08B97098u;
inline constexpr uint32_t kAiLootTerrainsWithKeyAddr = 0x08B9709Cu;
inline constexpr uint32_t kAiObstacleTerrainsAddr = 0x08B970C4u;

// External helper ROM addresses called via GbaCall() (until decompiled in
// subsequent batches).
inline constexpr uint32_t kAddrAiPerformAttack = 0x08038558u;
inline constexpr uint32_t kAddrAiPerformEscape = 0x08039894u;
inline constexpr uint32_t kAddrAiTrySpecialAction = 0x0803AAD8u;
inline constexpr uint32_t kAddrAiTryDoorOrChestAction = 0x0803BA18u;
inline constexpr uint32_t kAddrAiMapFloodNoDoors = 0x0803BF60u;
inline constexpr uint32_t kAddrAiMapFloodNoDoorsUnit = 0x0803BF8Cu;
inline constexpr uint32_t kAddrAiMapFloodObstacles = 0x0803C058u;
inline constexpr uint32_t kAddrAiMapFloodRangeWithUnit = 0x0803C0FCu;

// FE7 US ROM addresses for the 41 decompiled AI opcode & helper functions.
inline constexpr uint32_t kAddrAiTryFindUnitByPid = 0x0803589Cu;
inline constexpr uint32_t kAddrAiTryFindClosestUnitMatching = 0x08035A38u;
inline constexpr uint32_t kAddrAiTryFindClosestUnitMatchingNoDoors =
    0x08035B54u;
inline constexpr uint32_t kAddrAiFindNearestTerrainInList = 0x08035E44u;
inline constexpr uint32_t kAddrAiFindNearestAdjacentToTerrainInList =
    0x08035FA0u;
inline constexpr uint32_t kAddrAiFindNearestDoorOrChest = 0x080360E8u;
inline constexpr uint32_t kAddrAiFindBestAdjacentPosition = 0x08036810u;
inline constexpr uint32_t kAddrAiFindNearestLootableTile = 0x080369F4u;
inline constexpr uint32_t kAddrAiMoveTowards = 0x08036B00u;
inline constexpr uint32_t kAddrAiMoveTowardsNoDoors = 0x08036CECu;

inline constexpr uint32_t kAddrAiScriptCmdAttackPid = 0x08037A68u;
inline constexpr uint32_t kAddrAiScriptCmdAttack = 0x08037B00u;
inline constexpr uint32_t kAddrAiScriptCmdNop06 = 0x08037B70u;
inline constexpr uint32_t kAddrAiScriptCmdAttackInPlace = 0x08037B78u;
inline constexpr uint32_t kAddrAiScriptCmdAttackJid = 0x08037BD8u;
inline constexpr uint32_t kAddrAiScriptCmdAction09 = 0x08037C28u;
inline constexpr uint32_t kAddrAiScriptCmdAction0A = 0x08037C44u;
inline constexpr uint32_t kAddrAiScriptCmdAction0B = 0x08037C60u;
inline constexpr uint32_t kAddrAiScriptCmdMoveToCoord = 0x08037C7Cu;
inline constexpr uint32_t kAddrAiScriptCmdMoveToPid = 0x08037CC8u;
inline constexpr uint32_t kAddrAiScriptCmdNop0E = 0x08037D84u;
inline constexpr uint32_t kAddrAiScriptCmdMoveToJid = 0x08037D8Cu;
inline constexpr uint32_t kAddrAiScriptCmdLoot = 0x08037DD0u;
inline constexpr uint32_t kAddrAiScriptCmdMoveToSafe = 0x08037EC8u;
inline constexpr uint32_t kAddrAiScriptCmdMoveToEnemy = 0x08037F0Cu;
inline constexpr uint32_t kAddrAiScriptCmdMoveToEnemyNoDoors = 0x08037F8Cu;
inline constexpr uint32_t kAddrAiScriptCmdNop14 = 0x0803800Cu;
inline constexpr uint32_t kAddrAiScriptCmdNop15 = 0x08038014u;
inline constexpr uint32_t kAddrAiScriptCmdMoveRandom = 0x0803801Cu;
inline constexpr uint32_t kAddrAiScriptCmdEscape = 0x08038030u;
inline constexpr uint32_t kAddrAiGetObstacleAttackPosScore = 0x08038054u;
inline constexpr uint32_t kAddrAiFindBestObstacleAttackPos = 0x080380A8u;
inline constexpr uint32_t kAddrAiFindNearestObstacleTerrain = 0x08038218u;
inline constexpr uint32_t kAddrAiScriptCmdAttackObstacle = 0x0803831Cu;
inline constexpr uint32_t kAddrAiScriptCmdMoveToTerrain = 0x080383E0u;
inline constexpr uint32_t kAddrAiScriptCmdMoveToTerrainList = 0x0803845Cu;
inline constexpr uint32_t kAddrAiScriptCmdLabel = 0x080384D8u;
inline constexpr uint32_t kAddrAiBerserkAction = 0x080384ECu;
inline constexpr uint32_t kAddrAiBerserkMove = 0x0803850Cu;
inline constexpr uint32_t kAddrAiReturnTrue = 0x08038544u;
inline constexpr uint32_t kAddrAiCheckJobRankAndReturnTrue = 0x08038548u;

// Movement, terrain, and target-search helpers (0x0803589C..0x08036CEC).
bool AiTryFindUnitByPid(int pid, GbaPtr<AiPos> out_pos);
bool AiTryFindClosestUnitMatching(uint32_t pred_addr, GbaPtr<AiPos> out_pos);
bool AiTryFindClosestUnitMatchingNoDoors(uint32_t pred_addr,
                                         GbaPtr<AiPos> out_pos);
bool AiFindNearestTerrainInList(GbaPtr<const uint8_t> terrain_list,
                                uint32_t flags, GbaPtr<AiPos> out_pos);
bool AiFindNearestAdjacentToTerrainInList(GbaPtr<const uint8_t> terrain_list,
                                          uint32_t flags,
                                          GbaPtr<AiPos> out_pos);
bool AiFindNearestAdjacentToTerrainInListOut(GbaPtr<const uint8_t> terrain_list,
                                             uint32_t flags, AiPos* out_pos);
bool AiFindNearestDoorOrChest(uint32_t flags, GbaPtr<AiPos> out_pos);
bool AiFindNearestDoorOrChestOut(uint32_t flags, AiPos* out_pos);
bool AiFindBestAdjacentPosition(int x, int y, uint32_t score_fn_addr,
                                GbaPtr<AiPos> out_pos);
bool AiFindNearestLootableTile(GbaPtr<AiPos> out_pos,
                               GbaPtr<uint8_t> out_key_slot);
void AiMoveTowards(int16_t x, int16_t y, uint8_t action_id,
                   uint8_t danger_threshold, uint8_t unitless_flood);
void AiMoveTowardsNoDoors(int16_t x, int16_t y, uint8_t action_id,
                          uint8_t danger_threshold, uint8_t unitless_flood);

// Remaining AI script opcode handlers (`ops 0x04..0x1B`) & helpers.
void AiScriptCmdAttackPid(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdAttack(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdNop06(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdAttackInPlace(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdAttackJid(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdAction09(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdAction0A(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdAction0B(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdMoveToCoord(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdMoveToPid(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdNop0E(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdMoveToJid(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdLoot(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdMoveToSafe(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdMoveToEnemy(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdMoveToEnemyNoDoors(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdNop14(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdNop15(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdMoveRandom(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdEscape(GbaPtr<uint8_t> pc_ptr);
uint32_t AiGetObstacleAttackPosScore(int x, int y);
bool AiFindBestObstacleAttackPos(int target_x, int target_y,
                                 GbaPtr<AiPos> out_pos,
                                 GbaPtr<uint8_t> out_slot);
bool AiFindNearestObstacleTerrain(GbaPtr<const uint8_t> terrain_list,
                                  uint32_t flags, GbaPtr<AiPos> out_pos);
void AiScriptCmdAttackObstacle(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdMoveToTerrain(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdMoveToTerrainList(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdLabel(GbaPtr<uint8_t> pc_ptr);
void AiBerserkAction();
void AiBerserkMove();
bool AiReturnTrue();
bool AiCheckJobRankAndReturnTrue(GbaPtr<const uint8_t> jid_ptr);

// Registers all 41 AI opcode & movement/target-search functions.
void RegisterAiOpcodeFunctions();

}  // namespace fe7

#endif  // FE7_GAME_AI_AI_OPCODES_H_
