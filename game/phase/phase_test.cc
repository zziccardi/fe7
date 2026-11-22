// Unit tests for FE7 US turn flow, phase switching, faction queries, and gold.

#include "game/phase/phase.h"

#include <array>
#include <cstdint>
#include <cstring>

#include "game/unit.h"
#include "game/unit/unit.h"
#include "gtest/gtest.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

alignas(16) std::array<uint8_t, 0x01000000u> gSyntheticRom{};
int gSupportGainCalls = 0;

uint32_t MockProcessSupportGains(uint32_t, uint32_t, uint32_t, uint32_t,
                                 const uint32_t*) {
  ++gSupportGainCalls;
  return 0;
}

class PhaseTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ewram_.fill(0);
    iwram_.fill(0);
    gSyntheticRom.fill(0);
    gSupportGainCalls = 0;
    ResetDispatchTable();

    gMem = {};
    gMem.ewram = ewram_.data();
    gMem.iwram = iwram_.data();
    gMem.rom = gSyntheticRom.data();
    gMem.rom_size = gSyntheticRom.size();

    // Populate gUnitLookup in synthetic ROM (ids 1..3 -> EWRAM 0x0202BD50+).
    pinfo_addr_ = 0x08001000u;
    jinfo_addr_ = 0x08001100u;
    PInfo p{};
    p.id = 1;
    JInfo j{};
    std::memcpy(gSyntheticRom.data() + (pinfo_addr_ - 0x08000000u), &p,
                sizeof(p));
    std::memcpy(gSyntheticRom.data() + (jinfo_addr_ - 0x08000000u), &j,
                sizeof(j));

    for (uint32_t id = 1; id <= 3; ++id) {
      const uint32_t unit_addr = 0x0202BD50u + (id - 1u) * sizeof(Unit);
      const uint32_t table_off =
          (kUnitLutAddr - 0x08000000u) + id * sizeof(uint32_t);
      std::memcpy(gSyntheticRom.data() + table_off, &unit_addr,
                  sizeof(unit_addr));
    }
  }

  void TearDown() override {
    ResetDispatchTable();
    gMem = {};
  }

  void PlaceUnit(int id, uint32_t flags, uint8_t status) {
    GbaPtr<Unit> u = GetUnit(id);
    std::memset(u.get(), 0, sizeof(Unit));
    u->pinfo = GbaPtr<const PInfo>(pinfo_addr_);
    u->jinfo = GbaPtr<const JInfo>(jinfo_addr_);
    u->id = static_cast<int8_t>(id);
    u->flags = flags;
    u->status = status;
  }

  std::array<uint8_t, 0x40000u> ewram_{};
  std::array<uint8_t, 0x8000u> iwram_{};
  uint32_t pinfo_addr_ = 0;
  uint32_t jinfo_addr_ = 0;
};

TEST_F(PhaseTest, LockAndReleaseGameCounter) {
  EXPECT_EQ(GetGameLock(), 0u);
  LockGame();
  LockGame();
  EXPECT_EQ(GetGameLock(), 2u);
  ReleaseGame();
  EXPECT_EQ(GetGameLock(), 1u);
}

TEST_F(PhaseTest, SwitchPhasesRotatesBlueRedGreenAndIncrementsTurn) {
  RegisterNative(kAddrProcessSupportGains, MockProcessSupportGains);
  GbaPtr<uint8_t> faction(kPhasePlayStatusAddr + 0x0Fu);
  GbaPtr<uint16_t> turn(kPhasePlayStatusAddr + 0x10u);
  *faction = kFactionBlue;
  *turn = 1;

  SwitchPhases();
  EXPECT_EQ(*faction, kFactionRed);
  EXPECT_EQ(*turn, 1u);
  EXPECT_EQ(GetCurrentPhase(), kFactionRed);
  EXPECT_EQ(GetNonActiveFaction(), kFactionBlue);

  SwitchPhases();
  EXPECT_EQ(*faction, kFactionGreen);
  EXPECT_EQ(*turn, 1u);

  SwitchPhases();
  EXPECT_EQ(*faction, kFactionBlue);
  EXPECT_EQ(*turn, 2u);
  EXPECT_EQ(gSupportGainCalls, 1);

  // Turn count caps at 999.
  *faction = kFactionGreen;
  *turn = kMaxTurnCount;
  SwitchPhases();
  EXPECT_EQ(*turn, kMaxTurnCount);
  EXPECT_EQ(gSupportGainCalls, 2);
}

TEST_F(PhaseTest, GoldSetAndAddClampAtMax) {
  SetGold(5000);
  EXPECT_EQ(GetGold(), 5000);
  AddGold(2500);
  EXPECT_EQ(GetGold(), 7500);
  AddGold(2000000);
  EXPECT_EQ(GetGold(), kMaxGold);
  SetGold(1500000);
  EXPECT_EQ(GetGold(), kMaxGold);
}

TEST_F(PhaseTest, AllianceAndAbleUnitCounts) {
  EXPECT_TRUE(AreUnitIdsAllied(0x01, 0x41));
  EXPECT_FALSE(AreUnitIdsAllied(0x01, 0x81));
  EXPECT_TRUE(IsSameAllegiance(0x01, 0x02));
  EXPECT_FALSE(IsSameAllegiance(0x01, 0x41));

  PlaceUnit(1, 0u, 0u);     // Able
  PlaceUnit(2, 0x02u, 0u);  // Unselectable/moved
  PlaceUnit(3, 0u, 2u);     // Asleep
  EXPECT_EQ(GetPhaseAbleUnitCount(kFactionBlue), 1);
  EXPECT_EQ(CountUnitsInState(kFactionBlue, 0x02u), 2);
  EXPECT_EQ(CountUnitsByFaction(kFactionBlue), 3);

  ClearAllUnitsBit8();
  PlaceUnit(1, 0x0100u, 0u);
  ClearAllUnitsBit8();
  EXPECT_EQ(GetUnit(1)->flags, 0u);
}

}  // namespace
}  // namespace fe7
