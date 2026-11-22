// FE7 US (AE7E) Enemy AI decision state, reachability, and combat scoring.
//
// Implements 26 core AI functions (ROM 0x08034E30..0x08034E94,
// 0x08035D50..0x08035E28, 0x08037044..0x08037380, and 0x08039010..0x080393BC):
// decision record management (`AiClearDecision`, `AiSetDecision`,
// `AiUpdateDecision`), birds-eye distance and list checks, unit AI flag/init
// helpers, all 8 combat score components (`AiComputeCombatScore`), position
// evaluation (`AiFindBestPositionInRange`), and danger-map refresh.

#ifndef FE7_GAME_AI_AI_H_
#define FE7_GAME_AI_AI_H_

#include <cstdint>
#include <type_traits>

#include "game/unit.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// Active AI action decision (`gAiDecision` at EWRAM 0x0203A97C, 12 bytes).
struct AiDecision {
  uint8_t action_id;        // 0x00
  uint8_t unit_id;          // 0x01
  uint8_t x_move;           // 0x02
  uint8_t y_move;           // 0x03
  uint8_t unk_04;           // 0x04
  uint8_t unk_05;           // 0x05
  uint8_t unit_target_id;   // 0x06
  uint8_t item_slot;        // 0x07
  uint8_t x_target;         // 0x08
  uint8_t y_target;         // 0x09
  int8_t action_performed;  // 0x0A
  uint8_t pad_0b;           // 0x0B
};

static_assert(sizeof(AiDecision) == 12);
static_assert(std::is_trivially_copyable_v<AiDecision>);

// 2D 16-bit position output used by `AiFindBestPositionInRange`.
struct AiPos {
  int16_t x;
  int16_t y;
};

static_assert(sizeof(AiPos) == 4);
static_assert(std::is_trivially_copyable_v<AiPos>);

// Combat simulation candidate scored by `AiComputeCombatScore` (12 bytes).
struct AiCombatSimulationSt {
  uint8_t x_move;      // 0x00
  uint8_t y_move;      // 0x01
  uint8_t target_id;   // 0x02
  uint8_t pad_03;      // 0x03
  uint16_t item_slot;  // 0x04
  uint16_t pad_06;     // 0x06
  uint32_t score;      // 0x08
};

static_assert(sizeof(AiCombatSimulationSt) == 12);
static_assert(std::is_trivially_copyable_v<AiCombatSimulationSt>);

// Combat-score weight table entry (`0x081D36F4`, 20 bytes per entry).
struct AiCombatScoreCoefficients {
  uint8_t coeff_damage_dealt;      // 0x00
  uint8_t coeff_low_hp_opponent;   // 0x01
  uint8_t coeff_friend_zone;       // 0x02
  uint8_t coeff_class_rank_bonus;  // 0x03
  uint8_t coeff_turn_number;       // 0x04
  uint8_t coeff_damage_taken;      // 0x05
  uint8_t coeff_danger;            // 0x06
  uint8_t coeff_low_hp_self;       // 0x07
  uint8_t class_rank_bonuses[12];  // 0x08..0x13
};

static_assert(sizeof(AiCombatScoreCoefficients) == 20);
static_assert(std::is_trivially_copyable_v<AiCombatScoreCoefficients>);

// Fixed FE7 US RAM & ROM table addresses for the AI subsystem.
inline constexpr uint32_t kAiPlayStatusAddr = 0x0202BBF8u;
inline constexpr uint32_t kAiActiveUnitIdAddr = 0x0202BD48u;
inline constexpr uint32_t kAiMapOtherAddr = 0x0202E3F4u;
inline constexpr uint32_t kAiStateAddr = 0x0203A8ECu;
inline constexpr uint32_t kAiDecisionAddr = 0x0203A97Cu;
inline constexpr uint32_t kAiScoreCoeffPtrAddr = 0x030013C0u;
inline constexpr uint32_t kAiActiveUnitAddr = 0x03004690u;

inline constexpr uint32_t kAiCombatScoreCoeffTableAddr = 0x081D36F4u;
inline constexpr uint32_t kAiJobRankTableAddr = 0x08B970C8u;
inline constexpr uint32_t kAiFriendZoneCombatTableAddr = 0x08B989FCu;
inline constexpr uint32_t kAiFriendZonePosTableAddr = 0x08B98A60u;

// External helper ROM address called via GbaCall().
inline constexpr uint32_t kAddrAiFillDangerMap = 0x080393E8u;

// FE7 US ROM addresses for the 26 decompiled Enemy AI functions.
inline constexpr uint32_t kAddrAiClearDecision = 0x08034E30u;
inline constexpr uint32_t kAddrAiSetDecision = 0x08034E50u;
inline constexpr uint32_t kAddrAiUpdateDecision = 0x08034E94u;
inline constexpr uint32_t kAddrAiIsWithinReachByBirdsEyeDistance = 0x08035D50u;
inline constexpr uint32_t kAddrAiCouldReachByBirdsEyeDistance = 0x08035DA4u;
inline constexpr uint32_t kAddrAiIsInShortList = 0x08035E0Cu;
inline constexpr uint32_t kAddrAiIsInByteList = 0x08035E28u;
inline constexpr uint32_t kAddrAiGetJobRank = 0x08037044u;
inline constexpr uint32_t kAddrAiUpdateUnitStaffFlags = 0x08037218u;
inline constexpr uint32_t kAddrAiUpdateUnitItemFlags = 0x08037260u;
inline constexpr uint32_t kAddrAiCountNearbyAllies = 0x080372ACu;
inline constexpr uint32_t kAddrUnitInitAiFromInfo = 0x08037350u;
inline constexpr uint32_t kAddrAiFindBestPositionInRange = 0x08037380u;
inline constexpr uint32_t kAddrAiGetDamageDealtCombatScoreComponent =
    0x08039010u;
inline constexpr uint32_t kAddrAiGetOpponentLowHpScoreComponent = 0x08039070u;
inline constexpr uint32_t kAddrAiGetFriendZoneCombatScoreComponent =
    0x08039094u;
inline constexpr uint32_t kAddrAiGetTargetClassCombatScoreComponent =
    0x08039138u;
inline constexpr uint32_t kAddrAiGetTurnCombatScoreComponent = 0x0803916Cu;
inline constexpr uint32_t kAddrAiGetDamageTakenScoreComponent = 0x08039184u;
inline constexpr uint32_t kAddrAiGetDangerScoreComponent = 0x080391E4u;
inline constexpr uint32_t kAddrAiGetLowHpScoreComponent = 0x0803921Cu;
inline constexpr uint32_t kAddrAiComputeCombatScore = 0x08039240u;
inline constexpr uint32_t kAddrAiGetInRangeCombatPositionScoreComponent =
    0x080392B4u;
inline constexpr uint32_t kAddrAiGetTerrainCombatPositionScoreComponent =
    0x08039304u;
inline constexpr uint32_t kAddrAiGetFriendZonePositionScoreComponent =
    0x08039344u;
inline constexpr uint32_t kAddrAiRefreshDangerMap = 0x080393BCu;

// Zeroes `gAiDecision` (`0x0203A97C`, bytes 0..10). Matches `func_08034E30`.
void AiClearDecision();

// Populates `gAiDecision` and sets `action_performed = 1`.
// Matches FE7 US `func_08034E50`.
void AiSetDecision(int16_t x_move, int16_t y_move, uint8_t action_id,
                   uint8_t target_id, uint8_t item_slot, uint8_t x_target,
                   uint8_t y_target);

// Updates non-`0xFF` fields of `gAiDecision` and sets `action_performed = 1`.
// Matches FE7 US `func_08034E94`.
void AiUpdateDecision(uint8_t action_id, uint8_t target_id, uint8_t item_slot,
                      uint8_t x_target, uint8_t y_target);

// Returns true if Manhattan distance between `unit` and `target` is within
// `unit`'s movement + `GetItemMaxRange(item)`. Matches `func_08035D50`.
bool AiIsWithinReachByBirdsEyeDistance(GbaPtr<const Unit> unit,
                                       GbaPtr<const Unit> target,
                                       uint16_t item);

// Returns true if Manhattan distance is within both units' combined movement +
// `GetItemMaxRange(item)`. Matches `AiCouldReachByBirdsEyeDistance`.
bool AiCouldReachByBirdsEyeDistance(GbaPtr<const Unit> unit,
                                    GbaPtr<const Unit> target, uint16_t item);

// Searches 0-terminated `uint16_t` list for `item`. Matches `func_08035E0C`.
bool AiIsInShortList(GbaPtr<const uint16_t> list, uint16_t item);

// Searches 0-terminated `uint8_t` list for `item`. Matches `func_08035E28`.
bool AiIsInByteList(GbaPtr<const uint8_t> list, uint8_t item);

// Returns the AI class-rank index (`0..`) for `jid` from `0x08B970C8`.
// Matches FE7 US `func_08037044`.
uint8_t AiGetJobRank(uint8_t jid);

// Sets `unit->ai_flags` staff capability bits for `item`. Matches `0x08037218`.
void AiUpdateUnitStaffFlags(GbaPtr<Unit> unit, uint16_t item);

// Sets `unit->ai_flags` ranged-weapon/healing-item bits. Matches `0x08037260`.
void AiUpdateUnitItemFlags(GbaPtr<Unit> unit, uint16_t item);

// Counts allied units within range 1..8 of `unit` and stores the count in
// `unit->unk_46`. Matches FE7 US `func_080372AC`.
void AiCountNearbyAllies(GbaPtr<Unit> unit);

// Copies AI bytes `0x0C..0x0F` from chapter `UnitInfo` into `unit`.
// Matches FE7 US `UnitInitAiFromInfo` (0x08037350).
void UnitInitAiFromInfo(GbaPtr<Unit> unit, GbaPtr<const uint8_t> udef);

// Finds the highest-scoring reachable tile in `gMapRange` and writes it to
// `*out_pos`. Returns true if any tile was found. Matches `func_08037380`.
bool AiFindBestPositionInRange(GbaPtr<AiPos> out_pos);

// Combat-scoring component evaluators reading `gBattleActor`/`gBattleTarget`
// and `*gAiCombatScoreCoeffPtr` (`0x030013C0`).
int AiGetDamageDealtCombatScoreComponent();
int AiGetOpponentLowHpScoreComponent();
int AiGetFriendZoneCombatScoreComponent();
int AiGetTargetClassCombatScoreComponent();
int AiGetTurnCombatScoreComponent();
int AiGetDamageTakenScoreComponent();
int AiGetDangerScoreComponent();
int AiGetLowHpScoreComponent();

// Computes the combined combat score for `gBattleActor` vs `gBattleTarget` and
// stores it in `st->score`. Matches FE7 US `func_08039240`.
void AiComputeCombatScore(GbaPtr<AiCombatSimulationSt> st);

// Returns 50 if `(x, y)` is outside `target`'s equipped weapon range, else 0.
// Matches FE7 US `func_080392B4`.
int AiGetInRangeCombatPositionScoreComponent(int x, int y,
                                             GbaPtr<const Unit> target);

// Returns `gActiveUnit`'s terrain avoid + defense + resistance bonus at
// `(x, y)`. Matches FE7 US `func_08039304`.
int AiGetTerrainCombatPositionScoreComponent(int x, int y);

// Sums +5 per nearby ally and -5 per nearby enemy around `(x, y)` using
// `0x08B98A60`. Matches FE7 US `func_08039344`.
int AiGetFriendZonePositionScoreComponent(int x, int y);

// Populates `gMapOther` danger values once per AI turn (`gAiState + 0x7A`).
// Matches FE7 US `AiRefreshDangerMap` (0x080393BC).
void AiRefreshDangerMap();

// Registers all 26 Enemy AI functions in the dispatch table.
void RegisterAiFunctions();

}  // namespace fe7

#endif  // FE7_GAME_AI_AI_H_
