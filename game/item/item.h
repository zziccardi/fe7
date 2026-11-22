// FE7 US (AE7E) item properties, weapon stat bonuses, and equip/usage checks.
//
// Implements 42 core item & weapon functions (ROM 0x08016040..0x080174AC)
// backed directly by FE7 US's ROM item table (`0x08BE222C`). Registering these
// upgrades Wave 2 unit stat getters (`GetUnitMaxHp`..`GetUnitLuck`) to pure
// native-to-native calls for standard weapons.

#ifndef FE7_GAME_ITEM_ITEM_H_
#define FE7_GAME_ITEM_ITEM_H_

#include <cstdint>

#include "game/item.h"
#include "game/unit.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// Fixed FE7 US ROM table addresses used by the item subsystem.
inline constexpr uint32_t kIInfoTableAddr = 0x08BE222Cu;
inline constexpr uint32_t kFlyingEffectivenessAddr = 0x08C97ED2u;

// External helper ROM addresses called via GbaCall() when needed.
inline constexpr uint32_t kAddrCheckUnusableWeaponLock = 0x08017178u;
inline constexpr uint32_t kAddrIsUnitInMagicSealRange = 0x0801878Cu;

// FE7 US ROM addresses for the 42 decompiled item functions.
inline constexpr uint32_t kAddrItemHpBonus = 0x08016040u;
inline constexpr uint32_t kAddrItemPowBonus = 0x0801606Cu;
inline constexpr uint32_t kAddrItemSklBonus = 0x08016098u;
inline constexpr uint32_t kAddrItemSpdBonus = 0x080160C4u;
inline constexpr uint32_t kAddrItemDefBonus = 0x080160F0u;
inline constexpr uint32_t kAddrItemResBonus = 0x0801611Cu;
inline constexpr uint32_t kAddrItemLckBonus = 0x08016148u;
inline constexpr uint32_t kAddrCreateItem = 0x08016174u;
inline constexpr uint32_t kAddrCanUnitUseWeapon = 0x080161A4u;
inline constexpr uint32_t kAddrCanUnitUseWeaponNow = 0x08016380u;
inline constexpr uint32_t kAddrCanUnitUseStaff = 0x080163D4u;
inline constexpr uint32_t kAddrCanUnitUseStaffNow = 0x08016428u;
inline constexpr uint32_t kAddrGetItemAfterUse = 0x08016730u;
inline constexpr uint32_t kAddrUnitEquippedWeapon = 0x08016764u;
inline constexpr uint32_t kAddrUnitEquippedWeaponSlot = 0x08016794u;
inline constexpr uint32_t kAddrCanItemReachDistance = 0x080167C8u;
inline constexpr uint32_t kAddrUnitEquipItemSlot = 0x080167F4u;
inline constexpr uint32_t kAddrIsItemEffectiveAgainst = 0x08016820u;
inline constexpr uint32_t kAddrGetWeaponLevelFromExp = 0x08016948u;
inline constexpr uint32_t kAddrGetItemIid = 0x080171B4u;
inline constexpr uint32_t kAddrGetItemDescId = 0x0801722Cu;
inline constexpr uint32_t kAddrGetItemUseDescId = 0x08017244u;
inline constexpr uint32_t kAddrGetItemKind = 0x0801725Cu;
inline constexpr uint32_t kAddrGetItemAttributes = 0x0801727Cu;
inline constexpr uint32_t kAddrGetItemUses = 0x08017294u;
inline constexpr uint32_t kAddrGetItemMaxUses = 0x080172BCu;
inline constexpr uint32_t kAddrGetItemMight = 0x080172E0u;
inline constexpr uint32_t kAddrGetItemHit = 0x080172F8u;
inline constexpr uint32_t kAddrGetItemWeight = 0x08017310u;
inline constexpr uint32_t kAddrGetItemCrit = 0x08017328u;
inline constexpr uint32_t kAddrGetItemValue = 0x08017340u;
inline constexpr uint32_t kAddrGetItemMinRange = 0x0801736Cu;
inline constexpr uint32_t kAddrGetItemMaxRange = 0x08017384u;
inline constexpr uint32_t kAddrGetItemEncodedRange = 0x080173A0u;
inline constexpr uint32_t kAddrGetItemRequiredExp = 0x080173B8u;
inline constexpr uint32_t kAddrGetItemEffectiveness = 0x080173D0u;
inline constexpr uint32_t kAddrGetItemBonuses = 0x080173E8u;
inline constexpr uint32_t kAddrGetItemIcon = 0x08017400u;
inline constexpr uint32_t kAddrGetItemWeaponEffect = 0x08017424u;
inline constexpr uint32_t kAddrGetItemEffect = 0x0801743Cu;
inline constexpr uint32_t kAddrGetItemCostPerUse = 0x08017454u;
inline constexpr uint32_t kAddrGetItemMaxValue = 0x0801746Cu;
inline constexpr uint32_t kAddrGetItemWeaponExp = 0x08017494u;
inline constexpr uint32_t kAddrGetIInfo = 0x080174ACu;

// Stat bonuses granted while `item` is equipped (0 if null/no bonus table).
int GetItemHpBonus(int item);
int GetItemPowBonus(int item);
int GetItemSklBonus(int item);
int GetItemSpdBonus(int item);
int GetItemDefBonus(int item);
int GetItemResBonus(int item);
int GetItemLckBonus(int item);

// Packs a fresh `(uses << 8) | iid` halfword (uses=0 for unbreakable items).
int CreateItem(int iid);

// Weapon and staff usability checks (locks, silence, magic seal, and WEXP).
bool CanUnitUseWeapon(GbaPtr<const Unit> unit, int item);
bool CanUnitUseWeaponNow(GbaPtr<const Unit> unit, int item);
bool CanUnitUseStaff(GbaPtr<const Unit> unit, int item);
bool CanUnitUseStaffNow(GbaPtr<const Unit> unit, int item);

// Decrements item uses by 1 (returning 0 when broken; unchanged if
// unbreakable).
uint16_t GetItemAfterUse(int item);

// Equipped weapon lookup and slot reordering helpers.
int GetUnitEquippedWeapon(GbaPtr<const Unit> unit);
int GetUnitEquippedWeaponSlot(GbaPtr<const Unit> unit);
bool CanItemReachDistance(int item, int distance);
void UnitEquipItemSlot(GbaPtr<Unit> unit, int slot);

// Returns true iff `item` deals effective damage against `target` (respecting
// Delphi Shield / `kItemAttrNegateFlying` on flying effectiveness).
bool IsItemEffectiveAgainst(uint16_t item, GbaPtr<const Unit> target);

// Maps weapon experience (0..255) to WeaponRank (0=None, 1=E .. 6=S).
int GetWeaponLevelFromExp(int wexp);

// Direct item data table property getters.
int GetItemIid(int item);
int GetItemDescId(int item);
int GetItemUseDescId(int item);
int GetItemKind(int item);
uint32_t GetItemAttributes(int item);
int GetItemUses(int item);
int GetItemMaxUses(int item);
int GetItemMight(int item);
int GetItemHit(int item);
int GetItemWeight(int item);
int GetItemCrit(int item);
int GetItemValue(int item);
int GetItemMinRange(int item);
int GetItemMaxRange(int item);
int GetItemEncodedRange(int item);
int GetItemRequiredExp(int item);
GbaPtr<const uint8_t> GetItemEffectiveness(int item);
GbaPtr<const ItemStatBonuses> GetItemBonuses(int item);
int GetItemIcon(int item);
int GetItemWeaponEffect(int item);
int GetItemEffect(int item);
int GetItemCostPerUse(int item);
int GetItemMaxValue(int item);
int GetItemWeaponExp(int item);
GbaPtr<const IInfo> GetIInfo(int iid);

// ROM address for UnitRemoveInvalidItems (inventory compaction).
inline constexpr uint32_t kAddrUnitRemoveInvalidItems = 0x08017688u;

// Compacts `unit->items[0..4]` in place, removing zero entries and zero-padding
// trailing slots. Matches FE7 US `UnitRemoveInvalidItems` (0x08017688).
void UnitRemoveInvalidItems(GbaPtr<Unit> unit);

// Registers all 43 native item functions in the global dispatch table.
void RegisterItemFunctions();

}  // namespace fe7

#endif  // FE7_GAME_ITEM_ITEM_H_
