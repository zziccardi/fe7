// FE7 US (AE7E) post-battle EXP, level-up stat rolls, WEXP, and unit updates.
//
// Implements 26 post-battle progression functions (ROM 0x0802956C..0x0802A05C):
// EXP formulas (`GetUnitExpLevel`, `GetUnitRoundExp`, `GetUnitKillExpBonus`,
// `GetBattleUnitExpGain`, `GetBattleUnitStaffExp`), stat growth and autolevel
// rolls (`GetStatIncrease`, `GetAutoleveledStatIncrease`,
// `CheckBattleUnitLevelUp`, `CheckBattleUnitStatCaps`), promotion
// (`UnitPromote`), WEXP advancement (`GetBattleUnitUpdatedWeaponExp`), and
// post-combat EWRAM synchronization (`BattleApplyExpGains`,
// `BattleApplyUnitUpdates`, `UpdateUnitFromBattle`).

#ifndef FE7_GAME_BATTLE_BATTLE_EXP_H_
#define FE7_GAME_BATTLE_BATTLE_EXP_H_

#include <cstdint>

#include "game/battle.h"
#include "game/unit.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// Fixed FE7 US RAM addresses touched by post-battle update routines.
inline constexpr uint32_t kBattleExpScratchAddr = 0x0203A510u;
inline constexpr uint32_t kBattleUnitPaletteScratchAddr = 0x03002850u;

// External helper ROM addresses invoked via GbaCall().
inline constexpr uint32_t kAddrUnitRemoveInvalidItems = 0x08017688u;
inline constexpr uint32_t kAddrUnitCheckStatOverflow = 0x08017C64u;
inline constexpr uint32_t kAddrUpdateObstacleFromBattle = 0x0802A314u;
inline constexpr uint32_t kAddrGetTrap = 0x0802C318u;
inline constexpr uint32_t kAddrPidStatsAddExpGained = 0x080A009Cu;

// FE7 US ROM addresses for the 26 decompiled post-battle EXP/update functions.
inline constexpr uint32_t kAddrBattleApplyExpGains = 0x0802956Cu;
inline constexpr uint32_t kAddrGetStatIncrease = 0x080295E0u;
inline constexpr uint32_t kAddrGetAutoleveledStatIncrease = 0x08029604u;
inline constexpr uint32_t kAddrCanBattleUnitGainExp = 0x08029634u;
inline constexpr uint32_t kAddrCheckBattleUnitLevelUp = 0x08029660u;
inline constexpr uint32_t kAddrUnitPromote = 0x08029818u;
inline constexpr uint32_t kAddrGenerateBattleUnitStatGainsComparatively =
    0x08029904u;
inline constexpr uint32_t kAddrCheckBattleUnitStatCaps = 0x08029970u;
inline constexpr uint32_t kAddrBattleApplyUnitUpdates = 0x08029A70u;
inline constexpr uint32_t kAddrBattleGetExpGainStub = 0x08029AF8u;
inline constexpr uint32_t kAddrGetBattleUnitUpdatedWeaponExp = 0x08029AFCu;
inline constexpr uint32_t kAddrHasBattleUnitGainedWeaponLevel = 0x08029BE8u;
inline constexpr uint32_t kAddrUpdateUnitFromBattle = 0x08029C24u;
inline constexpr uint32_t kAddrUpdateUnitDuringBattle = 0x08029D0Cu;
inline constexpr uint32_t kAddrBattleApplyBallistaUpdates = 0x08029D38u;
inline constexpr uint32_t kAddrClearBattleExpScratch = 0x08029D6Cu;
inline constexpr uint32_t kAddrGetUnitExpLevel = 0x08029D7Cu;
inline constexpr uint32_t kAddrGetUnitRoundExp = 0x08029D9Cu;
inline constexpr uint32_t kAddrGetUnitPowerLevel = 0x08029DCCu;
inline constexpr uint32_t kAddrGetUnitClassKillExpBonus = 0x08029E08u;
inline constexpr uint32_t kAddrGetUnitExpMultiplier = 0x08029E30u;
inline constexpr uint32_t kAddrGetUnitKillExpBonus = 0x08029E74u;
inline constexpr uint32_t kAddrGetBattleUnitExpGain = 0x08029F10u;
inline constexpr uint32_t kAddrBattleApplyMiscActionExpGains = 0x08029F7Cu;
inline constexpr uint32_t kAddrGetBattleUnitStaffExp = 0x08029FF8u;
inline constexpr uint32_t kAddrBattleApplyStealExpGain = 0x0802A05Cu;

// Computes and applies EXP gains and level-ups to `gBattleActor` and
// `gBattleTarget`. Matches FE7 US `BattleApplyExpGains` (0x0802956C).
void BattleApplyExpGains();

// Rolls a stat increase for percentage `growth` (guaranteed +1 per 100%, plus
// 1-RN roll for the remainder). Matches FE7 US `GetStatIncrease` (0x080295E0).
int GetStatIncrease(int growth);

// Rolls an autoleveled stat increase over `level_count` levels with ±12.5%
// variance. Matches FE7 US `GetAutoleveledStatIncrease` (0x08029604).
int GetAutoleveledStatIncrease(int growth, int level_count);

// Returns true if `bu` is eligible to earn EXP (player faction with EXP !=
// 0xFF, or link-arena/debug override). Matches FE7 US `CanBattleUnitGainExp`.
bool CanBattleUnitGainExp(GbaPtr<BattleUnit> bu);

// Processes a level-up when `bu->unit.exp >= 100`, rolling all 7 stat growths
// (with +5% Afa's Drops bonus and up to 2 empty-level rerolls) and clamping to
// caps. Matches FE7 US `func_08029660`.
void CheckBattleUnitLevelUp(GbaPtr<BattleUnit> bu);

// Promotes `unit` into `unit->jinfo->jid_promote`, applying promotion bonuses,
// capping stats, updating weapon ranks, and resetting level/EXP to 1/0.
// Matches FE7 US `UnitPromote` (0x08029818).
void UnitPromote(GbaPtr<Unit> unit);

// Records stat deltas `bu->change_* = bu->unit.* - unit->*` (used for promotion
// display). Matches FE7 US `GenerateBattleUnitStatGainsComparatively`.
void GenerateBattleUnitStatGainsComparatively(GbaPtr<BattleUnit> bu,
                                              GbaPtr<const Unit> unit);

// Clamps `bu->change_*` so `unit->* + bu->change_*` does not exceed class/HP/
// Luck caps. Matches FE7 US `func_08029970`.
void CheckBattleUnitStatCaps(GbaPtr<const Unit> unit, GbaPtr<BattleUnit> bu);

// Writes `gBattleActor` and `gBattleTarget` back to their backing EWRAM `Unit`
// slots (or obstacle trap). Matches FE7 US `BattleApplyUnitUpdates`.
void BattleApplyUnitUpdates();

// Constant stub returning 1. Matches FE7 US `func_08029AF8`.
int BattleGetExpGainStub();

// Computes `bu`'s updated weapon EXP after combat, enforcing the single S-rank
// rule and promoted/unpromoted caps, or returns -1 if ineligible.
// Matches FE7 US `GetBattleUnitUpdatedWeaponExp` (0x08029AFC).
int GetBattleUnitUpdatedWeaponExp(GbaPtr<BattleUnit> bu);

// Returns true if `bu`'s post-combat WEXP crosses a weapon-rank threshold.
// Matches FE7 US `func_08029BE8`.
bool HasBattleUnitGainedWeaponLevel(GbaPtr<BattleUnit> bu);

// Applies HP, level/EXP, status, stat gains, WEXP, inventory, and PID EXP stats
// from `bu` to `unit`. Matches FE7 US `UpdateUnitFromBattle` (0x08029C24).
void UpdateUnitFromBattle(GbaPtr<Unit> unit, GbaPtr<BattleUnit> bu);

// Syncs HP and WEXP mid-battle from `bu` to `unit`.
// Matches FE7 US `UpdateUnitDuringBattle` (0x08029D0C).
void UpdateUnitDuringBattle(GbaPtr<Unit> unit, GbaPtr<BattleUnit> bu);

// Writes remaining ballista ammo back to the active ballista trap when
// `kBattleConfigBallista` is set. Matches FE7 US `BattleApplyBallistaUpdates`.
void BattleApplyBallistaUpdates();

// Clears the 3-byte post-battle scratch block at `0x0203A510`.
// Matches FE7 US `func_08029D6C`.
void ClearBattleExpScratch();

// Returns `unit->level` (+20 if promoted). Matches FE7 US `GetUnitExpLevel`.
int GetUnitExpLevel(GbaPtr<const Unit> unit);

// Computes base non-kill combat EXP. Matches FE7 US `GetUnitRoundExp`.
int GetUnitRoundExp(GbaPtr<const Unit> actor, GbaPtr<const Unit> target);

// Computes class-power-weighted level used by kill-EXP formulas.
// Matches FE7 US `func_08029DCC`.
int GetUnitPowerLevel(GbaPtr<const Unit> unit);

// Returns bonus kill EXP for Thief (+20) and Boss (+40) targets.
// Matches FE7 US `func_08029E08`.
int GetUnitClassKillExpBonus(GbaPtr<const Unit> actor,
                             GbaPtr<const Unit> target);

// Returns 2 if `actor` is an Assassin who triggered Silencer in this battle,
// else 1. Matches FE7 US `func_08029E30`.
int GetUnitExpMultiplier(GbaPtr<const Unit> actor, GbaPtr<const Unit> target);

// Computes kill EXP bonus when `target->hp == 0`.
// Matches FE7 US `GetUnitKillExpBonus` (0x08029E74).
int GetUnitKillExpBonus(GbaPtr<const Unit> actor, GbaPtr<const Unit> target);

// Computes total combat EXP gained by `actor` against `target` (clamped to
// `[0, 100]`, or 1 for 0-damage survival). Matches `GetBattleUnitExpGain`.
int GetBattleUnitExpGain(GbaPtr<BattleUnit> actor, GbaPtr<BattleUnit> target);

// Applies EXP for staff use and Dance/Play actions on `gBattleActor`.
// Matches FE7 US `func_08029F7C`.
void BattleApplyMiscActionExpGains();

// Computes staff-use EXP from `GetItemCostPerUse` (halved if promoted, capped
// at 100). Matches FE7 US `func_08029FF8`.
int GetBattleUnitStaffExp(GbaPtr<BattleUnit> bu);

// Grants +10 EXP to `gBattleActor` for Steal actions.
// Matches FE7 US `func_0802A05C`.
void BattleApplyStealExpGain();

// Registers all 26 post-battle EXP & update functions in the dispatch table.
void RegisterBattleExpFunctions();

}  // namespace fe7

#endif  // FE7_GAME_BATTLE_BATTLE_EXP_H_
