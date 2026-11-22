#include "game/map/ballista.h"

#include <array>
#include <cstdint>

#include "absl/base/nullability.h"
#include "game/item.h"
#include "game/item/item.h"
#include "game/map/trap.h"
#include "game/phase/phase.h"
#include "game/unit.h"
#include "gtest/gtest.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

alignas(16) std::array<uint8_t, 0x40000> gTestEwram{};
alignas(16) std::array<uint8_t, 0x8000> gTestIwram{};
alignas(16) std::array<uint8_t, 0x01000000> gTestRom{};

int gRefreshCalls = 0;

uint32_t StubRefreshUnitSprites(uint32_t, uint32_t, uint32_t, uint32_t,
                                const uint32_t* absl_nullable) {
  ++gRefreshCalls;
  return 0u;
}

class BallistaTest : public ::testing::Test {
 protected:
  void SetUp() override {
    gTestEwram.fill(0);
    gTestIwram.fill(0);
    gTestRom.fill(0);
    gRefreshCalls = 0;

    gMem.ewram = gTestEwram.data();
    gMem.iwram = gTestIwram.data();
    gMem.rom = gTestRom.data();
    gMem.rom_size = static_cast<uint32_t>(gTestRom.size());

    ResetDispatchTable();
    RegisterItemFunctions();
    RegisterTrapFunctions();
    RegisterBallistaFunctions();
    RegisterNative(kAddrRefreshUnitSprites, &StubRefreshUnitSprites);

    InitTraps();

    // Populate Iron Ballista (IID 0x35) with 5 max uses in synthetic ROM.
    IInfo ballista_info{};
    ballista_info.id = 0x35u;
    ballista_info.max_uses = 5u;
    *GbaPtr<IInfo>(kIInfoTableAddr + 0x35u * sizeof(IInfo)) = ballista_info;
  }

  void TearDown() override { ResetDispatchTable(); }
};

TEST_F(BallistaTest, AddAndQueryBallistaTrap) {
  GbaPtr<Trap> trap = AddBallista(4, 6, 0x35);
  ASSERT_TRUE(static_cast<bool>(trap));
  EXPECT_TRUE(IsBallistaTrap(GbaPtr<const Trap>(trap.addr)));
  EXPECT_EQ(GetBallistaAt(4, 6).addr, trap.addr);
  EXPECT_EQ(GetBallistaItemAtPos(4, 6), 0x0535);
  EXPECT_EQ(GetBallistaItemFromTrap(GbaPtr<const Trap>(trap.addr)), 0x0535);
  EXPECT_EQ(GetBallistaItemAtIgnoreUses(4, 6), 0x0135);

  // Empty ammo hides ballista from GetBallistaAt/GetBallistaItemAtPos, but
  // GetBallistaItemAtIgnoreUses still reports it.
  trap->data[2] = 0u;
  EXPECT_FALSE(static_cast<bool>(GetBallistaAt(4, 6)));
  EXPECT_EQ(GetBallistaItemAtPos(4, 6), 0);
  EXPECT_EQ(GetBallistaItemAtIgnoreUses(4, 6), 0x0135);
}

TEST_F(BallistaTest, RideAndRemoveUnitFromBallista) {
  GbaPtr<Trap> trap = AddBallista(3, 5, 0x35);
  GbaPtr<Unit> unit(0x02010000u);
  unit->x = 3;
  unit->y = 5;
  unit->flags = 0u;

  UnitRideBallista(unit);
  EXPECT_NE(unit->flags & kUnitFlagInBallista, 0u);
  EXPECT_EQ(trap->data[1], 1u);
  EXPECT_EQ(unit->ballista_trap, 0u);
  EXPECT_EQ(gRefreshCalls, 1);

  unit->x = 4;
  unit->y = 5;
  TryRemoveUnitFromBallista(unit);
  EXPECT_EQ(unit->flags & kUnitFlagInBallista, 0u);
  EXPECT_EQ(trap->data[1], 0u);
  EXPECT_EQ(trap->x, 4u);
  EXPECT_EQ(trap->y, 5u);
  EXPECT_EQ(gRefreshCalls, 2);
}

}  // namespace
}  // namespace fe7
