// FE7 US (AE7E) unit action execution, rescue/drop/give, autoleveling, item
// consumption, key lookup, and stat-booster mechanics.

#include "game/action/action.h"

#include <cstdint>

#include "absl/base/nullability.h"
#include "game/battle/battle_exp.h"
#include "game/item.h"
#include "game/item/item.h"
#include "game/map/map.h"
#include "game/map/trap.h"
#include "game/phase/phase.h"
#include "game/support/support.h"
#include "game/unit.h"
#include "game/unit/unit.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

int FindUnitItemSlot(GbaPtr<const Unit> unit, int iid) {
  for (int i = 0; i < kUnitItemCount; ++i) {
    if (static_cast<int>(unit->items[i] & 0xFFu) == iid) {
      return i;
    }
  }
  return -1;
}

bool UnitHasItem(GbaPtr<const Unit> unit, int item) {
  const int target_iid = GetItemIid(item);
  for (int i = 0; i < kUnitItemCount && unit->items[i] != 0u; ++i) {
    if (GetItemIid(unit->items[i]) == target_iid) {
      return true;
    }
  }
  return false;
}

void UnitInitStats(GbaPtr<Unit> unit, GbaPtr<const PInfo> pinfo) {
  const GbaPtr<const JInfo> j = unit->jinfo;
  unit->max_hp = static_cast<int8_t>(pinfo->base_hp + j->base_hp);
  unit->pow = static_cast<int8_t>(pinfo->base_pow + j->base_pow);
  unit->skl = static_cast<int8_t>(pinfo->base_skl + j->base_skl);
  unit->spd = static_cast<int8_t>(pinfo->base_spd + j->base_spd);
  unit->def = static_cast<int8_t>(pinfo->base_def + j->base_def);
  unit->res = static_cast<int8_t>(pinfo->base_res + j->base_res);
  unit->lck = pinfo->base_lck;
  unit->bonus_con = 0;

  for (int i = 0; i < kUnitWexpCount; ++i) {
    unit->wexp[i] = unit->jinfo->wexp[i];
    if (unit->pinfo->wexp[i] != 0u) {
      unit->wexp[i] = unit->pinfo->wexp[i];
    }
  }

  if ((static_cast<uint8_t>(unit->id) & 0xC0u) == 0u && unit->level != 20 &&
      unit->pinfo->id != 0x28u) {
    unit->exp = 0u;
  } else {
    unit->exp = 0xFFu;
  }
}

void UnitApplyAltPinfo(GbaPtr<Unit> unit) {
  const uint32_t attrs = unit->pinfo->attributes | unit->jinfo->attributes;
  if ((attrs & kUnitAttrAltPinfo) != 0u) {
    unit->pinfo = GetPersonInfo(static_cast<int>(unit->pinfo->id) - 1);
  }
}

void UnitInitSupports(GbaPtr<Unit> unit) {
  GbaPtr<const Unit> unit_c(unit.addr);
  const int count = GetUnitSupporterCount(unit_c);
  for (int i = 0; i < count; ++i) {
    unit->supports[i] =
        static_cast<uint8_t>(GetUnitSupporterInitialExp(unit_c, i));
  }
}

void UnitAutolevelWeaponExp(GbaPtr<Unit> unit, GbaPtr<const uint8_t> udef) {
  if ((udef[3] & 1u) == 0u) {
    return;
  }
  GbaPtr<const Unit> unit_c(unit.addr);
  for (int i = 0; i < GetUnitItemCount(unit_c); ++i) {
    uint16_t item = unit->items[i];
    const uint32_t attrs = GetItemAttributes(item);
    if ((attrs & (kItemAttrWeapon | kItemAttrStaff)) == 0u) {
      continue;
    }
    if ((attrs & kItemAttrWeapon) != 0u && CanUnitUseWeapon(unit_c, item)) {
      continue;
    }
    if ((attrs & kItemAttrStaff) != 0u && CanUnitUseStaff(unit_c, item)) {
      continue;
    }
    if ((attrs & kItemAttrLockAny) != 0u) {
      continue;
    }
    const int kind = GetItemKind(item);
    if (unit->wexp[kind] == 0u) {
      item = 0u;
    }
    unit->wexp[kind] = static_cast<uint8_t>(GetItemRequiredExp(item));
  }
}

void UnitAutolevelCore(GbaPtr<Unit> unit, uint8_t /*jid*/, int level_count) {
  if (level_count == 0) {
    return;
  }
  const GbaPtr<const JInfo> j = unit->jinfo;
  unit->max_hp = static_cast<int8_t>(
      unit->max_hp + GetAutoleveledStatIncrease(j->growth_hp, level_count));
  unit->pow = static_cast<int8_t>(
      unit->pow + GetAutoleveledStatIncrease(j->growth_pow, level_count));
  unit->skl = static_cast<int8_t>(
      unit->skl + GetAutoleveledStatIncrease(j->growth_skl, level_count));
  unit->spd = static_cast<int8_t>(
      unit->spd + GetAutoleveledStatIncrease(j->growth_spd, level_count));
  unit->def = static_cast<int8_t>(
      unit->def + GetAutoleveledStatIncrease(j->growth_def, level_count));
  unit->res = static_cast<int8_t>(
      unit->res + GetAutoleveledStatIncrease(j->growth_res, level_count));
}

void UnitApplyBonusLevels(GbaPtr<Unit> unit, int level_count) {
  UnitAutolevelCore(unit, unit->jinfo->id, level_count);
  UnitCheckStatOverflow(unit);
  const uint16_t weapon = static_cast<uint16_t>(
      GetUnitEquippedWeapon(GbaPtr<const Unit>(unit.addr)));
  unit->hp = static_cast<int8_t>(unit->max_hp + GetItemHpBonus(weapon));
}

void UnitAutolevelPlayer(GbaPtr<Unit> unit) {
  const GbaPtr<const PInfo> p = unit->pinfo;
  const uint32_t attrs = p->attributes | unit->jinfo->attributes;
  int levels = unit->level - p->base_level;
  if ((attrs & kUnitAttrPromoted) != 0u) {
    levels = unit->level - (p->base_level - 14);
  }
  for (int i = 0; i < levels; ++i) {
    unit->max_hp =
        static_cast<int8_t>(unit->max_hp + GetStatIncrease(p->growth_hp));
    unit->pow = static_cast<int8_t>(unit->pow + GetStatIncrease(p->growth_pow));
    unit->skl = static_cast<int8_t>(unit->skl + GetStatIncrease(p->growth_skl));
    unit->spd = static_cast<int8_t>(unit->spd + GetStatIncrease(p->growth_spd));
    unit->def = static_cast<int8_t>(unit->def + GetStatIncrease(p->growth_def));
    unit->res = static_cast<int8_t>(unit->res + GetStatIncrease(p->growth_res));
    unit->lck = static_cast<int8_t>(unit->lck + GetStatIncrease(p->growth_lck));
  }
}

GbaPtr<Unit> GetUnitByPidInFaction(int pid, int faction) {
  for (int id = faction + 1; id < faction + 0x40; ++id) {
    GbaPtr<Unit> unit = GetUnit(id);
    if (unit && unit->pinfo && unit->pinfo->id == pid) {
      return unit;
    }
  }
  return GbaPtr<Unit>(0);
}

bool CanUnitCarry(GbaPtr<const Unit> actor, GbaPtr<const Unit> target) {
  const int aid = GetUnitAid(actor);
  const int target_con =
      target->jinfo->base_con + target->pinfo->bonus_con + target->bonus_con;
  return aid >= target_con;
}

void UnitRescue(GbaPtr<Unit> actor, GbaPtr<Unit> target) {
  actor->flags |= kUnitFlagRescuing;
  target->flags |= (kUnitFlagRescued | kUnitFlagHidden);
  actor->rescue = static_cast<uint8_t>(target->id);
  target->rescue = static_cast<uint8_t>(actor->id);
  target->x = actor->x;
  target->y = actor->y;
}

void UnitDropRescue(GbaPtr<Unit> actor, int x, int y) {
  GbaPtr<Unit> target = GetUnit(actor->rescue);
  actor->flags &= ~0x30u;
  target->flags &= ~0x31u;
  const uint8_t active_faction =
      *GbaPtr<const uint8_t>(kActionPlayStatusAddr + 0x0Fu);
  if ((static_cast<uint8_t>(target->id) & 0xC0u) == active_faction) {
    target->flags |= kUnitFlagTurnEnded;
  }
  actor->rescue = 0u;
  target->rescue = 0u;
  target->x = static_cast<int8_t>(x);
  target->y = static_cast<int8_t>(y);
}

void UnitGiveRescue(GbaPtr<Unit> actor, GbaPtr<Unit> target) {
  GbaPtr<Unit> rescuee = GetUnit(actor->rescue);
  // Retail FE7 calls CanUnitCarry for parity before transferring rescuee.
  (void)CanUnitCarry(GbaPtr<const Unit>(target.addr),
                     GbaPtr<const Unit>(rescuee.addr));
  UnitDropRescue(actor, 0, 0);
  UnitRescue(target, rescuee);
}

void KillUnit(GbaPtr<Unit> unit) {
  if ((static_cast<uint8_t>(unit->id) & 0xC0u) == 0u) {
    unit->flags |= (kUnitFlagHidden | kUnitFlagDead);
    ClearUnitSupports(unit);
  } else {
    unit->pinfo = GbaPtr<const PInfo>(0);
  }
}

void UnitSyncMovement(GbaPtr<Unit> unit) {
  if ((unit->flags & kUnitFlagRescuing) != 0u) {
    GbaPtr<Unit> rescuee = GetUnit(unit->rescue);
    rescuee->x = unit->x;
    rescuee->y = unit->y;
  }
  if ((unit->flags & kUnitFlagInBallista) != 0u) {
    GbaPtr<Trap> trap = GetTrap(unit->ballista_trap);
    trap->x = static_cast<uint8_t>(unit->x);
    trap->y = static_cast<uint8_t>(unit->y);
  }
}

void UnitBeginAction(GbaPtr<Unit> unit) {
  *GbaPtr<uint32_t>(kActionActiveUnitAddr) = unit.addr;
  *GbaPtr<uint8_t>(kActionActiveUnitIdAddr) = static_cast<uint8_t>(unit->id);
  GbaPtr<int16_t> origin(kActionMoveOriginAddr);
  origin[0] = unit->x;
  origin[1] = unit->y;

  GbaPtr<uint8_t> action(kActionDataAddr);
  action[0x0C] = static_cast<uint8_t>(unit->id);
  action[0x11] = 0u;
  action[0x10] = 0u;

  *GbaPtr<uint8_t>(kActionBmStatusAddr + 0x3Du) = 0u;
  *GbaPtr<uint8_t>(kActionBmStatusAddr + 0x3Fu) = 0xFFu;
  ClearBattleExpScratch();

  GbaPtr<Unit> active(*GbaPtr<uint32_t>(kActionActiveUnitAddr));
  active->flags |= kUnitFlagHidden;
  GbaPtr<GbaPtr<uint8_t>> umap = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr);
  umap[unit->y][unit->x] = 0u;
}

void UnitBeginReMoveAction(GbaPtr<Unit> unit) {
  *GbaPtr<uint32_t>(kActionActiveUnitAddr) = unit.addr;
  *GbaPtr<uint8_t>(kActionActiveUnitIdAddr) = static_cast<uint8_t>(unit->id);
  GbaPtr<int16_t> origin(kActionMoveOriginAddr);
  origin[0] = unit->x;
  origin[1] = unit->y;

  *GbaPtr<uint8_t>(kActionDataAddr + 0x11u) = 0u;
  *GbaPtr<uint8_t>(kActionBmStatusAddr + 0x3Du) = 0u;
  ClearBattleExpScratch();

  GbaPtr<Unit> active(*GbaPtr<uint32_t>(kActionActiveUnitAddr));
  active->flags |= kUnitFlagHidden;
  GbaPtr<GbaPtr<uint8_t>> umap = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr);
  umap[unit->y][unit->x] = 0u;
}

void UnitUpdateUsedItem(GbaPtr<Unit> unit, int item_slot) {
  if (unit->items[item_slot] != 0u) {
    unit->items[item_slot] =
        static_cast<uint16_t>(GetItemAfterUse(unit->items[item_slot]));
    UnitRemoveInvalidItems(unit);
  }
}

void UnitRevealAtPosition(GbaPtr<Unit> unit, int x, int y) {
  if ((unit->flags & kUnitFlagUnderARoof) == 0u) {
    unit->flags &= ~0x09u;
    unit->x = static_cast<int8_t>(x);
    unit->y = static_cast<int8_t>(y);
  }
}

int GetUnitKeyItemSlotForTerrain(GbaPtr<const Unit> unit, int terrain) {
  const uint32_t attrs = unit->pinfo->attributes | unit->jinfo->attributes;
  if ((attrs & kUnitAttrSteal) != 0u) {
    const int lockpick = FindUnitItemSlot(unit, 0x6A);
    if (lockpick >= 0) {
      return lockpick;
    }
  }
  if (terrain == 0x21) {  // Chest
    const int chest_key = FindUnitItemSlot(unit, 0x68);
    if (chest_key >= 0) {
      return chest_key;
    }
    return FindUnitItemSlot(unit, 0x78);  // 5-use Chest Key
  }
  const int key_iid = (terrain == 0x1E) ? 0x69 : 0;  // Door Key
  return FindUnitItemSlot(unit, key_iid);
}

int GetAidIconFromAttributes(uint32_t attributes) {
  if ((attributes & kUnitAttrMounted) != 0u) return 0x81;
  if ((attributes & kUnitAttrPegasus) != 0u) return 0x82;
  if ((attributes & kUnitAttrWyvern) != 0u) return 0x83;
  return -1;
}

int GetUnitUseFlags(GbaPtr<const Unit> unit) {
  int flags = 0;
  for (int i = 0; i < kUnitItemCount && unit->items[i] != 0u; ++i) {
    const uint16_t item = unit->items[i];
    if ((GetItemAttributes(item) & kItemAttrWeapon) != 0u &&
        CanUnitUseWeapon(unit, item)) {
      flags |= 1;
    }
    if ((GetItemAttributes(item) & kItemAttrStaff) != 0u &&
        CanUnitUseStaff(unit, item)) {
      flags |= 2;
    }
  }
  return flags;
}

int GetEnemyFactionUseFlags() {
  int flags = 0;
  for (int id = 0x81; id <= 0xBF; ++id) {
    GbaPtr<Unit> unit = GetUnit(id);
    if (unit && unit->pinfo) {
      flags |= GetUnitUseFlags(GbaPtr<const Unit>(unit.addr));
    }
  }
  return flags;
}

uint16_t GetUnitLastItem(GbaPtr<const Unit> unit) {
  const int count = GetUnitItemCount(unit);
  return unit->items[count - 1];
}

int ApplyItemStatBoost(GbaPtr<Unit> unit, int item_slot) {
  const uint16_t item = unit->items[item_slot];
  if (GetItemIid(item) == 0x88) {  // Afa's Drops
    unit->flags |= kUnitFlagGrowthBoost;
    UnitUpdateUsedItem(unit, item_slot);
    return 0x0719;
  }

  GbaPtr<const ItemStatBonuses> b = GetItemBonuses(item);
  unit->max_hp = static_cast<int8_t>(unit->max_hp + b->hp);
  unit->hp = static_cast<int8_t>(unit->hp + b->hp);
  unit->pow = static_cast<int8_t>(unit->pow + b->pow);
  unit->skl = static_cast<int8_t>(unit->skl + b->skl);
  unit->spd = static_cast<int8_t>(unit->spd + b->spd);
  unit->def = static_cast<int8_t>(unit->def + b->def);
  unit->res = static_cast<int8_t>(unit->res + b->res);
  unit->lck = static_cast<int8_t>(unit->lck + b->lck);
  unit->bonus_mov = static_cast<int8_t>(unit->bonus_mov + b->mov);
  unit->bonus_con = static_cast<int8_t>(unit->bonus_con + b->con);

  UnitCheckStatOverflow(unit);
  UnitUpdateUsedItem(unit, item_slot);

  switch (GetItemIid(item)) {
    case 0x5A:  // Angelic Robe
      return 0x0718;
    case 0x5B:  // Energy Ring
      return UnitKnowsMagic(GbaPtr<const Unit>(unit.addr)) ? 0x0710 : 0x070F;
    case 0x5C:  // Secret Book
      return 0x0711;
    case 0x5D:  // Speedwings
      return 0x0712;
    case 0x5E:  // Goddess Icon
      return 0x0713;
    case 0x5F:  // Dragonshield
      return 0x0714;
    case 0x60:  // Talisman
      return 0x0715;
    case 0x61:  // Boots
      return 0x0716;
    case 0x62:  // Body Ring
      return 0x0717;
    default:
      return 0;
  }
}

void RegisterActionFunctions() {
  RegisterNative(kAddrFindUnitItemSlot,
                 [](uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return static_cast<uint32_t>(FindUnitItemSlot(
                       GbaPtr<const Unit>(r0), static_cast<int32_t>(r1)));
                 });
  RegisterNative(kAddrUnitHasItem,
                 [](uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return UnitHasItem(GbaPtr<const Unit>(r0),
                                      static_cast<int32_t>(r1))
                              ? 1u
                              : 0u;
                 });
  RegisterNative(kAddrUnitInitStats,
                 [](uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   UnitInitStats(GbaPtr<Unit>(r0), GbaPtr<const PInfo>(r1));
                   return 0u;
                 });
  RegisterNative(kAddrUnitApplyAltPinfo,
                 [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   UnitApplyAltPinfo(GbaPtr<Unit>(r0));
                   return 0u;
                 });
  RegisterNative(kAddrUnitInitSupports,
                 [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   UnitInitSupports(GbaPtr<Unit>(r0));
                   return 0u;
                 });
  RegisterNative(kAddrUnitAutolevelWeaponExp,
                 [](uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   UnitAutolevelWeaponExp(GbaPtr<Unit>(r0),
                                          GbaPtr<const uint8_t>(r1));
                   return 0u;
                 });
  RegisterNative(kAddrUnitAutolevelCore,
                 [](uint32_t r0, uint32_t r1, uint32_t r2, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   UnitAutolevelCore(GbaPtr<Unit>(r0), static_cast<uint8_t>(r1),
                                     static_cast<int32_t>(r2));
                   return 0u;
                 });
  RegisterNative(kAddrUnitApplyBonusLevels,
                 [](uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   UnitApplyBonusLevels(GbaPtr<Unit>(r0),
                                        static_cast<int32_t>(r1));
                   return 0u;
                 });
  RegisterNative(kAddrUnitAutolevelPlayer,
                 [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   UnitAutolevelPlayer(GbaPtr<Unit>(r0));
                   return 0u;
                 });
  RegisterNative(kAddrGetUnitByPidInFaction,
                 [](uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return GetUnitByPidInFaction(static_cast<int32_t>(r0),
                                                static_cast<int32_t>(r1))
                       .addr;
                 });
  RegisterNative(kAddrCanUnitCarry,
                 [](uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return CanUnitCarry(GbaPtr<const Unit>(r0),
                                       GbaPtr<const Unit>(r1))
                              ? 1u
                              : 0u;
                 });
  RegisterNative(kAddrUnitRescue,
                 [](uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   UnitRescue(GbaPtr<Unit>(r0), GbaPtr<Unit>(r1));
                   return 0u;
                 });
  RegisterNative(kAddrUnitDropRescue,
                 [](uint32_t r0, uint32_t r1, uint32_t r2, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   UnitDropRescue(GbaPtr<Unit>(r0), static_cast<int32_t>(r1),
                                  static_cast<int32_t>(r2));
                   return 0u;
                 });
  RegisterNative(kAddrUnitGiveRescue,
                 [](uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   UnitGiveRescue(GbaPtr<Unit>(r0), GbaPtr<Unit>(r1));
                   return 0u;
                 });
  RegisterNative(kAddrKillUnit,
                 [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   KillUnit(GbaPtr<Unit>(r0));
                   return 0u;
                 });
  RegisterNative(kAddrUnitSyncMovement,
                 [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   UnitSyncMovement(GbaPtr<Unit>(r0));
                   return 0u;
                 });
  RegisterNative(kAddrUnitBeginAction,
                 [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   UnitBeginAction(GbaPtr<Unit>(r0));
                   return 0u;
                 });
  RegisterNative(kAddrUnitBeginReMoveAction,
                 [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   UnitBeginReMoveAction(GbaPtr<Unit>(r0));
                   return 0u;
                 });
  RegisterNative(kAddrUnitUpdateUsedItem,
                 [](uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   UnitUpdateUsedItem(GbaPtr<Unit>(r0),
                                      static_cast<int32_t>(r1));
                   return 0u;
                 });
  RegisterNative(kAddrUnitRevealAtPosition,
                 [](uint32_t r0, uint32_t r1, uint32_t r2, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   UnitRevealAtPosition(GbaPtr<Unit>(r0),
                                        static_cast<int32_t>(r1),
                                        static_cast<int32_t>(r2));
                   return 0u;
                 });
  RegisterNative(kAddrGetUnitKeyItemSlotForTerrain,
                 [](uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return static_cast<uint32_t>(GetUnitKeyItemSlotForTerrain(
                       GbaPtr<const Unit>(r0), static_cast<int32_t>(r1)));
                 });
  RegisterNative(kAddrGetAidIconFromAttributes,
                 [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return static_cast<uint32_t>(GetAidIconFromAttributes(r0));
                 });
  RegisterNative(
      kAddrGetUnitUseFlags,
      [](uint32_t r0, uint32_t, uint32_t, uint32_t,
         const uint32_t* absl_nullable) -> uint32_t {
        return static_cast<uint32_t>(GetUnitUseFlags(GbaPtr<const Unit>(r0)));
      });
  RegisterNative(kAddrGetEnemyFactionUseFlags,
                 [](uint32_t, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return static_cast<uint32_t>(GetEnemyFactionUseFlags());
                 });
  RegisterNative(kAddrGetUnitLastItem,
                 [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return GetUnitLastItem(GbaPtr<const Unit>(r0));
                 });
  RegisterNative(kAddrApplyItemStatBoost,
                 [](uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   return static_cast<uint32_t>(ApplyItemStatBoost(
                       GbaPtr<Unit>(r0), static_cast<int32_t>(r1)));
                 });
}

}  // namespace fe7
