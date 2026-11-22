// FE7 US (AE7E) Enemy AI script bytecode interpreter, target predicates,
// steal/chest helpers, safe-position & closest-valid-position search.
//
// Implements 30 Enemy AI functions (ROM 0x080366F0..0x08037A28):
//   - Range, steal/chest, safe-position & closest-valid-position helpers
//     (`AiMakeMoveRangeMapsForUnitAndStaff`,
//     `AiMakeMoveRangeMapsForUnitAndItem`, `AiGetStealItemPriority`,
//     `AiGetBestStealableItemSlot`, `AiFindSafestReachableLocation`,
//     `AiFindChestKeySlot`, `AiGetUnitClosestValidPosition`,
//     `AiIsUnitAliveByPid`, `AiIsWithinManhattanDistance`,
//     `AiIsSpecialLootableTerrain`, `AiRefreshAllBlueUnitFlags`,
//     `AiCountAlliesInRecoveryMode`, `AiCountAlliesInRangeNeedingRecovery`,
//     `AiUnitHasUsableStaffOrMagic`, `AiFloodMovementForUnit`).
//   - AI script bytecode interpreter & core opcodes/target predicates
//     (`AiTryExecScriptA`, `AiExecFallbackScriptA`, `AiTryExecScriptB`,
//     `AiExecFallbackScriptB`, `AiScriptExec`, `AiScriptCmdConditionalGoto`,
//     `AiScriptCmdCallFunc`, `AiScriptCmdChangeAi`, `AiScriptCmdGoto`,
//     `AiIsUnitEnemy`, `AiIsUnitNotActive`, `AiIsUnitEnemyNotInPidList`,
//     `AiIsUnitEnemyOrInPidList`, `AiIsUnitEnemyWithPid`,
//     `AiIsUnitEnemyWithJid`).

#ifndef FE7_GAME_AI_AI_SCRIPT_H_
#define FE7_GAME_AI_AI_SCRIPT_H_

#include <cstdint>
#include <type_traits>

#include "game/ai/ai.h"
#include "game/unit.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// 16-byte FE7 AI script bytecode instruction (`AiScr`).
struct AiScriptCmd {
  uint8_t cmd;        // 0x00: Opcode (0x00..0x1B)
  uint8_t arg1;       // 0x01: Comparison op / new AI1 / param
  uint8_t arg2;       // 0x02: Priority/chance (`gAiState + 0x7E`) / new AI2
  uint8_t arg3;       // 0x03: Branch label ID
  uint32_t u32_arg4;  // 0x04: Immediate value / target PID or JID in low byte
  uint32_t u32_arg8;  // 0x08: Pointer operand (byte ptr, list ptr, or callback)
  uint32_t u32_argC;  // 0x0C: Extra callback parameter
};

static_assert(sizeof(AiScriptCmd) == 16);
static_assert(std::is_trivially_copyable_v<AiScriptCmd>);

// Fixed FE7 US IWRAM & ROM addresses for the AI script engine.
inline constexpr uint32_t kAiScriptDoneAddr = 0x030013B0u;
inline constexpr uint32_t kAiScriptKindAddr = 0x030013B4u;
inline constexpr uint32_t kAiScriptCurAddr = 0x030013B8u;
inline constexpr uint32_t kAiScriptFuncPtrAddr = 0x030013BCu;

inline constexpr uint32_t kAiScriptOpcodeTableAddr = 0x081D3678u;
inline constexpr uint32_t kAiFallbackScriptAAddr = 0x08B970A4u;
inline constexpr uint32_t kAiFallbackScriptBAddr = 0x08B970B4u;
inline constexpr uint32_t kAiStealPriorityListAddr = 0x08B97290u;
inline constexpr uint32_t kAiScriptTableBPtrAddr = 0x08B989E4u;
inline constexpr uint32_t kAiScriptTableAPtrAddr = 0x08B989F0u;

// FE7 US ROM addresses for the 30 decompiled AI script & targeting functions.
inline constexpr uint32_t kAddrAiMakeMoveRangeMapsForUnitAndStaff = 0x080366F0u;
inline constexpr uint32_t kAddrAiMakeMoveRangeMapsForUnitAndItem = 0x08036770u;
inline constexpr uint32_t kAddrAiGetStealItemPriority = 0x0803688Cu;
inline constexpr uint32_t kAddrAiGetBestStealableItemSlot = 0x080368C0u;
inline constexpr uint32_t kAddrAiFindSafestReachableLocation = 0x08036900u;
inline constexpr uint32_t kAddrAiFindChestKeySlot = 0x08036A8Cu;
inline constexpr uint32_t kAddrAiGetUnitClosestValidPosition = 0x08036ED8u;
inline constexpr uint32_t kAddrAiIsUnitAliveByPid = 0x0803707Cu;
inline constexpr uint32_t kAddrAiIsWithinManhattanDistance = 0x080370C8u;
inline constexpr uint32_t kAddrAiIsSpecialLootableTerrain = 0x0803710Cu;
inline constexpr uint32_t kAddrAiRefreshAllBlueUnitFlags = 0x0803715Cu;
inline constexpr uint32_t kAddrAiCountAlliesInRecoveryMode = 0x08037460u;
inline constexpr uint32_t kAddrAiCountAlliesInRangeNeedingRecovery =
    0x080374ACu;
inline constexpr uint32_t kAddrAiUnitHasUsableStaffOrMagic = 0x08037548u;
inline constexpr uint32_t kAddrAiFloodMovementForUnit = 0x0803758Cu;
inline constexpr uint32_t kAddrAiTryExecScriptA = 0x080375B8u;
inline constexpr uint32_t kAddrAiExecFallbackScriptA = 0x0803760Cu;
inline constexpr uint32_t kAddrAiTryExecScriptB = 0x08037648u;
inline constexpr uint32_t kAddrAiExecFallbackScriptB = 0x0803769Cu;
inline constexpr uint32_t kAddrAiScriptExec = 0x080376D8u;
inline constexpr uint32_t kAddrAiScriptCmdConditionalGoto = 0x08037744u;
inline constexpr uint32_t kAddrAiScriptCmdCallFunc = 0x080377E8u;
inline constexpr uint32_t kAddrAiScriptCmdChangeAi = 0x08037818u;
inline constexpr uint32_t kAddrAiScriptCmdGoto = 0x08037888u;
inline constexpr uint32_t kAddrAiIsUnitEnemy = 0x08037914u;
inline constexpr uint32_t kAddrAiIsUnitNotActive = 0x08037944u;
inline constexpr uint32_t kAddrAiIsUnitEnemyNotInPidList = 0x08037958u;
inline constexpr uint32_t kAddrAiIsUnitEnemyOrInPidList = 0x080379A0u;
inline constexpr uint32_t kAddrAiIsUnitEnemyWithPid = 0x080379E8u;
inline constexpr uint32_t kAddrAiIsUnitEnemyWithJid = 0x08037A28u;

// Floods `unit`'s movement onto `gMapMovement` and marks staff range
// (`min(GetUnitPower(unit), 20)`) onto `gMapRange`. Matches `func_080366F0`.
void AiMakeMoveRangeMapsForUnitAndStaff(GbaPtr<const Unit> unit);

// Floods `unit`'s movement onto `gMapMovement` and marks `item`'s
// `[min_range, max_range]` ring onto `gMapRange`. Matches `func_08036770`.
void AiMakeMoveRangeMapsForUnitAndItem(GbaPtr<const Unit> unit, uint16_t item);

// Returns 0-based steal priority index of `iid` in `0x08B97290`, or -1.
// Matches `func_0803688C`.
int AiGetStealItemPriority(uint16_t iid);

// Returns the inventory slot index of the highest-priority stealable item on
// `unit`, or -1 if none. Matches `func_080368C0`.
int8_t AiGetBestStealableItemSlot(GbaPtr<const Unit> unit);

// Finds the reachable unoccupied (or self-occupied) tile with minimum danger in
// `gMapOther` and writes it to `*out_pos`. Matches `func_08036900`.
bool AiFindSafestReachableLocation(GbaPtr<const Unit> unit,
                                   GbaPtr<AiPos> out_pos);

// Searches `gActiveUnit` for a Chest Key (`0x68`) or Lockpick (`0x6A` for
// thieves). Sets `ai_flags |= 8` if inventory is full. Matches `func_08036A8C`.
bool AiFindChestKeySlot(GbaPtr<uint8_t> out_slot);

// Finds the closest valid unoccupied tile to `(x, y)` reachable by `unit` and
// stores it in `*out_pos`. Matches `AiGetUnitClosestValidPosition`
// (0x08036ED8).
bool AiGetUnitClosestValidPosition(GbaPtr<const Unit> unit, int16_t x,
                                   int16_t y, GbaPtr<AiPos> out_pos);

// Returns true if a unit with character ID `pid` is alive/deployed (or
// rescued). Matches `func_0803707C`.
bool AiIsUnitAliveByPid(uint16_t pid);

// Returns true if Manhattan distance between `(x1, y1)` and `(x2, y2)` is
// `<= max_dist`. Matches `func_080370C8`.
bool AiIsWithinManhattanDistance(int16_t x1, int16_t y1, uint8_t x2, uint8_t y2,
                                 uint8_t max_dist);

// Returns true if terrain at `(x, y)` is a lootable Village/House/Ruins or an
// openable Chest (when `gActiveUnit` holds a key/lockpick). Matches
// `0x0803710C`.
bool AiIsSpecialLootableTerrain(uint8_t x, uint8_t y);

// Refreshes `gAiState + 0x85` (max blue movement) and AI item/staff/ally flags
// across all live Blue units (`1..0x3F`). Matches `func_0803715C`.
void AiRefreshAllBlueUnitFlags();

// Counts live units in the current phase with recovery bit 0 set.
// Matches `func_08037460`.
int AiCountAlliesInRecoveryMode();

// Counts allied units in `gMapRange` with recovery bit 0 set.
// Matches `func_080374AC`.
int AiCountAlliesInRangeNeedingRecovery();

// Returns true if `unit` holds any usable staff/magic-staff item.
// Matches `func_08037548`.
bool AiUnitHasUsableStaffOrMagic(GbaPtr<const Unit> unit);

// Floods `unit`'s movement (or 0-mov if `gAiState[0x7B] & 2` is set).
// Matches `func_0803758C`.
void AiFloodMovementForUnit(GbaPtr<const Unit> unit);

// Executes one step of `gActiveUnit`'s AI1/AI2 script (or fallback script) and
// returns `gAiScriptDone` (`0x030013B0`). Matches `0x080375B8..0x0803769C`.
int8_t AiTryExecScriptA();
int8_t AiExecFallbackScriptA();
int8_t AiTryExecScriptB();
int8_t AiExecFallbackScriptB();

// Dispatches `*gAiScriptCur` through the 28-entry opcode table at `0x081D3678`.
// Matches `func_080376D8`.
void AiScriptExec(GbaPtr<uint8_t> pc_ptr);

// Core AI script opcode handlers (`0x00` conditional goto, `0x01` call native
// or ROM callback, `0x02` change AI1/AI2, `0x03` unconditional goto).
void AiScriptCmdConditionalGoto(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdCallFunc(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdChangeAi(GbaPtr<uint8_t> pc_ptr);
void AiScriptCmdGoto(GbaPtr<uint8_t> pc_ptr);

// AI target-selection predicates evaluated against candidate `unit`.
bool AiIsUnitEnemy(GbaPtr<const Unit> unit);
bool AiIsUnitNotActive(GbaPtr<const Unit> unit);
bool AiIsUnitEnemyNotInPidList(GbaPtr<const Unit> unit);
bool AiIsUnitEnemyOrInPidList(GbaPtr<const Unit> unit);
bool AiIsUnitEnemyWithPid(GbaPtr<const Unit> unit);
bool AiIsUnitEnemyWithJid(GbaPtr<const Unit> unit);

// Registers all 30 AI script & targeting functions in the dispatch table.
void RegisterAiScriptFunctions();

}  // namespace fe7

#endif  // FE7_GAME_AI_AI_SCRIPT_H_
