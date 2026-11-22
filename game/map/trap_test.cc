// Synthetic unit tests for FE7 map traps, obstacles, light runes, and fog maps.

#include "game/map/trap.h"

#include <array>
#include <cstdint>
#include <cstring>

#include "game/map/map.h"
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

constexpr int kW = 8;
constexpr int kH = 8;

class TrapTest : public ::testing::Test {
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
    sz->x = kW;
    sz->y = kH;

    MapInit(GbaPtr<uint8_t>(0x02010000u),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr), kW, kH);
    MapInit(GbaPtr<uint8_t>(0x02011000u),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr), kW, kH);
    MapInit(GbaPtr<uint8_t>(0x02012000u),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapFogAddr), kW, kH);
    MapInit(GbaPtr<uint8_t>(0x02013000u),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapHiddenAddr), kW, kH);

    // Populate gUnitLookup for Blue 1 and Red 0x81.
    const uint32_t blue1 = 0x0202BD50u;
    const uint32_t red1 = 0x0202CEB8u;
    std::memcpy(gSyntheticRom.data() + (kUnitLutAddr - 0x08000000u) + 1u * 4u,
                &blue1, sizeof(blue1));
    std::memcpy(
        gSyntheticRom.data() + (kUnitLutAddr - 0x08000000u) + 0x81u * 4u, &red1,
        sizeof(red1));
    InitTraps();
  }

  void TearDown() override { gMem = {}; }
};

TEST_F(TrapTest, AddQueryTickAndRemoveTraps) {
  GbaPtr<Trap> fire = AddFireTrap(3, 4, 2, 3);
  EXPECT_EQ(fire->type, static_cast<uint8_t>(TrapType::kFire));
  EXPECT_EQ(fire->data[2], 2u);
  EXPECT_EQ(fire->data[3], 10u);

  EXPECT_EQ(GetTrapAt(3, 4).addr, fire.addr);
  EXPECT_EQ(GetSpecificTrapAt(3, 4, static_cast<int>(TrapType::kFire)).addr,
            fire.addr);
  EXPECT_FALSE(GetSpecificTrapAt(3, 4, static_cast<int>(TrapType::kGas)));

  TickDamagingTraps();
  EXPECT_EQ(fire->data[2], 1u);
  TickDamagingTraps();
  EXPECT_EQ(fire->data[2], 0u);
  ResetExpiredDamagingTraps();
  EXPECT_EQ(fire->data[2], 3u);

  RemoveTrap(fire);
  EXPECT_FALSE(GetTrapAt(3, 4));
}

TEST_F(TrapTest, LightRuneAndMapChangeTraps) {
  GbaPtr<GbaPtr<uint8_t>> terrain =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr);
  terrain[2][5] = 1u;

  GbaPtr<Trap> rune = AddLightRune(5, 2);
  EXPECT_EQ(rune->extra, 1u);
  EXPECT_EQ(rune->data[2], 3u);
  EXPECT_EQ(terrain[2][5], 0u);
  EXPECT_EQ(GetTrapExtraAt(5, 2), 1);

  AddMapChangeTrap(7);
  EXPECT_TRUE(GetSpecificTrapAt(0, 0, static_cast<int>(TrapType::kMapChange)));
  RemoveMapChangeTrap(7);
  EXPECT_FALSE(GetSpecificTrapAt(0, 0, static_cast<int>(TrapType::kMapChange)));
}

TEST_F(TrapTest, RefreshEntityMapsHandlesFogTorchlightAndMines) {
  // Enable fog of war (base vision = 3).
  *GbaPtr<uint8_t>(kMapPlayStatusAddr + 0x0Du) = 3u;
  *GbaPtr<uint8_t>(kMapPlayStatusAddr + 0x0Fu) = kFactionBlue;

  // Blue unit 1 at (1, 1); Red unit 0x81 at (7, 7) outside fog vision.
  GbaPtr<Unit> u1 = GetUnit(1);
  u1->pinfo = GetPersonInfo(1);
  u1->jinfo = GetJobInfo(1);
  u1->id = 1;
  u1->x = 1;
  u1->y = 1;

  GbaPtr<Unit> e1 = GetUnit(0x81);
  e1->pinfo = GetPersonInfo(2);
  e1->jinfo = GetJobInfo(1);
  e1->id = static_cast<int8_t>(0x81);
  e1->x = 7;
  e1->y = 7;

  // Torchlight at (5, 2) with radius 2, and a mine at (4, 4).
  AddTrap(5, 2, static_cast<int>(TrapType::kTorchlight), 2);
  AddTrap(4, 4, static_cast<int>(TrapType::kMine), 0);

  RefreshEntityMaps();

  GbaPtr<GbaPtr<uint8_t>> umap = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr);
  GbaPtr<GbaPtr<uint8_t>> fog = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapFogAddr);
  GbaPtr<GbaPtr<uint8_t>> hid =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapHiddenAddr);

  EXPECT_EQ(umap[1][1], 1u);
  EXPECT_GT(fog[1][1], 0u);
  EXPECT_GT(fog[2][5], 0u);  // Torchlight lit.
  EXPECT_EQ(fog[7][7], 0u);  // Enemy in fog.
  EXPECT_EQ(umap[7][7], 0u);
  EXPECT_NE(hid[7][7] & 1u, 0u);
  EXPECT_NE(e1->flags & kUnitFlagFogHidden, 0u);
  EXPECT_NE(hid[4][4] & 2u, 0u);  // Mine marked on hidden map.
}

}  // namespace
}  // namespace fe7
