// FE7 US (AE7E) battle simulation, hit generation, and combat unwinding.
//
// Implements 22 battle simulation functions (ROM 0x0802857C..0x080286B8,
// 0x08028F84..0x080294D8, 0x0802A0A4..0x0802A1F4, and 0x0802AB90..0x0802ABA0):
// RN rollers (`BattleRoll1RN`, `BattleRoll2RN`), battle-unit initialization
// (`InitBattleUnit`, `InitBattleUnitWithoutBonuses`), target counter setup,
// triangle attack detection, round unwinding (`BattleUnwind`), and per-hit
// attribute/effect evaluation (`BattleGenerateHit*`).

#ifndef FE7_GAME_BATTLE_BATTLE_SIM_H_
#define FE7_GAME_BATTLE_BATTLE_SIM_H_

#include <cstdint>

#include "game/battle.h"
#include "game/unit.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// FE7 US ROM addresses for the 22 decompiled battle simulation functions.
inline constexpr uint32_t kAddrBattleRoll1RN = 0x0802857Cu;
inline constexpr uint32_t kAddrBattleRoll2RN = 0x080285A8u;
inline constexpr uint32_t kAddrInitBattleUnit = 0x080285D4u;
inline constexpr uint32_t kAddrInitBattleUnitWithoutBonuses = 0x080286B8u;
inline constexpr uint32_t kAddrClearBattleHits = 0x08028F84u;
inline constexpr uint32_t kAddrBattleUnwind = 0x08028FB0u;
inline constexpr uint32_t kAddrBattleGetBattleUnitOrder = 0x08029028u;
inline constexpr uint32_t kAddrBattleGetFollowUpOrder = 0x0802903Cu;
inline constexpr uint32_t kAddrBattleGenerateRoundHits = 0x080290B8u;
inline constexpr uint32_t kAddrBattleGetEffectiveHitCount = 0x08029114u;
inline constexpr uint32_t kAddrBattleCheckBrave = 0x08029128u;
inline constexpr uint32_t kAddrBattleCheckTriangleAttackOrder = 0x0802914Cu;
inline constexpr uint32_t kAddrBattleUpdateBattleStats = 0x08029234u;
inline constexpr uint32_t kAddrBattleGenerateHitAttributes = 0x08029264u;
inline constexpr uint32_t kAddrBattleGenerateHitTriangleAttack = 0x08029324u;
inline constexpr uint32_t kAddrBattleGenerateHitEffects = 0x0802939Cu;
inline constexpr uint32_t kAddrBattleGenerateHit = 0x080294D8u;
inline constexpr uint32_t kAddrBattleUnitTargetSetEquippedWeapon = 0x0802A0A4u;
inline constexpr uint32_t kAddrBattleUnitTargetCheckHasRealWeapon = 0x0802A0F8u;
inline constexpr uint32_t kAddrBattleInitTargetCanCounter = 0x0802A1F4u;
inline constexpr uint32_t kAddrBattleHitAdvance = 0x0802AB90u;
inline constexpr uint32_t kAddrBattleHitTerminate = 0x0802ABA0u;

// Evaluates a 1-RN roll in real combat or returns `sim_result` when
// `kBattleConfigSimulate` is set. Matches FE7 US `func_0802857C`.
int8_t BattleRoll1RN(uint16_t threshold, bool sim_result);

// Evaluates a 2-RN roll in real combat or returns `sim_result` when
// `kBattleConfigSimulate` is set. Matches FE7 US `func_080285A8`.
int8_t BattleRoll2RN(uint16_t threshold, bool sim_result);

// Copies `unit` into `bu` with item/status-adjusted stats and resets combat
// deltas on `bu`, `gBattleActor`, and `gBattleTarget`.
// Matches FE7 US `InitBattleUnit` (0x080285D4).
void InitBattleUnit(GbaPtr<BattleUnit> bu, GbaPtr<const Unit> unit);

// Same as `InitBattleUnit`, but restores raw unboosted unit stats and omits
// `bonus_con`. Matches FE7 US `InitBattleUnitWithoutBonuses` (0x080286B8).
void InitBattleUnitWithoutBonuses(GbaPtr<BattleUnit> bu,
                                  GbaPtr<const Unit> unit);

// Clears all 7 `gBattleHitArray` entries and resets `gBattleHitIt` to slot 0.
// Matches FE7 US `ClearBattleHits` (0x08028F84).
void ClearBattleHits();

// Unwinds a full combat exchange (initial attack, retaliation, and speed
// follow-up) into `gBattleHitArray`. Matches FE7 US `BattleUnwind`
// (0x08028FB0).
void BattleUnwind();

// Sets `*out_attacker = &gBattleActor` and `*out_defender = &gBattleTarget`.
// Matches FE7 US `func_08029028`.
void BattleGetBattleUnitOrder(GbaPtr<GbaPtr<BattleUnit>> out_attacker,
                              GbaPtr<GbaPtr<BattleUnit>> out_defender);

// Determines whether either combatant earns a speed follow-up (AS diff >= 4,
// non-Eclipse weapon) and writes the attacker/defender pointers.
// Matches FE7 US `func_0802903C`.
bool BattleGetFollowUpOrder(GbaPtr<GbaPtr<BattleUnit>> out_attacker,
                            GbaPtr<GbaPtr<BattleUnit>> out_defender);

// Generates all hits for one attack phase (1, or 2 for Brave weapons). Returns
// true if combat terminated early due to a KO. Matches FE7 US `func_080290B8`.
bool BattleGenerateRoundHits(GbaPtr<BattleUnit> attacker,
                             GbaPtr<BattleUnit> defender);

// Returns 2 if `attacker` has a Brave weapon, else 1 (setting
// `kBattleHitAttrBrave` on the current hit when Brave). Matches FE7 US
// `func_08029114`.
int BattleGetEffectiveHitCount(GbaPtr<BattleUnit> attacker);

// Returns true and sets `kBattleHitAttrBrave` if `attacker` wields a Brave
// weapon (`kItemAttrBrave`). Matches FE7 US `func_08029128`.
bool BattleCheckBrave(GbaPtr<BattleUnit> attacker);

// Scans the 4 tiles adjacent to `defender` for 2+ same-faction Triangle Attack
// allies, recording them in `gBattleStats.ta_unit_{a,b}`.
// Matches FE7 US `func_0802914C`.
bool BattleCheckTriangleAttackOrder(GbaPtr<BattleUnit> attacker,
                                    GbaPtr<BattleUnit> defender);

// Copies `attacker`'s attack/hit/crit/silencer and `defender`'s defense into
// `gBattleStats`. Matches FE7 US `func_08029234`.
void BattleUpdateBattleStats(GbaPtr<BattleUnit> attacker,
                             GbaPtr<BattleUnit> defender);

// Rolls hit, crit, and Silencer for the current hit, clamping damage to
// `[0, 127]`. Matches FE7 US `func_08029264`.
void BattleGenerateHitAttributes(GbaPtr<BattleUnit> attacker);

// Checks Triangle Attack eligibility and forces 100% hit/crit when active.
// Matches FE7 US `func_08029324`.
void BattleGenerateHitTriangleAttack(GbaPtr<BattleUnit> attacker,
                                     GbaPtr<BattleUnit> defender);

// Applies poison, Eclipse, Devil backlash, HP damage, Nosferatu HP drain, and
// weapon durability loss for the current hit.
// Matches FE7 US `BattleGenerateHitEffects` (0x0802939C).
void BattleGenerateHitEffects(GbaPtr<BattleUnit> attacker,
                              GbaPtr<BattleUnit> defender);

// Evaluates a single strike and advances `gBattleHitIt`. Returns true if either
// combatant reached 0 HP. Matches FE7 US `func_080294D8`.
bool BattleGenerateHit(GbaPtr<BattleUnit> attacker,
                       GbaPtr<BattleUnit> defender);

// Populates `bu->weapon_before` with the unit's equipped weapon (or first
// usable staff for magic users). Matches FE7 US `0x0802A0A4`.
void BattleUnitTargetSetEquippedWeapon(GbaPtr<BattleUnit> bu);

// Invalidates displayed combat stats to `0xFF` when `!bu->can_counter`.
// Matches FE7 US `BattleUnitTargetCheckHasRealWeapon` (0x0802A0F8).
void BattleUnitTargetCheckHasRealWeapon(GbaPtr<BattleUnit> bu);

// Clears `gBattleTarget`'s counter weapon when an uncounterable weapon or
// berserk player-vs-player engagement occurs. Matches FE7 US `0x0802A1F4`.
void BattleInitTargetCanCounter();

// Advances `gBattleHitIt` by one `BattleHit` entry. Matches FE7 US
// `0x0802AB90`.
void BattleHitAdvance();

// Advances `gBattleHitIt` and marks the new slot with `kBattleHitInfoEnd`.
// Matches FE7 US `0x0802ABA0`.
void BattleHitTerminate();

// Registers all 22 battle simulation functions in the dispatch table.
void RegisterBattleSimFunctions();

}  // namespace fe7

#endif  // FE7_GAME_BATTLE_BATTLE_SIM_H_
