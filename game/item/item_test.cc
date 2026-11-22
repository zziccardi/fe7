#include "game/item/item.h"

#include <array>
#include <cstdint>

#include "absl/base/nullability.h"
#include "game/item.h"
#include "game/unit.h"
#include "gtest/gtest.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

alignas(16) std::array<uint8_t, 0x01000000u> gSyntheticRom{};

uint32_t MockNotSealed(uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/,
                       uint32_t /*r3*/,
                       const uint32_t* absl_nullable /*stack*/) {
  return 0u;
}

class ItemTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ewram_.fill(0);
    iwram_.fill(0);
    // Zero only the IInfo table window we populate in tests.
    gSyntheticRom.fill(0);
    gMem.ewram = ewram_.data();
    gMem.iwram = iwram_.data();
    gMem.rom = gSyntheticRom.data();
    gMem.rom_size = gSyntheticRom.size();
    ResetDispatchTable();
    RegisterItemFunctions();
    RegisterNative(kAddrIsUnitInMagicSealRange, &MockNotSealed);
  }

  void TearDown() override {
    ResetDispatchTable();
    gMem = GbaMemoryMap{};
  }

  alignas(16) std::array<uint8_t, 0x40000> ewram_;
  alignas(16) std::array<uint8_t, 0x8000> iwram_;
};

TEST_F(ItemTest, CreateUsesValueAfterUseAndPropertyGetters) {
  GbaPtr<IInfo> iron_sword(kIInfoTableAddr + sizeof(IInfo));
  GbaPtr<ItemStatBonuses> bonuses(0x02001000u);
  bonuses->hp = 5;
  bonuses->pow = 2;
  bonuses->skl = 1;
  bonuses->spd = 3;
  bonuses->def = 4;
  bonuses->res = -1;
  bonuses->lck = 2;

  iron_sword->id = 1;
  iron_sword->kind = static_cast<uint8_t>(ItemKind::kSword);
  iron_sword->attributes = kItemAttrWeapon;
  iron_sword->bonuses = GbaPtr<const ItemStatBonuses>(bonuses.addr);
  iron_sword->max_uses = 46;
  iron_sword->might = 5;
  iron_sword->hit = 90;
  iron_sword->weight = 5;
  iron_sword->crit = 0;
  iron_sword->encoded_range = 0x11;
  iron_sword->cost_per_use = 10;
  iron_sword->required_wexp = 1;
  iron_sword->icon_id = 7;

  const int item = CreateItem(1);
  EXPECT_EQ(item, (46 << 8) | 1);
  EXPECT_EQ(GetItemIid(item), 1);
  EXPECT_EQ(GetItemUses(item), 46);
  EXPECT_EQ(GetItemMaxUses(item), 46);
  EXPECT_EQ(GetItemMight(item), 5);
  EXPECT_EQ(GetItemHit(item), 90);
  EXPECT_EQ(GetItemWeight(item), 5);
  EXPECT_EQ(GetItemMinRange(item), 1);
  EXPECT_EQ(GetItemMaxRange(item), 1);
  EXPECT_EQ(GetItemValue(item), 460);
  EXPECT_EQ(GetItemMaxValue(item), 460);
  EXPECT_EQ(GetItemIcon(item), 7);
  EXPECT_EQ(GetItemIcon(0), -1);
  EXPECT_EQ(GetItemKind(0), 0xFF);
  EXPECT_TRUE(CanItemReachDistance(item, 1));
  EXPECT_FALSE(CanItemReachDistance(item, 2));

  EXPECT_EQ(GetItemHpBonus(item), 5);
  EXPECT_EQ(GetItemPowBonus(item), 2);
  EXPECT_EQ(GetItemSklBonus(item), 1);
  EXPECT_EQ(GetItemSpdBonus(item), 3);
  EXPECT_EQ(GetItemDefBonus(item), 4);
  EXPECT_EQ(GetItemResBonus(item), -1);
  EXPECT_EQ(GetItemLckBonus(item), 2);

  EXPECT_EQ(GetItemAfterUse((2 << 8) | 1), static_cast<uint16_t>((1 << 8) | 1));
  EXPECT_EQ(GetItemAfterUse((1 << 8) | 1), 0u);
}

TEST_F(ItemTest, WeaponUsabilityEquipSlotAndWeaponLevel) {
  GbaPtr<IInfo> sword(kIInfoTableAddr + 1 * sizeof(IInfo));
  sword->kind = static_cast<uint8_t>(ItemKind::kSword);
  sword->attributes = kItemAttrWeapon;
  sword->required_wexp = 31;

  GbaPtr<IInfo> mani_katti(kIInfoTableAddr + 2 * sizeof(IInfo));
  mani_katti->kind = static_cast<uint8_t>(ItemKind::kSword);
  mani_katti->attributes = kItemAttrWeapon | kItemAttrLockLyn;
  mani_katti->required_wexp = 1;

  GbaPtr<PInfo> p(0x02002000u);
  GbaPtr<JInfo> j(0x02002100u);
  GbaPtr<Unit> u(0x02002200u);
  u->pinfo = GbaPtr<const PInfo>(p.addr);
  u->jinfo = GbaPtr<const JInfo>(j.addr);
  u->wexp[0] = 50;  // D rank swords
  u->items[0] = (10 << 8) | 2;
  u->items[1] = (20 << 8) | 1;

  // Without Lyn lock, slot 0 is unusable so slot 1 is equipped.
  EXPECT_FALSE(CanUnitUseWeapon(GbaPtr<const Unit>(u.addr), u->items[0]));
  EXPECT_TRUE(CanUnitUseWeapon(GbaPtr<const Unit>(u.addr), u->items[1]));
  EXPECT_EQ(GetUnitEquippedWeapon(GbaPtr<const Unit>(u.addr)), u->items[1]);
  EXPECT_EQ(GetUnitEquippedWeaponSlot(GbaPtr<const Unit>(u.addr)), 1);

  UnitEquipItemSlot(u, 1);
  EXPECT_EQ(u->items[0], (20 << 8) | 1);
  EXPECT_EQ(u->items[1], (10 << 8) | 2);

  EXPECT_EQ(GetWeaponLevelFromExp(0), 0);
  EXPECT_EQ(GetWeaponLevelFromExp(1), 1);
  EXPECT_EQ(GetWeaponLevelFromExp(31), 2);
  EXPECT_EQ(GetWeaponLevelFromExp(71), 3);
  EXPECT_EQ(GetWeaponLevelFromExp(121), 4);
  EXPECT_EQ(GetWeaponLevelFromExp(181), 5);
  EXPECT_EQ(GetWeaponLevelFromExp(251), 6);
}

}  // namespace
}  // namespace fe7
