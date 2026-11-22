// FE7 US (AE7E) combat stat computation and weapon triangle formulas.
//
// Implements 18 core battle-formula functions (ROM 0x080289B0..0x08028EEC and
// 0x0802A120..0x0802A170): defense, attack (FE7 US 2x effectiveness), attack
// speed, hit/avoid (with Tactician star bonuses), crit/dodge, effective
// hit/crit (Iron Rune), Assassin Silencer, S-rank +5/+5, Dance ring boosts,
// special weapons (Magic Swords, Eclipse, Luna), and Weapon Triangle + Reaver.

#ifndef FE7_GAME_BATTLE_BATTLE_H_
#define FE7_GAME_BATTLE_BATTLE_H_

#include <cstdint>

#include "game/battle.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// Fixed FE7 US ROM/EWRAM addresses used by battle formulas.
inline constexpr uint32_t kPlayStatusAddr = 0x0202BBF8u;
inline constexpr uint32_t kBmStatusAddr = 0x0202BBB8u;
inline constexpr uint32_t kTacticianAffinityTableAddr = 0x081C3AC0u;
inline constexpr uint32_t kWeaponTriangleTableAddr = 0x08B9426Cu;

// External helper ROM addresses called via GbaCall().
inline constexpr uint32_t kAddrHasTacticianAfaBonus = 0x08028194u;
inline constexpr uint32_t kAddrComputeBattleUnitSupportBonuses = 0x08028A24u;

// FE7 US ROM addresses for the 18 decompiled battle formula functions.
inline constexpr uint32_t kAddrComputeBattleUnitStats = 0x080289B0u;
inline constexpr uint32_t kAddrComputeBattleUnitEffectiveStats = 0x080289FCu;
inline constexpr uint32_t kAddrComputeBattleUnitDefense = 0x08028A98u;
inline constexpr uint32_t kAddrComputeBattleUnitBaseDefense = 0x08028AF8u;
inline constexpr uint32_t kAddrComputeBattleUnitAttack = 0x08028B10u;
inline constexpr uint32_t kAddrComputeBattleUnitSpeed = 0x08028B68u;
inline constexpr uint32_t kAddrComputeBattleUnitHitRate = 0x08028BA0u;
inline constexpr uint32_t kAddrComputeBattleUnitAvoidRate = 0x08028C50u;
inline constexpr uint32_t kAddrComputeBattleUnitCritRate = 0x08028D00u;
inline constexpr uint32_t kAddrComputeBattleUnitDodgeRate = 0x08028D3Cu;
inline constexpr uint32_t kAddrComputeBattleUnitEffectiveHitRate = 0x08028D48u;
inline constexpr uint32_t kAddrComputeBattleUnitEffectiveCritRate = 0x08028D74u;
inline constexpr uint32_t kAddrComputeBattleUnitSilencerRate = 0x08028E1Cu;
inline constexpr uint32_t kAddrComputeBattleUnitWeaponRankBonuses = 0x08028E78u;
inline constexpr uint32_t kAddrComputeBattleUnitStatusBonuses = 0x08028EB4u;
inline constexpr uint32_t kAddrComputeBattleUnitSpecialWeaponStats =
    0x08028EECu;
inline constexpr uint32_t kAddrBattleApplyReaverEffect = 0x0802A120u;
inline constexpr uint32_t kAddrBattleApplyWeaponTriangleEffect = 0x0802A170u;

// Computes all primary battle stats for `attacker` facing `defender`.
void ComputeBattleUnitStats(GbaPtr<BattleUnit> attacker,
                            GbaPtr<BattleUnit> defender);

// Computes effective hit, effective crit, silencer, and special weapon stats.
void ComputeBattleUnitEffectiveStats(GbaPtr<BattleUnit> attacker,
                                     GbaPtr<BattleUnit> defender);

// Individual battle stat computation functions.
void ComputeBattleUnitDefense(GbaPtr<BattleUnit> attacker,
                              GbaPtr<const BattleUnit> defender);
void ComputeBattleUnitBaseDefense(GbaPtr<BattleUnit> bu);
void ComputeBattleUnitAttack(GbaPtr<BattleUnit> attacker,
                             GbaPtr<const BattleUnit> defender);
void ComputeBattleUnitSpeed(GbaPtr<BattleUnit> bu);
void ComputeBattleUnitHitRate(GbaPtr<BattleUnit> bu);
void ComputeBattleUnitAvoidRate(GbaPtr<BattleUnit> bu);
void ComputeBattleUnitCritRate(GbaPtr<BattleUnit> bu);
void ComputeBattleUnitDodgeRate(GbaPtr<BattleUnit> bu);
void ComputeBattleUnitEffectiveHitRate(GbaPtr<BattleUnit> attacker,
                                       GbaPtr<const BattleUnit> defender);
void ComputeBattleUnitEffectiveCritRate(GbaPtr<BattleUnit> attacker,
                                        GbaPtr<const BattleUnit> defender);
void ComputeBattleUnitSilencerRate(GbaPtr<BattleUnit> attacker,
                                   GbaPtr<const BattleUnit> defender);
void ComputeBattleUnitWeaponRankBonuses(GbaPtr<BattleUnit> bu);
void ComputeBattleUnitStatusBonuses(GbaPtr<BattleUnit> bu);
void ComputeBattleUnitSpecialWeaponStats(GbaPtr<BattleUnit> attacker,
                                         GbaPtr<BattleUnit> defender);

// Reverses and doubles weapon triangle bonuses unless both units hold Reavers.
void BattleApplyReaverEffect(GbaPtr<BattleUnit> attacker,
                             GbaPtr<BattleUnit> defender);

// Applies weapon triangle hit/dmg modifiers (and Reaver reversal) to both
// combatants.
void BattleApplyWeaponTriangleEffect(GbaPtr<BattleUnit> attacker,
                                     GbaPtr<BattleUnit> defender);

// Registers all 18 native battle formula functions in the dispatch table.
void RegisterBattleFunctions();

}  // namespace fe7

#endif  // FE7_GAME_BATTLE_BATTLE_H_
