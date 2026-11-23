#include "game/ai/ai_staff_loot.h"

#include <array>
#include <cstdint>

#include "game/ai/ai.h"
#include "game/item/item.h"
#include "game/map/map.h"
#include "game/unit.h"
#include "gtest/gtest.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

alignas(16) std::array<uint8_t, 0x40000> gTestEwram{};
alignas(16) std::array<uint8_t, 0x8000> gTestIwram{};
alignas(16) std::array<uint8_t, 0x01000000> gTestRom{};

class AiStaffLootTest : public ::testing::Test {
 protected:
  void SetUp() override {
    gTestEwram.fill(0);
    gTestIwram.fill(0);
    gTestRom.fill(0);
    gMem.ewram = gTestEwram.data();
    gMem.iwram = gTestIwram.data();
    gMem.rom = gTestRom.data();
    gMem.rom_size = static_cast<uint32_t>(gTestRom.size());

    ResetDispatchTable();
    RegisterItemFunctions();
    RegisterMapFunctions();
    RegisterAiFunctions();
    RegisterAiStaffLootFunctions();
  }

  void TearDown() override { ResetDispatchTable(); }
};

TEST_F(AiStaffLootTest, SetMovTableHelpersPopulateWorkingMovTable) {
  constexpr uint32_t kBaseTableAddr = 0x02010000u;
  GbaPtr<int8_t> base(kBaseTableAddr);
  for (int i = 0; i <= 0x40; ++i) {
    base[i] = (i % 2 == 0) ? -1 : 2;
  }

  AiSetMovTableAllPassable(GbaPtr<const int8_t>(kBaseTableAddr));
  GbaPtr<const int8_t> dst(kWorkingMovTableAddr);
  EXPECT_EQ(dst[1], 2);
  EXPECT_EQ(dst[2], 1);

  AiSetMovTableAllowOneTerrain(GbaPtr<const int8_t>(kBaseTableAddr), 0x1E);
  EXPECT_EQ(dst[0x1E], 1);
  EXPECT_EQ(dst[0x1C], -1);

  AiSetMovTableAllowTwoTerrains(GbaPtr<const int8_t>(kBaseTableAddr), 0x1B,
                                0x33);
  EXPECT_EQ(dst[0x1B], 1);
  EXPECT_EQ(dst[0x33], 1);
}

TEST_F(AiStaffLootTest, UpdateIgnoreSpecialItemsFlagTogglesBit1) {
  GbaPtr<Unit> unit(0x02011000u);
  unit->ai_config = 0x2000u;
  AiUpdateIgnoreSpecialItemsFlag(GbaPtr<const Unit>(unit.addr));
  EXPECT_EQ(*GbaPtr<uint8_t>(kAiStateAddr + 0x7Bu) & 2u, 2u);

  unit->ai_config = 0x0000u;
  AiUpdateIgnoreSpecialItemsFlag(GbaPtr<const Unit>(unit.addr));
  EXPECT_EQ(*GbaPtr<uint8_t>(kAiStateAddr + 0x7Bu) & 2u, 0u);
}

TEST_F(AiStaffLootTest, GetSpecialItemTableIndexMatchesEntry) {
  // Seed a 1-entry special item table at kAiSpecialItemTableAddr.
  *GbaPtr<uint16_t>(kAiSpecialItemTableAddr) = 0x69u;
  *GbaPtr<uint32_t>(kAiSpecialItemTableAddr + 4u) = 0x0803BAA9u;
  *GbaPtr<uint16_t>(kAiSpecialItemTableAddr + 8u) = 0u;

  EXPECT_EQ(AiGetSpecialItemTableIndex(0x0569u), 0);
  EXPECT_EQ(AiGetSpecialItemTableIndex(0x0501u), -1);
}

}  // namespace
}  // namespace fe7
