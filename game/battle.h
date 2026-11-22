// Canonical FE7 US BattleUnit, BattleStats, and WeaponTriangleRule layouts.
//
// Field names, sizes, and offsets match FE7 US (AE7E) EWRAM (`gBattleActor` at
// 0x0203A3F0, `gBattleTarget` at 0x0203A470, `gBattleStats` at 0x0203A3D8) so
// native C++20 combat formulas and the fallback ARM7TDMI CPU share identical
// 32-bit layouts.

#ifndef FE7_GAME_BATTLE_H_
#define FE7_GAME_BATTLE_H_

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "game/unit.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// Fixed EWRAM addresses for FE7 US active battle state.
inline constexpr uint32_t kBattleStatsAddr = 0x0203A3D8u;
inline constexpr uint32_t kBattleActorAddr = 0x0203A3F0u;
inline constexpr uint32_t kBattleTargetAddr = 0x0203A470u;
inline constexpr uint32_t kBattleHitsAddr = 0x0203A4F0u;
inline constexpr uint32_t kBattleHitItAddr = 0x0203A50Cu;
inline constexpr int kBattleHitMaxCount = 7;

// Battle configuration flags stored in BattleStats::config.
inline constexpr uint16_t kBattleConfigReal = 1u << 0;
inline constexpr uint16_t kBattleConfigSimulate = 1u << 1;
inline constexpr uint16_t kBattleConfigBit2 = 1u << 2;
inline constexpr uint16_t kBattleConfigBallista = 1u << 3;
inline constexpr uint16_t kBattleConfigPromotion = 1u << 4;
inline constexpr uint16_t kBattleConfigArena = 1u << 5;
inline constexpr uint16_t kBattleConfigRefresh = 1u << 6;
inline constexpr uint16_t kBattleConfigMapAnims = 1u << 7;
inline constexpr uint16_t kBattleConfigPromotionPrep = 1u << 8;
inline constexpr uint16_t kBattleConfigDanceRing = 1u << 9;

// Hit attribute bitmask stored in BattleHit::attributes.
inline constexpr uint16_t kBattleHitAttrCrit = 1u << 0;
inline constexpr uint16_t kBattleHitAttrMiss = 1u << 1;
inline constexpr uint16_t kBattleHitAttrFollowUp = 1u << 2;
inline constexpr uint16_t kBattleHitAttrRetaliation = 1u << 3;
inline constexpr uint16_t kBattleHitAttrBrave = 1u << 4;
inline constexpr uint16_t kBattleHitAttrPoison = 1u << 6;
inline constexpr uint16_t kBattleHitAttrDevil = 1u << 7;
inline constexpr uint16_t kBattleHitAttrHpSteal = 1u << 8;
inline constexpr uint16_t kBattleHitAttrHalfHp = 1u << 9;
inline constexpr uint16_t kBattleHitAttrTAttack = 1u << 10;
inline constexpr uint16_t kBattleHitAttrSilencer = 1u << 11;

// Hit info bitmask stored in BattleHit::info.
inline constexpr uint8_t kBattleHitInfoBegin = 1u << 0;
inline constexpr uint8_t kBattleHitInfoKills = 1u << 1;
inline constexpr uint8_t kBattleHitInfoKillsTarget = 1u << 2;
inline constexpr uint8_t kBattleHitInfoRetaliation = 1u << 3;
inline constexpr uint8_t kBattleHitInfoEnd = 1u << 7;

// Single combat round entry in `gBattleHitArray` (0x0203A4F0, 7 x 4 bytes).
struct BattleHit {
  uint16_t attributes;  // 0x00: kBattleHitAttr* bitmask
  uint8_t info;         // 0x02: kBattleHitInfo* bitmask
  int8_t hp_change;     // 0x03: Damage dealt (or HP changed)
};

static_assert(sizeof(BattleHit) == 4);
static_assert(std::is_trivially_copyable_v<BattleHit>);

// Single entry in FE7's ROM weapon triangle table (`0x08B9426C`, 4 bytes).
struct WeaponTriangleRule {
  int8_t attacker_kind;  // 0x00 (< 0 terminates table)
  int8_t defender_kind;  // 0x01
  int8_t hit_bonus;      // 0x02
  int8_t dmg_bonus;      // 0x03
};

static_assert(sizeof(WeaponTriangleRule) == 4);
static_assert(std::is_trivially_copyable_v<WeaponTriangleRule>);

// Combatant snapshot + computed battle stats (0x80 bytes, EWRAM).
struct BattleUnit {
  Unit unit;                           // 0x00..0x47
  uint16_t weapon;                     // 0x48: Current equipped item+uses
  uint16_t weapon_before;              // 0x4A: Pre-battle item+uses
  uint32_t weapon_attributes;          // 0x4C: Cached IInfo::attributes
  uint8_t weapon_type;                 // 0x50: ItemKind
  uint8_t weapon_slot_index;           // 0x51: Inventory slot index
  int8_t can_counter;                  // 0x52
  int8_t wtriangle_hit_bonus;          // 0x53
  int8_t wtriangle_dmg_bonus;          // 0x54
  uint8_t terrain_id;                  // 0x55
  int8_t terrain_defense;              // 0x56
  int8_t terrain_avoid;                // 0x57
  int8_t terrain_resistance;           // 0x58
  uint8_t pad_59;                      // 0x59
  int16_t battle_attack;               // 0x5A
  int16_t battle_defense;              // 0x5C
  int16_t battle_speed;                // 0x5E
  int16_t battle_hit_rate;             // 0x60
  int16_t battle_avoid_rate;           // 0x62
  int16_t battle_effective_hit_rate;   // 0x64
  int16_t battle_crit_rate;            // 0x66
  int16_t battle_dodge_rate;           // 0x68
  int16_t battle_effective_crit_rate;  // 0x6A
  int16_t battle_silencer_rate;        // 0x6C
  int8_t exp_gain;                     // 0x6E
  int8_t status_out;                   // 0x6F
  int8_t level_previous;               // 0x70
  int8_t exp_previous;                 // 0x71
  int8_t hp_initial;                   // 0x72
  int8_t change_hp;                    // 0x73
  int8_t change_pow;                   // 0x74
  int8_t change_skl;                   // 0x75
  int8_t change_spd;                   // 0x76
  int8_t change_def;                   // 0x77
  int8_t change_res;                   // 0x78
  int8_t change_lck;                   // 0x79
  int8_t change_con;                   // 0x7A
  int8_t wexp_multiplier;              // 0x7B
  int8_t non_zero_damage;              // 0x7C
  int8_t weapon_broke;                 // 0x7D
  int8_t has_item_effect_target;       // 0x7E
  uint8_t pad_7f;                      // 0x7F
};

static_assert(sizeof(BattleUnit) == 0x80, "BattleUnit must be 0x80 bytes");
static_assert(offsetof(BattleUnit, weapon) == 0x48);
static_assert(offsetof(BattleUnit, weapon_attributes) == 0x4C);
static_assert(offsetof(BattleUnit, wtriangle_hit_bonus) == 0x53);
static_assert(offsetof(BattleUnit, terrain_defense) == 0x56);
static_assert(offsetof(BattleUnit, battle_attack) == 0x5A);
static_assert(offsetof(BattleUnit, battle_speed) == 0x5E);
static_assert(offsetof(BattleUnit, battle_hit_rate) == 0x60);
static_assert(offsetof(BattleUnit, battle_effective_hit_rate) == 0x64);
static_assert(offsetof(BattleUnit, battle_crit_rate) == 0x66);
static_assert(offsetof(BattleUnit, battle_silencer_rate) == 0x6C);
static_assert(std::is_trivially_copyable_v<BattleUnit>);

// Global combat parameters (`gBattleStats`, 0x18 bytes).
struct BattleStats {
  uint16_t config;         // 0x00: kBattleConfig* bitmask
  uint8_t range;           // 0x02: Tile distance between combatants
  uint8_t pad_03;          // 0x03
  int16_t damage;          // 0x04
  int16_t attack;          // 0x06
  int16_t defense;         // 0x08
  int16_t hit_rate;        // 0x0A
  int16_t crit_rate;       // 0x0C
  int16_t silencer_rate;   // 0x0E
  GbaPtr<Unit> ta_unit_a;  // 0x10: Triangle attack partner A
  GbaPtr<Unit> ta_unit_b;  // 0x14: Triangle attack partner B
};

static_assert(sizeof(BattleStats) == 0x18, "BattleStats must be 0x18 bytes");
static_assert(offsetof(BattleStats, damage) == 0x04);
static_assert(offsetof(BattleStats, ta_unit_a) == 0x10);
static_assert(std::is_trivially_copyable_v<BattleStats>);

}  // namespace fe7

#endif  // FE7_GAME_BATTLE_H_
