// Synthetic unit tests for FE7 unit actions, rescue/drop/give, and boosters.

#include "game/action/action.h"

#include <array>
#include <cstdint>
#include <cstring>

#include "game/engine/rng.h"
#include "game/item.h"
#include "game/item/item.h"
#include "game/map/map.h"
#include "game/map/trap.h"
#include "game/phase/phase.h"
#include "game/unit.h"
#include "game/unit/unit.h"
#include "gtest/gtest.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

alignas(16) std::array<uint8_t, 0x40000u> gSyntheticEwram{};
alignas(16) std::array<uint8_t, 0x8000u> gSyntheticIwram{};
alignas(16) std::array<uint8_t, 0x01000000u> gSyntheticRom{};

class ActionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    gSyntheticEwram.fill(0u);
    gSyntheticIwram.fill(0u);
    gSyntheticRom.fill(0u);

    gMem.ewram = gSyntheticEwram.data();
    gMem.iwram = gSyntheticIwram.data();
    gMem.rom = gSyntheticRom.data();
    gMem.rom_size = gSyntheticRom.size();

    GbaPtr<MapSize> sz(kMapSizeAddr);
    sz->x = 8;
    sz->y = 8;
    MapInit(GbaPtr<uint8_t>(0x02011000u),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr), 8, 8);

    for (uint32_t id = 1; id <= 3; ++id) {
      const uint32_t addr = 0x0202BD50u + (id - 1u) * sizeof(Unit);
      std::memcpy(gSyntheticRom.data() + (kUnitLutAddr - 0x08000000u) + id * 4u,
                  &addr, sizeof(addr));
    }

    // Class 1 (Cavalier, Aid=16) and Class 2 (Lord, Con=5).
    JInfo cav{};
    cav.id = 1;
    cav.base_con = 9;
    cav.max_hp = 60;
    cav.max_pow = 20;
    cav.max_skl = 20;
    cav.max_spd = 20;
    cav.max_def = 20;
    cav.max_res = 20;
    cav.max_con = 20;
    cav.attributes = kUnitAttrMounted;
    std::memcpy(gSyntheticRom.data() + (kJInfoTableAddr - 0x08000000u) +
                    1u * sizeof(JInfo),
                &cav, sizeof(cav));

    JInfo lord{};
    lord.id = 2;
    lord.base_con = 5;
    lord.max_hp = 60;
    lord.max_pow = 20;
    lord.max_skl = 20;
    lord.max_spd = 20;
    lord.max_def = 20;
    lord.max_res = 20;
    lord.max_con = 20;
    std::memcpy(gSyntheticRom.data() + (kJInfoTableAddr - 0x08000000u) +
                    2u * sizeof(JInfo),
                &lord, sizeof(lord));

    // Item 0x5A (Angelic Robe: +7 HP).
    constexpr uint32_t kRobeBonusAddr = 0x08010000u;
    ItemStatBonuses robe_b{};
    robe_b.hp = 7;
    std::memcpy(gSyntheticRom.data() + (kRobeBonusAddr - 0x08000000u), &robe_b,
                sizeof(robe_b));
    IInfo robe{};
    robe.id = 0x5A;
    robe.max_uses = 1;
    robe.bonuses = GbaPtr<const ItemStatBonuses>(kRobeBonusAddr);
    std::memcpy(gSyntheticRom.data() + (kIInfoTableAddr - 0x08000000u) +
                    0x5Au * sizeof(IInfo),
                &robe, sizeof(robe));

    IInfo afa{};
    afa.id = 0x88;
    afa.max_uses = 1;
    std::memcpy(gSyntheticRom.data() + (kIInfoTableAddr - 0x08000000u) +
                    0x88u * sizeof(IInfo),
                &afa, sizeof(afa));
  }

  void TearDown() override { gMem = {}; }
};

TEST_F(ActionTest, RescueSyncGiveAndDropFlow) {
  GbaPtr<Unit> kent = GetUnit(1);
  GbaPtr<Unit> lyn = GetUnit(2);
  GbaPtr<Unit> sain = GetUnit(3);
  kent->id = 1;
  kent->pinfo = GetPersonInfo(1);
  kent->jinfo = GetJobInfo(1);
  kent->x = 2;
  kent->y = 3;

  lyn->id = 2;
  lyn->pinfo = GetPersonInfo(2);
  lyn->jinfo = GetJobInfo(2);
  lyn->x = 3;
  lyn->y = 3;

  sain->id = 3;
  sain->pinfo = GetPersonInfo(3);
  sain->jinfo = GetJobInfo(1);
  sain->x = 4;
  sain->y = 3;

  EXPECT_TRUE(CanUnitCarry(GbaPtr<const Unit>(kent.addr),
                           GbaPtr<const Unit>(lyn.addr)));
  UnitRescue(kent, lyn);
  EXPECT_EQ(kent->rescue, 2u);
  EXPECT_EQ(lyn->rescue, 1u);
  EXPECT_NE(lyn->flags & kUnitFlagRescued, 0u);

  kent->x = 5;
  kent->y = 6;
  UnitSyncMovement(kent);
  EXPECT_EQ(lyn->x, 5);
  EXPECT_EQ(lyn->y, 6);

  UnitGiveRescue(kent, sain);
  EXPECT_EQ(kent->rescue, 0u);
  EXPECT_EQ(sain->rescue, 2u);

  *GbaPtr<uint8_t>(kActionPlayStatusAddr + 0x0Fu) = kFactionBlue;
  UnitDropRescue(sain, 6, 6);
  EXPECT_EQ(sain->rescue, 0u);
  EXPECT_EQ(lyn->rescue, 0u);
  EXPECT_EQ(lyn->x, 6);
  EXPECT_EQ(lyn->y, 6);
  EXPECT_NE(lyn->flags & kUnitFlagTurnEnded, 0u);
}

TEST_F(ActionTest, StatBoostersAndAfasDrops) {
  GbaPtr<Unit> lyn = GetUnit(2);
  lyn->id = 2;
  lyn->pinfo = GetPersonInfo(2);
  lyn->jinfo = GetJobInfo(2);
  lyn->max_hp = 18;
  lyn->hp = 18;
  lyn->items[0] = static_cast<uint16_t>((1u << 8) | 0x5Au);
  lyn->items[1] = static_cast<uint16_t>((1u << 8) | 0x88u);

  EXPECT_EQ(ApplyItemStatBoost(lyn, 0), 0x0718);
  EXPECT_EQ(lyn->max_hp, 25);
  EXPECT_EQ(lyn->hp, 25);
  EXPECT_EQ(GetItemIid(lyn->items[0]), 0x88);  // Robe consumed & compacted.

  EXPECT_EQ(ApplyItemStatBoost(lyn, 0), 0x0719);
  EXPECT_NE(lyn->flags & kUnitFlagGrowthBoost, 0u);
  EXPECT_EQ(lyn->items[0], 0u);
}

TEST_F(ActionTest, AidIconsAndBeginAction) {
  EXPECT_EQ(GetAidIconFromAttributes(kUnitAttrMounted), 0x81);
  EXPECT_EQ(GetAidIconFromAttributes(kUnitAttrPegasus), 0x82);
  EXPECT_EQ(GetAidIconFromAttributes(kUnitAttrWyvern), 0x83);
  EXPECT_EQ(GetAidIconFromAttributes(0u), -1);

  GbaPtr<Unit> u = GetUnit(1);
  u->id = 1;
  u->x = 3;
  u->y = 4;
  (*GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr))[4][3] = 1u;

  UnitBeginAction(u);
  EXPECT_EQ(*GbaPtr<uint32_t>(kActionActiveUnitAddr), u.addr);
  EXPECT_EQ(*GbaPtr<uint8_t>(kActionActiveUnitIdAddr), 1u);
  EXPECT_NE(u->flags & kUnitFlagHidden, 0u);
  EXPECT_EQ((*GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr))[4][3], 0u);
}

}  // namespace
}  // namespace fe7
