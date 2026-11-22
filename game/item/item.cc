#include "game/item/item.h"

#include <cstdint>

#include "absl/base/nullability.h"
#include "game/item.h"
#include "game/unit.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

GbaPtr<const IInfo> InfoForItem(int item) {
  return GbaPtr<const IInfo>(kIInfoTableAddr) + (item & 0xFF);
}

uint32_t ThunkGetItemHpBonus(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                             uint32_t /*r3*/,
                             const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemHpBonus(static_cast<int>(r0)));
}

uint32_t ThunkGetItemPowBonus(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                              uint32_t /*r3*/,
                              const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemPowBonus(static_cast<int>(r0)));
}

uint32_t ThunkGetItemSklBonus(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                              uint32_t /*r3*/,
                              const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemSklBonus(static_cast<int>(r0)));
}

uint32_t ThunkGetItemSpdBonus(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                              uint32_t /*r3*/,
                              const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemSpdBonus(static_cast<int>(r0)));
}

uint32_t ThunkGetItemDefBonus(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                              uint32_t /*r3*/,
                              const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemDefBonus(static_cast<int>(r0)));
}

uint32_t ThunkGetItemResBonus(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                              uint32_t /*r3*/,
                              const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemResBonus(static_cast<int>(r0)));
}

uint32_t ThunkGetItemLckBonus(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                              uint32_t /*r3*/,
                              const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemLckBonus(static_cast<int>(r0)));
}

uint32_t ThunkCreateItem(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                         uint32_t /*r3*/,
                         const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(CreateItem(static_cast<int>(r0)));
}

uint32_t ThunkCanUnitUseWeapon(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                               uint32_t /*r3*/,
                               const uint32_t* absl_nullable /*stack*/) {
  return CanUnitUseWeapon(GbaPtr<const Unit>(r0), static_cast<int>(r1)) ? 1u
                                                                        : 0u;
}

uint32_t ThunkCanUnitUseWeaponNow(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                                  uint32_t /*r3*/,
                                  const uint32_t* absl_nullable /*stack*/) {
  return CanUnitUseWeaponNow(GbaPtr<const Unit>(r0), static_cast<int>(r1)) ? 1u
                                                                           : 0u;
}

uint32_t ThunkCanUnitUseStaff(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                              uint32_t /*r3*/,
                              const uint32_t* absl_nullable /*stack*/) {
  return CanUnitUseStaff(GbaPtr<const Unit>(r0), static_cast<int>(r1)) ? 1u
                                                                       : 0u;
}

uint32_t ThunkCanUnitUseStaffNow(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                                 uint32_t /*r3*/,
                                 const uint32_t* absl_nullable /*stack*/) {
  return CanUnitUseStaffNow(GbaPtr<const Unit>(r0), static_cast<int>(r1)) ? 1u
                                                                          : 0u;
}

uint32_t ThunkGetItemAfterUse(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                              uint32_t /*r3*/,
                              const uint32_t* absl_nullable /*stack*/) {
  return GetItemAfterUse(static_cast<int>(r0));
}

uint32_t ThunkGetUnitEquippedWeapon(uint32_t r0, uint32_t /*r1*/,
                                    uint32_t /*r2*/, uint32_t /*r3*/,
                                    const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetUnitEquippedWeapon(GbaPtr<const Unit>(r0)));
}

uint32_t ThunkGetUnitEquippedWeaponSlot(
    uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/, uint32_t /*r3*/,
    const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(
      GetUnitEquippedWeaponSlot(GbaPtr<const Unit>(r0)));
}

uint32_t ThunkCanItemReachDistance(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                                   uint32_t /*r3*/,
                                   const uint32_t* absl_nullable /*stack*/) {
  return CanItemReachDistance(static_cast<int>(r0), static_cast<int>(r1)) ? 1u
                                                                          : 0u;
}

uint32_t ThunkUnitEquipItemSlot(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                                uint32_t /*r3*/,
                                const uint32_t* absl_nullable /*stack*/) {
  UnitEquipItemSlot(GbaPtr<Unit>(r0), static_cast<int>(r1));
  return 0u;
}

uint32_t ThunkIsItemEffectiveAgainst(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                                     uint32_t /*r3*/,
                                     const uint32_t* absl_nullable /*stack*/) {
  return IsItemEffectiveAgainst(static_cast<uint16_t>(r0 & 0xFFFFu),
                                GbaPtr<const Unit>(r1))
             ? 1u
             : 0u;
}

uint32_t ThunkGetWeaponLevelFromExp(uint32_t r0, uint32_t /*r1*/,
                                    uint32_t /*r2*/, uint32_t /*r3*/,
                                    const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetWeaponLevelFromExp(static_cast<int>(r0)));
}

uint32_t ThunkGetItemIid(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                         uint32_t /*r3*/,
                         const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemIid(static_cast<int>(r0)));
}

uint32_t ThunkGetItemDescId(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                            uint32_t /*r3*/,
                            const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemDescId(static_cast<int>(r0)));
}

uint32_t ThunkGetItemUseDescId(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                               uint32_t /*r3*/,
                               const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemUseDescId(static_cast<int>(r0)));
}

uint32_t ThunkGetItemKind(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                          uint32_t /*r3*/,
                          const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemKind(static_cast<int>(r0)));
}

uint32_t ThunkGetItemAttributes(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                                uint32_t /*r3*/,
                                const uint32_t* absl_nullable /*stack*/) {
  return GetItemAttributes(static_cast<int>(r0));
}

uint32_t ThunkGetItemUses(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                          uint32_t /*r3*/,
                          const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemUses(static_cast<int>(r0)));
}

uint32_t ThunkGetItemMaxUses(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                             uint32_t /*r3*/,
                             const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemMaxUses(static_cast<int>(r0)));
}

uint32_t ThunkGetItemMight(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                           uint32_t /*r3*/,
                           const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemMight(static_cast<int>(r0)));
}

uint32_t ThunkGetItemHit(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                         uint32_t /*r3*/,
                         const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemHit(static_cast<int>(r0)));
}

uint32_t ThunkGetItemWeight(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                            uint32_t /*r3*/,
                            const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemWeight(static_cast<int>(r0)));
}

uint32_t ThunkGetItemCrit(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                          uint32_t /*r3*/,
                          const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemCrit(static_cast<int>(r0)));
}

uint32_t ThunkGetItemValue(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                           uint32_t /*r3*/,
                           const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemValue(static_cast<int>(r0)));
}

uint32_t ThunkGetItemMinRange(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                              uint32_t /*r3*/,
                              const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemMinRange(static_cast<int>(r0)));
}

uint32_t ThunkGetItemMaxRange(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                              uint32_t /*r3*/,
                              const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemMaxRange(static_cast<int>(r0)));
}

uint32_t ThunkGetItemEncodedRange(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                                  uint32_t /*r3*/,
                                  const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemEncodedRange(static_cast<int>(r0)));
}

uint32_t ThunkGetItemRequiredExp(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                                 uint32_t /*r3*/,
                                 const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemRequiredExp(static_cast<int>(r0)));
}

uint32_t ThunkGetItemEffectiveness(uint32_t r0, uint32_t /*r1*/,
                                   uint32_t /*r2*/, uint32_t /*r3*/,
                                   const uint32_t* absl_nullable /*stack*/) {
  return GetItemEffectiveness(static_cast<int>(r0)).addr;
}

uint32_t ThunkGetItemBonuses(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                             uint32_t /*r3*/,
                             const uint32_t* absl_nullable /*stack*/) {
  return GetItemBonuses(static_cast<int>(r0)).addr;
}

uint32_t ThunkGetItemIcon(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                          uint32_t /*r3*/,
                          const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemIcon(static_cast<int>(r0)));
}

uint32_t ThunkGetItemWeaponEffect(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                                  uint32_t /*r3*/,
                                  const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemWeaponEffect(static_cast<int>(r0)));
}

uint32_t ThunkGetItemEffect(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                            uint32_t /*r3*/,
                            const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemEffect(static_cast<int>(r0)));
}

uint32_t ThunkGetItemCostPerUse(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                                uint32_t /*r3*/,
                                const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemCostPerUse(static_cast<int>(r0)));
}

uint32_t ThunkGetItemMaxValue(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                              uint32_t /*r3*/,
                              const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemMaxValue(static_cast<int>(r0)));
}

uint32_t ThunkGetItemWeaponExp(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                               uint32_t /*r3*/,
                               const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetItemWeaponExp(static_cast<int>(r0)));
}

uint32_t ThunkGetIInfo(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                       uint32_t /*r3*/,
                       const uint32_t* absl_nullable /*stack*/) {
  return GetIInfo(static_cast<int>(r0)).addr;
}

}  // namespace

int GetItemHpBonus(int item) {
  if (item == 0) {
    return 0;
  }
  const GbaPtr<const ItemStatBonuses> b = InfoForItem(item)->bonuses;
  return b ? static_cast<int>(b->hp) : 0;
}

int GetItemPowBonus(int item) {
  if (item == 0) {
    return 0;
  }
  const GbaPtr<const ItemStatBonuses> b = InfoForItem(item)->bonuses;
  return b ? static_cast<int>(b->pow) : 0;
}

int GetItemSklBonus(int item) {
  if (item == 0) {
    return 0;
  }
  const GbaPtr<const ItemStatBonuses> b = InfoForItem(item)->bonuses;
  return b ? static_cast<int>(b->skl) : 0;
}

int GetItemSpdBonus(int item) {
  if (item == 0) {
    return 0;
  }
  const GbaPtr<const ItemStatBonuses> b = InfoForItem(item)->bonuses;
  return b ? static_cast<int>(b->spd) : 0;
}

int GetItemDefBonus(int item) {
  if (item == 0) {
    return 0;
  }
  const GbaPtr<const ItemStatBonuses> b = InfoForItem(item)->bonuses;
  return b ? static_cast<int>(b->def) : 0;
}

int GetItemResBonus(int item) {
  if (item == 0) {
    return 0;
  }
  const GbaPtr<const ItemStatBonuses> b = InfoForItem(item)->bonuses;
  return b ? static_cast<int>(b->res) : 0;
}

int GetItemLckBonus(int item) {
  if (item == 0) {
    return 0;
  }
  const GbaPtr<const ItemStatBonuses> b = InfoForItem(item)->bonuses;
  return b ? static_cast<int>(b->lck) : 0;
}

int CreateItem(int iid) {
  const int masked = iid & 0xFF;
  const GbaPtr<const IInfo> info = InfoForItem(masked);
  const int uses =
      (info->attributes & kItemAttrUnbreakable) != 0u ? 0 : info->max_uses;
  return (uses << 8) + masked;
}

bool CanUnitUseWeapon(GbaPtr<const Unit> unit, int item) {
  if (item == 0) {
    return false;
  }
  const GbaPtr<const IInfo> info = InfoForItem(item);
  const uint32_t item_attrs = info->attributes;
  if ((item_attrs & kItemAttrWeapon) == 0u) {
    return false;
  }

  if ((item_attrs & kItemAttrLockAny) != 0u) {
    const uint32_t unit_attrs = UnitAttributes(*unit);
    if ((item_attrs & kItemAttrLockRoy) != 0u &&
        (unit_attrs & kUnitAttrLockRoy) == 0u) {
      return false;
    }
    if ((item_attrs & kItemAttrLockEliwood) != 0u &&
        (unit_attrs & kUnitAttrLockEliwood) == 0u) {
      return false;
    }
    if ((item_attrs & kItemAttrLockHector) != 0u &&
        (unit_attrs & kUnitAttrLockHector) == 0u) {
      return false;
    }
    if ((item_attrs & kItemAttrLockLyn) != 0u &&
        (unit_attrs & kUnitAttrLockLyn) == 0u) {
      return false;
    }
    if ((item_attrs & kItemAttrLockAthos) != 0u &&
        (unit_attrs & kUnitAttrLockAthos) == 0u) {
      return false;
    }
    if ((item_attrs & kItemAttrLockMyrm) != 0u &&
        (unit_attrs & kUnitAttrLockMyrm) == 0u) {
      return false;
    }
    if ((item_attrs & kItemAttrLockMonster) != 0u) {
      return (unit_attrs & kUnitAttrMonster) != 0u;
    }
    if ((item_attrs & kItemAttrUnusable) != 0u) {
      const uint32_t allowed = GbaCall(kAddrCheckUnusableWeaponLock, unit.addr,
                                       static_cast<uint32_t>(item));
      if (static_cast<int8_t>(allowed) == 0) {
        return false;
      }
    }
  }

  if (unit->status == static_cast<uint8_t>(UnitStatus::kSilenced) &&
      (item_attrs & kItemAttrMagic) != 0u) {
    return false;
  }

  return unit->wexp[info->kind] >= info->required_wexp;
}

bool CanUnitUseWeaponNow(GbaPtr<const Unit> unit, int item) {
  if (item == 0) {
    return false;
  }
  const uint32_t attrs = InfoForItem(item)->attributes;
  if ((attrs & kItemAttrWeapon) == 0u) {
    return false;
  }
  if ((attrs & kItemAttrMagic) != 0u) {
    const uint32_t sealed = GbaCall(kAddrIsUnitInMagicSealRange, unit.addr);
    if (static_cast<int8_t>(sealed) != 0) {
      return false;
    }
  }
  return CanUnitUseWeapon(unit, item);
}

bool CanUnitUseStaff(GbaPtr<const Unit> unit, int item) {
  if (item == 0) {
    return false;
  }
  const GbaPtr<const IInfo> info = InfoForItem(item);
  if ((info->attributes & kItemAttrStaff) == 0u) {
    return false;
  }
  const auto st = static_cast<UnitStatus>(unit->status);
  if (st == UnitStatus::kSleep || st == UnitStatus::kBerserk ||
      st == UnitStatus::kSilenced) {
    return false;
  }
  return unit->wexp[info->kind] >= info->required_wexp;
}

bool CanUnitUseStaffNow(GbaPtr<const Unit> unit, int item) {
  if (item == 0) {
    return false;
  }
  if ((InfoForItem(item)->attributes & kItemAttrStaff) == 0u) {
    return false;
  }
  const uint32_t sealed = GbaCall(kAddrIsUnitInMagicSealRange, unit.addr);
  if (static_cast<int8_t>(sealed) != 0) {
    return false;
  }
  return CanUnitUseStaff(unit, item);
}

uint16_t GetItemAfterUse(int item) {
  if ((InfoForItem(item)->attributes & kItemAttrUnbreakable) != 0u) {
    return static_cast<uint16_t>(item);
  }
  // Subtract 1 from the upper uses byte (`item - 0x100`) using unsigned
  // wraparound to match Thumb `adds r2, #0xFFFFFF00` without signed UB.
  const int32_t next =
      static_cast<int32_t>(static_cast<uint32_t>(item) - 0x100u);
  if (next < 0x100) {
    return 0u;
  }
  return static_cast<uint16_t>(next);
}

int GetUnitEquippedWeapon(GbaPtr<const Unit> unit) {
  for (int i = 0; i < kUnitItemCount; ++i) {
    if (CanUnitUseWeapon(unit, unit->items[i])) {
      return static_cast<int>(unit->items[i]);
    }
  }
  return 0;
}

int GetUnitEquippedWeaponSlot(GbaPtr<const Unit> unit) {
  for (int i = 0; i < kUnitItemCount; ++i) {
    if (CanUnitUseWeaponNow(unit, unit->items[i])) {
      return i;
    }
  }
  return -1;
}

bool CanItemReachDistance(int item, int distance) {
  const uint8_t range = InfoForItem(item)->encoded_range;
  const int min_range = range >> 4;
  const int max_range = range & 0xF;
  return min_range <= distance && distance <= max_range;
}

void UnitEquipItemSlot(GbaPtr<Unit> unit, int slot) {
  const uint16_t chosen = unit->items[slot];
  for (int i = slot; i > 0; --i) {
    unit->items[i] = unit->items[i - 1];
  }
  unit->items[0] = chosen;
}

bool IsItemEffectiveAgainst(uint16_t item, GbaPtr<const Unit> target) {
  const uint8_t target_jid = target->jinfo->id;
  const GbaPtr<const uint8_t> list = InfoForItem(item)->effectiveness;
  if (!list) {
    return false;
  }
  bool matched = false;
  for (ptrdiff_t i = 0; list[i] != 0u; ++i) {
    if (list[i] == target_jid) {
      matched = true;
      break;
    }
  }
  if (!matched) {
    return false;
  }
  // Delphi Shield (`kItemAttrNegateFlying`) negates bows/Aircalibur against
  // the flying effectiveness list at ROM 0x08C97ED2.
  if (list.addr == kFlyingEffectivenessAddr) {
    uint32_t target_item_attrs = 0u;
    for (int i = 0; i < kUnitItemCount; ++i) {
      target_item_attrs |= InfoForItem(target->items[i])->attributes;
    }
    if ((target_item_attrs & kItemAttrNegateFlying) != 0u) {
      return false;
    }
  }
  return true;
}

int GetWeaponLevelFromExp(int wexp) {
  if (wexp <= 0) return static_cast<int>(WeaponRank::kNone);
  if (wexp <= 30) return static_cast<int>(WeaponRank::kE);
  if (wexp <= 70) return static_cast<int>(WeaponRank::kD);
  if (wexp <= 120) return static_cast<int>(WeaponRank::kC);
  if (wexp <= 180) return static_cast<int>(WeaponRank::kB);
  if (wexp <= 250) return static_cast<int>(WeaponRank::kA);
  return static_cast<int>(WeaponRank::kS);
}

int GetItemIid(int item) { return item & 0xFF; }

int GetItemDescId(int item) {
  return static_cast<int>(InfoForItem(item)->msg_desc);
}

int GetItemUseDescId(int item) {
  return static_cast<int>(InfoForItem(item)->msg_use_desc);
}

int GetItemKind(int item) {
  if (item == 0) {
    return 0xFF;
  }
  return static_cast<int>(InfoForItem(item)->kind);
}

uint32_t GetItemAttributes(int item) { return InfoForItem(item)->attributes; }

int GetItemUses(int item) {
  if ((InfoForItem(item)->attributes & kItemAttrUnbreakable) != 0u) {
    return 0xFF;
  }
  return item >> 8;
}

int GetItemMaxUses(int item) {
  const GbaPtr<const IInfo> info = InfoForItem(item);
  if ((info->attributes & kItemAttrUnbreakable) != 0u) {
    return 0xFF;
  }
  return static_cast<int>(info->max_uses);
}

int GetItemMight(int item) {
  return static_cast<int>(InfoForItem(item)->might);
}

int GetItemHit(int item) { return static_cast<int>(InfoForItem(item)->hit); }

int GetItemWeight(int item) {
  return static_cast<int>(InfoForItem(item)->weight);
}

int GetItemCrit(int item) { return static_cast<int>(InfoForItem(item)->crit); }

int GetItemValue(int item) {
  const GbaPtr<const IInfo> info = InfoForItem(item);
  if ((info->attributes & kItemAttrUnbreakable) != 0u) {
    return static_cast<int>(info->cost_per_use);
  }
  // Unsigned 32-bit multiply matches Thumb `muls` wraparound without UB.
  const uint32_t prod = static_cast<uint32_t>(item >> 8) *
                        static_cast<uint32_t>(info->cost_per_use);
  return static_cast<int32_t>(prod);
}

int GetItemMinRange(int item) {
  return static_cast<int>(InfoForItem(item)->encoded_range >> 4);
}

int GetItemMaxRange(int item) {
  return static_cast<int>(InfoForItem(item)->encoded_range & 0xF);
}

int GetItemEncodedRange(int item) {
  return static_cast<int>(InfoForItem(item)->encoded_range);
}

int GetItemRequiredExp(int item) {
  return static_cast<int>(InfoForItem(item)->required_wexp);
}

GbaPtr<const uint8_t> GetItemEffectiveness(int item) {
  return InfoForItem(item)->effectiveness;
}

GbaPtr<const ItemStatBonuses> GetItemBonuses(int item) {
  return InfoForItem(item)->bonuses;
}

int GetItemIcon(int item) {
  if (item == 0) {
    return -1;
  }
  return static_cast<int>(InfoForItem(item)->icon_id);
}

int GetItemWeaponEffect(int item) {
  return static_cast<int>(InfoForItem(item)->weapon_effect);
}

int GetItemEffect(int item) {
  return static_cast<int>(InfoForItem(item)->use_effect);
}

int GetItemCostPerUse(int item) {
  return static_cast<int>(InfoForItem(item)->cost_per_use);
}

int GetItemMaxValue(int item) {
  const GbaPtr<const IInfo> info = InfoForItem(item);
  const uint32_t max_uses =
      (info->attributes & kItemAttrUnbreakable) != 0u ? 0xFFu : info->max_uses;
  return static_cast<int32_t>(max_uses *
                              static_cast<uint32_t>(info->cost_per_use));
}

int GetItemWeaponExp(int item) {
  return static_cast<int>(InfoForItem(item)->wexp_gain);
}

GbaPtr<const IInfo> GetIInfo(int iid) {
  const uint32_t addr =
      kIInfoTableAddr +
      static_cast<uint32_t>(iid) * static_cast<uint32_t>(sizeof(IInfo));
  return GbaPtr<const IInfo>(addr);
}

void RegisterItemFunctions() {
  RegisterNative(kAddrItemHpBonus, &ThunkGetItemHpBonus);
  RegisterNative(kAddrItemPowBonus, &ThunkGetItemPowBonus);
  RegisterNative(kAddrItemSklBonus, &ThunkGetItemSklBonus);
  RegisterNative(kAddrItemSpdBonus, &ThunkGetItemSpdBonus);
  RegisterNative(kAddrItemDefBonus, &ThunkGetItemDefBonus);
  RegisterNative(kAddrItemResBonus, &ThunkGetItemResBonus);
  RegisterNative(kAddrItemLckBonus, &ThunkGetItemLckBonus);
  RegisterNative(kAddrCreateItem, &ThunkCreateItem);
  RegisterNative(kAddrCanUnitUseWeapon, &ThunkCanUnitUseWeapon);
  RegisterNative(kAddrCanUnitUseWeaponNow, &ThunkCanUnitUseWeaponNow);
  RegisterNative(kAddrCanUnitUseStaff, &ThunkCanUnitUseStaff);
  RegisterNative(kAddrCanUnitUseStaffNow, &ThunkCanUnitUseStaffNow);
  RegisterNative(kAddrGetItemAfterUse, &ThunkGetItemAfterUse);
  RegisterNative(kAddrUnitEquippedWeapon, &ThunkGetUnitEquippedWeapon);
  RegisterNative(kAddrUnitEquippedWeaponSlot, &ThunkGetUnitEquippedWeaponSlot);
  RegisterNative(kAddrCanItemReachDistance, &ThunkCanItemReachDistance);
  RegisterNative(kAddrUnitEquipItemSlot, &ThunkUnitEquipItemSlot);
  RegisterNative(kAddrIsItemEffectiveAgainst, &ThunkIsItemEffectiveAgainst);
  RegisterNative(kAddrGetWeaponLevelFromExp, &ThunkGetWeaponLevelFromExp);
  RegisterNative(kAddrGetItemIid, &ThunkGetItemIid);
  RegisterNative(kAddrGetItemDescId, &ThunkGetItemDescId);
  RegisterNative(kAddrGetItemUseDescId, &ThunkGetItemUseDescId);
  RegisterNative(kAddrGetItemKind, &ThunkGetItemKind);
  RegisterNative(kAddrGetItemAttributes, &ThunkGetItemAttributes);
  RegisterNative(kAddrGetItemUses, &ThunkGetItemUses);
  RegisterNative(kAddrGetItemMaxUses, &ThunkGetItemMaxUses);
  RegisterNative(kAddrGetItemMight, &ThunkGetItemMight);
  RegisterNative(kAddrGetItemHit, &ThunkGetItemHit);
  RegisterNative(kAddrGetItemWeight, &ThunkGetItemWeight);
  RegisterNative(kAddrGetItemCrit, &ThunkGetItemCrit);
  RegisterNative(kAddrGetItemValue, &ThunkGetItemValue);
  RegisterNative(kAddrGetItemMinRange, &ThunkGetItemMinRange);
  RegisterNative(kAddrGetItemMaxRange, &ThunkGetItemMaxRange);
  RegisterNative(kAddrGetItemEncodedRange, &ThunkGetItemEncodedRange);
  RegisterNative(kAddrGetItemRequiredExp, &ThunkGetItemRequiredExp);
  RegisterNative(kAddrGetItemEffectiveness, &ThunkGetItemEffectiveness);
  RegisterNative(kAddrGetItemBonuses, &ThunkGetItemBonuses);
  RegisterNative(kAddrGetItemIcon, &ThunkGetItemIcon);
  RegisterNative(kAddrGetItemWeaponEffect, &ThunkGetItemWeaponEffect);
  RegisterNative(kAddrGetItemEffect, &ThunkGetItemEffect);
  RegisterNative(kAddrGetItemCostPerUse, &ThunkGetItemCostPerUse);
  RegisterNative(kAddrGetItemMaxValue, &ThunkGetItemMaxValue);
  RegisterNative(kAddrGetItemWeaponExp, &ThunkGetItemWeaponExp);
  RegisterNative(kAddrGetIInfo, &ThunkGetIInfo);
}

}  // namespace fe7
