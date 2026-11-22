// FE7 US (AE7E) top-level Enemy AI decision, item ranking, recovery,
// range-counting, escape-point, and dance/play helpers.
//
// Implements 24 top-level AI functions (ROM 0x080357EC..0x080365B0 and
// 0x08039510..0x08039DD8): best-item ranking and auto-equip orchestration
// (`AiEquipBestItem`, `AiCanEquipBestItem`, `AiRankUnitItems`,
// `AiComputeEnemyThreatAtTile`, `AiEquipBestItemWithMask`,
// `AiChooseBestItemToEquip`, `AiUpdateUnitSpecialStaffFlags`,
// `AiIsWithinMoveBirdsEye`, `AiGetUnitAttackPower`), AI bytecode comparison
// (`AiCompare`), nearest-class search (`AiTryFindNearestUnitOfClass`), random
// movement (`AiTryMoveRandomly`), tile/range unit counting (`AiGetMoveCostAt`,
// `AiCountUnitsInRange`, `AiCountEnemiesInRange`, `AiCountAlliesInRange`,
// `AiCountUnitsNearby`, `AiCountEnemiesNearby`, `AiCountAlliesNearby`),
// danger-threshold and faction/unit recovery updates
// (`AiIsWithinDangerThreshold`, `AiUpdateFactionRecovery`,
// `AiUpdateUnitRecoveryMode`), escape-point lookup (`AiGetClosestEscapePoint`),
// and Dancer/Bard refresh selection (`AiTryDanceOrPlay`).

#ifndef FE7_GAME_AI_AI_DECIDE_H_
#define FE7_GAME_AI_AI_DECIDE_H_

#include <cstdint>

#include "game/ai/ai.h"
#include "game/unit.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// Fixed FE7 US ROM table addresses used by top-level AI helpers.
inline constexpr uint32_t kAiFactionUnitCountsAddr = 0x081D3B68u;
inline constexpr uint32_t kAiNearbyOffsetTableAddr = 0x08B97034u;
inline constexpr uint32_t kAiEnemyEscapeTableAddr = 0x08B97100u;
inline constexpr uint32_t kAiNpcEscapeTableAddr = 0x08B971C0u;
inline constexpr uint32_t kAiRecoveryThresholdTableAddr = 0x08B9727Cu;

// FE7 US ROM addresses for the 24 decompiled top-level Enemy AI functions.
inline constexpr uint32_t kAddrAiEquipBestItem = 0x080357ECu;
inline constexpr uint32_t kAddrAiCompare = 0x08035838u;
inline constexpr uint32_t kAddrAiTryFindNearestUnitOfClass = 0x0803598Cu;
inline constexpr uint32_t kAddrAiTryMoveRandomly = 0x08035C70u;
inline constexpr uint32_t kAddrAiGetMoveCostAt = 0x08035F48u;
inline constexpr uint32_t kAddrAiCountUnitsInRange = 0x0803631Cu;
inline constexpr uint32_t kAddrAiCountEnemiesInRange = 0x08036390u;
inline constexpr uint32_t kAddrAiCountAlliesInRange = 0x0803640Cu;
inline constexpr uint32_t kAddrAiCountUnitsNearby = 0x08036488u;
inline constexpr uint32_t kAddrAiCountEnemiesNearby = 0x08036514u;
inline constexpr uint32_t kAddrAiCountAlliesNearby = 0x080365B0u;
inline constexpr uint32_t kAddrAiIsWithinDangerThreshold = 0x08039510u;
inline constexpr uint32_t kAddrAiUpdateFactionRecovery = 0x08039704u;
inline constexpr uint32_t kAddrAiUpdateUnitRecoveryMode = 0x0803975Cu;
inline constexpr uint32_t kAddrAiGetClosestEscapePoint = 0x0803993Cu;
inline constexpr uint32_t kAddrAiCanEquipBestItem = 0x080399C4u;
inline constexpr uint32_t kAddrAiRankUnitItems = 0x080399F8u;
inline constexpr uint32_t kAddrAiComputeEnemyThreatAtTile = 0x08039B04u;
inline constexpr uint32_t kAddrAiEquipBestItemWithMask = 0x08039C58u;
inline constexpr uint32_t kAddrAiChooseBestItemToEquip = 0x08039CA4u;
inline constexpr uint32_t kAddrAiUpdateUnitSpecialStaffFlags = 0x08039CCCu;
inline constexpr uint32_t kAddrAiIsWithinMoveBirdsEye = 0x08039D04u;
inline constexpr uint32_t kAddrAiGetUnitAttackPower = 0x08039D48u;
inline constexpr uint32_t kAddrAiTryDanceOrPlay = 0x08039DD8u;

// Evaluates enemy threats at `gAiDecision`'s destination and equips the best
// matching melee/ranged weapon on `gActiveUnit`. Matches `func_080357EC`.
void AiEquipBestItem();

// Compares `*ptr` against `val` using AI comparison operator `op`
// (0: >, 1: >=, 2: ==, 3: <=, 4: <, 5: !=). Matches `func_08035838`.
bool AiCompare(GbaPtr<const uint8_t> ptr, uint8_t op, uint32_t val);

// Floods `gMapRange` unitlessly from `gActiveUnit` and finds the closest active
// unit whose class ID equals `jid`, writing its coordinates into `*out_pos`.
// Matches `func_0803598C`.
bool AiTryFindNearestUnitOfClass(uint32_t jid, GbaPtr<AiPos> out_pos);

// Floods `gActiveUnit`'s movement and sets a wait decision on a randomly chosen
// unoccupied reachable tile. Matches `func_08035C70`.
void AiTryMoveRandomly();

// Returns `gMapRange[y][x]` if `static_cast<int8_t>(gMapRange[y][x]) <= 0x77`
// and `(x, y)` is unoccupied or occupied by `gActiveUnitId`, else `0xFF`.
// Matches `func_08035F48`.
uint8_t AiGetMoveCostAt(int x, int y);

// Counts all units, non-allied units, or allied units on tiles with nonzero
// `gMapRange`. Matches `func_0803631C`, `func_08036390`, `func_0803640C`.
int AiCountUnitsInRange();
int AiCountEnemiesInRange();
int AiCountAlliesInRange();

// Counts all units, non-allied units, or allied units at the relative offsets
// listed in `0x08B97034` around `(x, y)`. Matches `func_08036488`,
// `func_08036514`, `func_080365B0`.
int AiCountUnitsNearby(int16_t x, int16_t y);
int AiCountEnemiesNearby(int16_t x, int16_t y);
int AiCountAlliesNearby(int16_t x, int16_t y);

// Returns true iff `gMapOther[y][x] <= threshold`. Matches `func_08039510`.
bool AiIsWithinDangerThreshold(int x, int y, uint8_t threshold);

// Updates recovery-mode flags for every live unit in the current phase's
// faction. Matches `func_08039704`.
void AiUpdateFactionRecovery();

// Updates `unit->ai_flags` bit 0 (recovery mode) based on current HP% and the
// threshold table at `0x08B9727C`. Returns true if in recovery mode.
// Matches `func_0803975C`.
bool AiUpdateUnitRecoveryMode(GbaPtr<Unit> unit);

// Returns a pointer to the closest reachable chapter escape point in
// `0x08B97100` (enemy) / `0x08B971C0` (NPC), or null. Matches `func_0803993C`.
GbaPtr<const uint8_t> AiGetClosestEscapePoint();

// Returns true if `gActiveUnit` is eligible to auto-equip a defensive weapon
// after deciding its move. Matches `func_080399C4`.
bool AiCanEquipBestItem();

// Populates `out_ranks[0..4]` with range/low-uses/staff flags in the low byte
// and item might in the high byte. Returns the exact raw `r0` value produced
// by `func_080399F8` (0 if inventory is empty, nonzero otherwise).
uint32_t AiRankUnitItems(GbaPtr<uint16_t> out_ranks);

// Fills `gMapOther` with enemy threat power and accumulates total ranged,
// melee, and combined enemy power that can reach `(x, y)`.
// Matches `func_08039B04`.
void AiComputeEnemyThreatAtTile(int x, int y, GbaPtr<uint16_t> out_ranged,
                                GbaPtr<uint16_t> out_melee,
                                GbaPtr<uint16_t> out_total);

// Equips the highest-might item slot in `item_ranks` matching `mask` if its
// slot index is `> 0`. Matches `func_08039C58`.
void AiEquipBestItemWithMask(uint32_t mask, GbaPtr<const uint16_t> item_ranks);

// Chooses melee (`mask = 1`) or ranged (`mask = 2`) auto-equip based on
// `melee_threat` vs `ranged_threat`. Matches `func_08039CA4`.
void AiChooseBestItemToEquip(uint16_t ranged_threat, uint16_t melee_threat,
                             uint16_t total_threat,
                             GbaPtr<const uint16_t> item_ranks);

// Sets `gActiveUnit->ai_flags` special staff bits (`0x04` for Heal..Physic,
// `0x10` for Unlock/Torch/etc. ID `0x56`). Matches `func_08039CCC`.
void AiUpdateUnitSpecialStaffFlags(uint16_t item);

// Returns true if Manhattan distance from `unit` to `(x, y)` is within
// `unit`'s movement (`base_mov + bonus_mov`). Matches `func_08039D04`.
bool AiIsWithinMoveBirdsEye(GbaPtr<const Unit> unit, int x, int y);

// Stores `unit`'s equipped weapon into `*out_weapon` and returns
// `GetUnitPower(unit) + GetItemMight(weapon)`. Matches `func_08039D48`.
int AiGetUnitAttackPower(GbaPtr<const Unit> unit, GbaPtr<uint16_t> out_weapon);

// If `gActiveUnit` is a Dancer/Bard, selects the highest-level adjacent allied
// non-refresher at `(x, y)` and records a dance/play decision (`action_id =
// 7`). Matches `func_08039DD8`.
bool AiTryDanceOrPlay(int x, int y);

// Registers all 24 top-level Enemy AI functions in the dispatch table.
void RegisterAiDecideFunctions();

}  // namespace fe7

#endif  // FE7_GAME_AI_AI_DECIDE_H_
