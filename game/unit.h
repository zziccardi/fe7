// Canonical FE7 US Unit, Character (PInfo), Class (JInfo), and UnitInfo
// struct layouts.
//
// Field names, sizes, and offsets match FE7 US (AE7E) EWRAM and ROM tables so
// native C++20 code and the fallback ARM7TDMI CPU share identical 32-bit
// layouts via GbaPtr<T>.

#ifndef FE7_GAME_UNIT_H_
#define FE7_GAME_UNIT_H_

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "runtime/gba_ptr.h"

namespace fe7 {

// Unit ID stride per faction and per-faction EWRAM pool capacities.
inline constexpr int kUnitsPerFaction = 0x40;
inline constexpr int kUnitCountBlue = 62;
inline constexpr int kUnitCountRed = 50;
inline constexpr int kUnitCountGreen = 20;
inline constexpr int kUnitCountPurple = 5;

// Fixed array sizes inside Unit / PInfo / JInfo.
inline constexpr int kUnitItemCount = 5;
inline constexpr int kUnitWexpCount = 8;
inline constexpr int kUnitSupportCount = 7;

// Faction base IDs (masked from Unit::id with 0xC0).
inline constexpr uint8_t kUnitFactionBlue = 0x00;    // Player units
inline constexpr uint8_t kUnitFactionGreen = 0x40;   // Ally NPC units
inline constexpr uint8_t kUnitFactionRed = 0x80;     // Enemy units
inline constexpr uint8_t kUnitFactionPurple = 0xC0;  // Link arena 4th team

// Character / class attribute bitmask flags (OR'd across PInfo and JInfo).
inline constexpr uint32_t kUnitAttrMounted = 1u << 0;
inline constexpr uint32_t kUnitAttrCanto = 1u << 1;
inline constexpr uint32_t kUnitAttrSteal = 1u << 2;
inline constexpr uint32_t kUnitAttrThief = 1u << 3;
inline constexpr uint32_t kUnitAttrDance = 1u << 4;
inline constexpr uint32_t kUnitAttrPlay = 1u << 5;
inline constexpr uint32_t kUnitAttrCritBonus = 1u << 6;
inline constexpr uint32_t kUnitAttrBallista = 1u << 7;
inline constexpr uint32_t kUnitAttrPromoted = 1u << 8;
inline constexpr uint32_t kUnitAttrSupply = 1u << 9;
inline constexpr uint32_t kUnitAttrHorse = 1u << 10;
inline constexpr uint32_t kUnitAttrWyvern = 1u << 11;
inline constexpr uint32_t kUnitAttrPegasus = 1u << 12;
inline constexpr uint32_t kUnitAttrLord = 1u << 13;
inline constexpr uint32_t kUnitAttrFemale = 1u << 14;
inline constexpr uint32_t kUnitAttrBoss = 1u << 15;
inline constexpr uint32_t kUnitAttrLockRoy = 1u << 16;
inline constexpr uint32_t kUnitAttrLockMyrm = 1u << 17;
inline constexpr uint32_t kUnitAttrMonster = 1u << 18;
inline constexpr uint32_t kUnitAttrMorph = 1u << 19;
inline constexpr uint32_t kUnitAttrUnselectable = 1u << 20;
inline constexpr uint32_t kUnitAttrTriangleAttackPegasi = 1u << 21;
inline constexpr uint32_t kUnitAttrTriangleAttackArmors = 1u << 22;
inline constexpr uint32_t kUnitAttrAltPinfo = 1u << 23;
inline constexpr uint32_t kUnitAttrFinalBoss = 1u << 24;
inline constexpr uint32_t kUnitAttrAssassin = 1u << 25;
inline constexpr uint32_t kUnitAttrMagicSeal = 1u << 26;
inline constexpr uint32_t kUnitAttrDropsItem = 1u << 27;
inline constexpr uint32_t kUnitAttrLockEliwood = 1u << 28;
inline constexpr uint32_t kUnitAttrLockHector = 1u << 29;
inline constexpr uint32_t kUnitAttrLockLyn = 1u << 30;
inline constexpr uint32_t kUnitAttrLockAthos = 1u << 31;

inline constexpr uint32_t kUnitAttrsRefresh = kUnitAttrDance | kUnitAttrPlay;
inline constexpr uint32_t kUnitAttrsFlying = kUnitAttrWyvern | kUnitAttrPegasus;

// Dynamic per-unit state bitmask flags stored in Unit::flags.
inline constexpr uint32_t kUnitFlagHidden = 1u << 0;
inline constexpr uint32_t kUnitFlagTurnEnded = 1u << 1;
inline constexpr uint32_t kUnitFlagDead = 1u << 2;
inline constexpr uint32_t kUnitFlagNotDeployed = 1u << 3;
inline constexpr uint32_t kUnitFlagRescuing = 1u << 4;
inline constexpr uint32_t kUnitFlagRescued = 1u << 5;
inline constexpr uint32_t kUnitFlagHasMoved = 1u << 6;
inline constexpr uint32_t kUnitFlagUnderARoof = 1u << 7;
inline constexpr uint32_t kUnitFlagFogSeen = 1u << 8;
inline constexpr uint32_t kUnitFlagFogHidden = 1u << 9;
inline constexpr uint32_t kUnitFlagHasMovedAi = 1u << 10;
inline constexpr uint32_t kUnitFlagInBallista = 1u << 11;
inline constexpr uint32_t kUnitFlagBit12 = 1u << 12;
inline constexpr uint32_t kUnitFlagGrowthBoost = 1u << 13;
inline constexpr uint32_t kUnitFlagSoloAnim1 = 1u << 14;
inline constexpr uint32_t kUnitFlagSoloAnim2 = 1u << 15;
inline constexpr uint32_t kUnitFlagAway = 1u << 16;
inline constexpr uint32_t kUnitFlagArenaA = 1u << 17;
inline constexpr uint32_t kUnitFlagArenaB = 1u << 18;
inline constexpr uint32_t kUnitFlagArenaC = 1u << 19;

inline constexpr uint32_t kUnitFlagsUnavailable =
    kUnitFlagDead | kUnitFlagNotDeployed | kUnitFlagAway;

// Status condition IDs stored in the 4-bit Unit::status field.
enum class UnitStatus : uint8_t {
  kNone = 0,
  kPoison = 1,
  kSleep = 2,
  kSilenced = 3,
  kBerserk = 4,
  kAttack = 5,
  kDefense = 6,
  kCrit = 7,
  kAvoid = 8,
};

// Forward declarations for companion structs decompiled in later waves.
struct SupportInfo;
struct UnitSprite;

// Static ROM character template ("CharacterData" / PInfo, 0x34 bytes).
struct PInfo {
  uint16_t msg_name;                        // 0x00
  uint16_t msg_desc;                        // 0x02
  uint8_t id;                               // 0x04
  uint8_t jid_default;                      // 0x05
  uint16_t fid;                             // 0x06
  uint8_t chibi_id;                         // 0x08
  uint8_t affinity;                         // 0x09
  uint8_t sort_key;                         // 0x0A
  int8_t base_level;                        // 0x0B
  int8_t base_hp;                           // 0x0C
  int8_t base_pow;                          // 0x0D
  int8_t base_skl;                          // 0x0E
  int8_t base_spd;                          // 0x0F
  int8_t base_def;                          // 0x10
  int8_t base_res;                          // 0x11
  int8_t base_lck;                          // 0x12
  int8_t bonus_con;                         // 0x13
  uint8_t wexp[kUnitWexpCount];             // 0x14
  uint8_t growth_hp;                        // 0x1C
  uint8_t growth_pow;                       // 0x1D
  uint8_t growth_skl;                       // 0x1E
  uint8_t growth_spd;                       // 0x1F
  uint8_t growth_def;                       // 0x20
  uint8_t growth_res;                       // 0x21
  uint8_t growth_lck;                       // 0x22
  uint8_t banim_pal_a;                      // 0x23
  uint8_t banim_pal_b;                      // 0x24
  uint8_t banim_info_override[2];           // 0x25
  uint8_t pad_27;                           // 0x27
  uint32_t attributes;                      // 0x28
  GbaPtr<const SupportInfo> support_info;   // 0x2C
  uint8_t generic_talk_tone;                // 0x30
  uint8_t pad_31[3];                        // 0x31
};

static_assert(sizeof(PInfo) == 0x34, "PInfo must be 0x34 bytes");
static_assert(offsetof(PInfo, wexp) == 0x14);
static_assert(offsetof(PInfo, attributes) == 0x28);
static_assert(offsetof(PInfo, support_info) == 0x2C);
static_assert(offsetof(PInfo, generic_talk_tone) == 0x30);
static_assert(std::is_trivially_copyable_v<PInfo>);

// Static ROM class template ("ClassData" / JInfo, 0x54 bytes).
struct JInfo {
  uint16_t msg_name;                       // 0x00
  uint16_t msg_desc;                       // 0x02
  uint8_t id;                              // 0x04
  uint8_t jid_promote;                     // 0x05
  uint8_t map_sprite;                      // 0x06
  uint8_t walk_speed;                      // 0x07
  uint16_t fid;                            // 0x08
  uint8_t sort_key;                        // 0x0A
  int8_t base_hp;                          // 0x0B
  int8_t base_pow;                         // 0x0C
  int8_t base_skl;                         // 0x0D
  int8_t base_spd;                         // 0x0E
  int8_t base_def;                         // 0x0F
  int8_t base_res;                         // 0x10
  int8_t base_con;                         // 0x11
  int8_t base_mov;                         // 0x12
  int8_t max_hp;                           // 0x13
  int8_t max_pow;                          // 0x14
  int8_t max_skl;                          // 0x15
  int8_t max_spd;                          // 0x16
  int8_t max_def;                          // 0x17
  int8_t max_res;                          // 0x18
  int8_t max_con;                          // 0x19
  int8_t power_level;                      // 0x1A
  int8_t growth_hp;                        // 0x1B
  int8_t growth_pow;                       // 0x1C
  int8_t growth_skl;                       // 0x1D
  int8_t growth_spd;                       // 0x1E
  int8_t growth_def;                       // 0x1F
  int8_t growth_res;                       // 0x20
  int8_t growth_lck;                       // 0x21
  uint8_t promotion_hp;                    // 0x22
  uint8_t promotion_pow;                   // 0x23
  uint8_t promotion_skl;                   // 0x24
  uint8_t promotion_spd;                   // 0x25
  uint8_t promotion_def;                   // 0x26
  uint8_t promotion_res;                   // 0x27
  uint32_t attributes;                     // 0x28
  uint8_t wexp[kUnitWexpCount];            // 0x2C
  GbaPtr<const void> banim_info;           // 0x34
  GbaPtr<const int8_t> mov_table[3];       // 0x38
  GbaPtr<const int8_t> avo_terrain_table;  // 0x44
  GbaPtr<const int8_t> def_terrain_table;  // 0x48
  GbaPtr<const int8_t> res_terrain_table;  // 0x4C
  GbaPtr<const void> unk_50;               // 0x50
};

static_assert(sizeof(JInfo) == 0x54, "JInfo must be 0x54 bytes");
static_assert(offsetof(JInfo, base_mov) == 0x12);
static_assert(offsetof(JInfo, attributes) == 0x28);
static_assert(offsetof(JInfo, banim_info) == 0x34);
static_assert(offsetof(JInfo, mov_table) == 0x38);
static_assert(offsetof(JInfo, unk_50) == 0x50);
static_assert(std::is_trivially_copyable_v<JInfo>);

// Active EWRAM unit instance (0x48 bytes).
struct Unit {
  GbaPtr<const PInfo> pinfo;           // 0x00: Character data in ROM
  GbaPtr<const JInfo> jinfo;           // 0x04: Class data in ROM
  int8_t level;                        // 0x08
  uint8_t exp;                         // 0x09
  uint8_t ai_flags;                    // 0x0A
  int8_t id;                           // 0x0B: Faction + 1-based slot index
  uint32_t flags;                      // 0x0C: kUnitFlag* bitmask
  int8_t x;                            // 0x10
  int8_t y;                            // 0x11
  int8_t max_hp;                       // 0x12
  int8_t hp;                           // 0x13
  int8_t pow;                          // 0x14
  int8_t skl;                          // 0x15
  int8_t spd;                          // 0x16
  int8_t def;                          // 0x17
  int8_t res;                          // 0x18
  int8_t lck;                          // 0x19
  int8_t bonus_con;                    // 0x1A
  uint8_t rescue;                      // 0x1B: Rescued/rescuing unit ID
  uint8_t ballista_trap;               // 0x1C
  int8_t bonus_mov;                    // 0x1D
  uint16_t items[kUnitItemCount];      // 0x1E
  uint8_t wexp[kUnitWexpCount];        // 0x28
  uint8_t status : 4;                  // 0x30 (low nibble)
  uint8_t status_duration : 4;         // 0x30 (high nibble)
  uint8_t torch : 4;                   // 0x31 (low nibble)
  uint8_t barrier : 4;                 // 0x31 (high nibble)
  uint8_t supports[kUnitSupportCount]; // 0x32
  uint8_t support_bits;                // 0x39
  uint8_t pad_3a[2];                   // 0x3A
  GbaPtr<UnitSprite> map_sprite;       // 0x3C
  uint16_t ai_config;                  // 0x40
  uint8_t ai_a;                        // 0x42
  uint8_t ai_a_pc;                     // 0x43
  uint8_t ai_b;                        // 0x44
  uint8_t ai_b_pc;                     // 0x45
  uint8_t unk_46;                      // 0x46
  uint8_t pad_47;                      // 0x47
};

static_assert(sizeof(Unit) == 0x48, "Unit must be 0x48 bytes");
static_assert(offsetof(Unit, level) == 0x08);
static_assert(offsetof(Unit, flags) == 0x0C);
static_assert(offsetof(Unit, x) == 0x10);
static_assert(offsetof(Unit, items) == 0x1E);
static_assert(offsetof(Unit, wexp) == 0x28);
static_assert(offsetof(Unit, supports) == 0x32);
static_assert(offsetof(Unit, map_sprite) == 0x3C);
static_assert(offsetof(Unit, ai_config) == 0x40);
static_assert(std::is_trivially_copyable_v<Unit>);

// Static ROM unit spawn entry used by chapter events (0x10 bytes).
struct UnitInfo {
  uint8_t pid;            // 0x00: Character ID
  uint8_t jid;            // 0x01: Class ID
  uint8_t pid_lead;       // 0x02: Leader character ID
  uint8_t autolevel : 1;  // 0x03 (bit 0)
  uint8_t faction_id : 2; // 0x03 (bits 1..2)
  uint8_t level : 5;      // 0x03 (bits 3..7)
  uint8_t x_load;         // 0x04
  uint8_t y_load;         // 0x05
  uint8_t x_move;         // 0x06
  uint8_t y_move;         // 0x07
  uint8_t item[4];        // 0x08
  uint8_t ai[4];          // 0x0C
};

static_assert(sizeof(UnitInfo) == 0x10, "UnitInfo must be 0x10 bytes");
static_assert(offsetof(UnitInfo, item) == 0x08);
static_assert(offsetof(UnitInfo, ai) == 0x0C);
static_assert(std::is_trivially_copyable_v<UnitInfo>);

// Returns the combined character + class attribute bitmask for `unit`.
inline uint32_t UnitAttributes(const Unit& unit) {
  return unit.pinfo->attributes | unit.jinfo->attributes;
}

}  // namespace fe7

#endif  // FE7_GAME_UNIT_H_
