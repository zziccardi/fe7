// Unit tests for FE7 US support relationships, EXP growth, and bonuses.

#include "game/support/support.h"

#include <array>
#include <cstdint>
#include <cstring>

#include "game/unit.h"
#include "game/unit/unit.h"
#include "gtest/gtest.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

alignas(16) std::array<uint8_t, 0x01000000u> gSyntheticRom{};

class SupportTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ewram_.fill(0);
    iwram_.fill(0);
    gSyntheticRom.fill(0);
    gMem = {};
    gMem.ewram = ewram_.data();
    gMem.iwram = iwram_.data();
    gMem.rom = gSyntheticRom.data();
    gMem.rom_size = gSyntheticRom.size();

    // Support EXP caps table at 0x08B94184: {80, 160, 240, 241}.
    constexpr uint32_t kCaps[4] = {80u, 160u, 240u, 241u};
    std::memcpy(gSyntheticRom.data() + (kSupportLevelCapsAddr - 0x08000000u),
                kCaps, sizeof(kCaps));

    // Affinity table at 0x08C9A1C0: Fire (1) and Thunder (2).
    const SupportBonuses aff[3] = {
        {1, 1, 0, 5, 5, 5, 0, 0},
        {2, 0, 1, 0, 5, 5, 5, 0},
        {0, 0, 0, 0, 0, 0, 0, 0},
    };
    std::memcpy(gSyntheticRom.data() + (kAffinityBonusTableAddr - 0x08000000u),
                aff, sizeof(aff));

    // Two paired characters (PID 1 and PID 2) with mutual SupportInfo.
    const uint32_t sup_a_addr = 0x08002000u;
    const uint32_t sup_b_addr = 0x08002020u;
    SupportInfo sup_a{};
    sup_a.pids[0] = 2;
    sup_a.exp_base[0] = 10;
    sup_a.exp_growth[0] = 5;
    sup_a.count = 1;
    SupportInfo sup_b{};
    sup_b.pids[0] = 1;
    sup_b.exp_base[0] = 10;
    sup_b.exp_growth[0] = 5;
    sup_b.count = 1;
    std::memcpy(gSyntheticRom.data() + (sup_a_addr - 0x08000000u), &sup_a,
                sizeof(sup_a));
    std::memcpy(gSyntheticRom.data() + (sup_b_addr - 0x08000000u), &sup_b,
                sizeof(sup_b));

    PInfo pa{};
    pa.id = 1;
    pa.affinity = 1;
    pa.support_info = GbaPtr<const SupportInfo>(sup_a_addr);
    PInfo pb{};
    pb.id = 2;
    pb.affinity = 2;
    pb.support_info = GbaPtr<const SupportInfo>(sup_b_addr);
    SetPersonEntry(1, pa);
    SetPersonEntry(2, pb);

    // Populate gUnitLookup for blue slots 1 and 2.
    for (uint32_t id = 1; id <= 2; ++id) {
      const uint32_t uaddr = 0x0202BD50u + (id - 1u) * sizeof(Unit);
      std::memcpy(gSyntheticRom.data() + (kUnitLutAddr - 0x08000000u) +
                      id * sizeof(uint32_t),
                  &uaddr, sizeof(uaddr));
    }

    GbaPtr<Unit> ua = GetUnit(1);
    ua->pinfo = GetPersonInfo(1);
    ua->id = 1;
    ua->x = 4;
    ua->y = 4;
    GbaPtr<Unit> ub = GetUnit(2);
    ub->pinfo = GetPersonInfo(2);
    ub->id = 2;
    ub->x = 5;
    ub->y = 4;
  }

  void TearDown() override { gMem = {}; }

  void SetPersonEntry(int pid, const PInfo& info) {
    const uint32_t off = (kPInfoTableAddr - 0x08000000u) +
                         static_cast<uint32_t>(pid) * sizeof(PInfo);
    std::memcpy(gSyntheticRom.data() + off, &info, sizeof(info));
  }

  std::array<uint8_t, 0x40000u> ewram_{};
  std::array<uint8_t, 0x8000u> iwram_{};
};

TEST_F(SupportTest, ProcessSupportGainsAndLevelUnlock) {
  *GbaPtr<uint16_t>(kSupportPlayStatusAddr + 0x10u) = 2;  // Turn 2
  GbaPtr<Unit> ua = GetUnit(1);
  GbaPtr<Unit> ub = GetUnit(2);
  ua->supports[0] = 78;
  ub->supports[0] = 78;

  ProcessSupportGains();
  // Clamped at C-rank cap 80.
  EXPECT_EQ(ua->supports[0], 80u);
  EXPECT_EQ(ub->supports[0], 80u);
  EXPECT_TRUE(CanUnitSupportNow(GbaPtr<const Unit>(ua.addr), 0));

  UnitGainSupportLevel(ua, 0);
  EXPECT_EQ(ua->supports[0], 81u);
  EXPECT_EQ(GetSupportLevelBySupportIndex(GbaPtr<const Unit>(ua.addr), 0), 1);
  EXPECT_TRUE(HasUnitGainedSupportLevel(GbaPtr<const Unit>(ua.addr), 0));
  EXPECT_TRUE(HasUnitGainedSupportLevel(GbaPtr<const Unit>(ub.addr), 0));
  EXPECT_FALSE(CanUnitSupportNow(GbaPtr<const Unit>(ua.addr), 0));
}

TEST_F(SupportTest, GetUnitSupportBonusesWithinThreeTiles) {
  GbaPtr<Unit> ua = GetUnit(1);
  GbaPtr<Unit> ub = GetUnit(2);
  ua->supports[0] = 161;  // B-rank (level 2)
  ub->supports[0] = 161;  // B-rank (level 2)

  GbaPtr<SupportBonuses> bonuses(0x02010000u);
  const int mask = GetUnitSupportBonuses(GbaPtr<const Unit>(ua.addr), bonuses);
  EXPECT_EQ(mask, 1);
  // Fire(1,0,5,5,5,0)*2 + Thunder(0,1,0,5,5,5)*2 = (2,2,10,20,20,10) >> 1
  EXPECT_EQ(bonuses->bonus_attack, 1u);
  EXPECT_EQ(bonuses->bonus_defense, 1u);
  EXPECT_EQ(bonuses->bonus_hit, 5u);
  EXPECT_EQ(bonuses->bonus_avoid, 10u);
  EXPECT_EQ(bonuses->bonus_crit, 10u);
  EXPECT_EQ(bonuses->bonus_dodge, 5u);

  // Move partner out of range (4 tiles away) -> zero bonuses.
  ub->x = 8;
  EXPECT_EQ(GetUnitSupportBonuses(GbaPtr<const Unit>(ua.addr), bonuses), 0);
  EXPECT_EQ(bonuses->bonus_attack, 0u);
}

TEST_F(SupportTest, IconsUiCharsAndClearSupports) {
  EXPECT_EQ(GetCharacterAffinityIcon(1), 0x7A);
  EXPECT_EQ(GetSupportLevelUiChar(0), 0x14u);
  EXPECT_EQ(GetSupportLevelUiChar(3), 0x19u);

  GbaPtr<Unit> ua = GetUnit(1);
  GbaPtr<Unit> ub = GetUnit(2);
  ua->supports[0] = 241;
  ub->supports[0] = 241;
  EXPECT_TRUE(ArePidsAtMaxSupport(1, 2));

  ClearUnitSupports(ua);
  EXPECT_EQ(ua->supports[0], 0u);
  EXPECT_EQ(ub->supports[0], 0u);
}

}  // namespace
}  // namespace fe7
