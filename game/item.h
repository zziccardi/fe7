// Canonical FE7 US Item (IInfo) and ItemStatBonuses struct layouts.
//
// Field names, sizes, and offsets match FE7 US (AE7E) ROM item tables
// (`gItemData` at 0x08BE222C, 0x24-byte entries) so native C++20 code and the
// fallback ARM7TDMI CPU share identical 32-bit layouts via GbaPtr<T>.

#ifndef FE7_GAME_ITEM_H_
#define FE7_GAME_ITEM_H_

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "runtime/gba_ptr.h"

namespace fe7 {

// Item attribute bitmask flags stored in IInfo::attributes (offset 0x08).
inline constexpr uint32_t kItemAttrWeapon = 1u << 0;
inline constexpr uint32_t kItemAttrMagic = 1u << 1;
inline constexpr uint32_t kItemAttrStaff = 1u << 2;
inline constexpr uint32_t kItemAttrUnbreakable = 1u << 3;
inline constexpr uint32_t kItemAttrUnsellable = 1u << 4;
inline constexpr uint32_t kItemAttrBrave = 1u << 5;
inline constexpr uint32_t kItemAttrMagicDamage = 1u << 6;
inline constexpr uint32_t kItemAttrUncounterable = 1u << 7;
inline constexpr uint32_t kItemAttrReverseTriangle = 1u << 8;
inline constexpr uint32_t kItemAttrHammerne = 1u << 9;
inline constexpr uint32_t kItemAttrLockMonster = 1u << 10;
inline constexpr uint32_t kItemAttrLockRoy = 1u << 11;
inline constexpr uint32_t kItemAttrLockMyrm = 1u << 12;
inline constexpr uint32_t kItemAttrLockKing = 1u << 13;
inline constexpr uint32_t kItemAttrNegateFlying = 1u << 14;
inline constexpr uint32_t kItemAttrNegateCrit = 1u << 15;
inline constexpr uint32_t kItemAttrUnusable = 1u << 16;
inline constexpr uint32_t kItemAttrNegateDefense = 1u << 17;
inline constexpr uint32_t kItemAttrLockEliwood = 1u << 18;
inline constexpr uint32_t kItemAttrLockHector = 1u << 19;
inline constexpr uint32_t kItemAttrLockLyn = 1u << 20;
inline constexpr uint32_t kItemAttrLockAthos = 1u << 21;

// Combined weapon-lock mask checked by CanUnitUseWeapon (0x003D3C00).
inline constexpr uint32_t kItemAttrLockAny =
    kItemAttrLockMonster | kItemAttrLockRoy | kItemAttrLockMyrm |
    kItemAttrLockKing | kItemAttrUnusable | kItemAttrLockEliwood |
    kItemAttrLockHector | kItemAttrLockLyn | kItemAttrLockAthos;

// Item category IDs stored in IInfo::kind (offset 0x07).
enum class ItemKind : uint8_t {
  kSword = 0,
  kLance = 1,
  kAxe = 2,
  kBow = 3,
  kStaff = 4,
  kAnima = 5,
  kLight = 6,
  kDark = 7,
  kBallista = 8,
  kItem = 9,
  kDragon = 10,
  kMonster = 11,
  kDance = 12,
};

// Weapon rank levels returned by GetWeaponLevelFromExp().
enum class WeaponRank : int {
  kNone = 0,
  kE = 1,
  kD = 2,
  kC = 3,
  kB = 4,
  kA = 5,
  kS = 6,
};

// Static ROM stat bonus block pointed to by IInfo::bonuses (9 bytes).
struct ItemStatBonuses {
  int8_t hp;   // 0x00
  int8_t pow;  // 0x01
  int8_t skl;  // 0x02
  int8_t spd;  // 0x03
  int8_t def;  // 0x04
  int8_t res;  // 0x05
  int8_t lck;  // 0x06
  int8_t mov;  // 0x07
  int8_t con;  // 0x08
};

static_assert(sizeof(ItemStatBonuses) == 9, "ItemStatBonuses must be 9 bytes");
static_assert(std::is_trivially_copyable_v<ItemStatBonuses>);

// Static ROM item template (`IInfo` / `ItemData`, 0x24 bytes).
struct IInfo {
  uint16_t msg_name;                      // 0x00
  uint16_t msg_desc;                      // 0x02
  uint16_t msg_use_desc;                  // 0x04
  uint8_t id;                             // 0x06
  uint8_t kind;                           // 0x07: ItemKind
  uint32_t attributes;                    // 0x08: kItemAttr* bitmask
  GbaPtr<const ItemStatBonuses> bonuses;  // 0x0C
  GbaPtr<const uint8_t> effectiveness;    // 0x10: 0-terminated JID list
  uint8_t max_uses;                       // 0x14
  uint8_t might;                          // 0x15
  uint8_t hit;                            // 0x16
  uint8_t weight;                         // 0x17
  uint8_t crit;                           // 0x18
  uint8_t encoded_range;                  // 0x19: (min << 4) | max
  uint16_t cost_per_use;                  // 0x1A
  uint8_t required_wexp;                  // 0x1C
  uint8_t icon_id;                        // 0x1D
  uint8_t use_effect;                     // 0x1E
  uint8_t weapon_effect;                  // 0x1F
  uint8_t wexp_gain;                      // 0x20
  uint8_t pad_21[3];                      // 0x21
};

static_assert(sizeof(IInfo) == 0x24, "IInfo must be 0x24 bytes");
static_assert(offsetof(IInfo, attributes) == 0x08);
static_assert(offsetof(IInfo, bonuses) == 0x0C);
static_assert(offsetof(IInfo, effectiveness) == 0x10);
static_assert(offsetof(IInfo, max_uses) == 0x14);
static_assert(offsetof(IInfo, encoded_range) == 0x19);
static_assert(offsetof(IInfo, cost_per_use) == 0x1A);
static_assert(offsetof(IInfo, required_wexp) == 0x1C);
static_assert(offsetof(IInfo, wexp_gain) == 0x20);
static_assert(std::is_trivially_copyable_v<IInfo>);

}  // namespace fe7

#endif  // FE7_GAME_ITEM_H_
