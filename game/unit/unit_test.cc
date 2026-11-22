#include "game/unit/unit.h"

#include <array>
#include <cstdint>

#include "absl/base/nullability.h"
#include "game/unit.h"
#include "gtest/gtest.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

uint32_t MockEquippedWeapon(uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/,
                            uint32_t /*r3*/,
                            const uint32_t* absl_nullable /*stack*/) {
  return 0x1E01u;
}

uint32_t MockItemBonus2(uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/,
                        uint32_t /*r3*/,
                        const uint32_t* absl_nullable /*stack*/) {
  return 2u;
}

uint32_t MockItemBonus0(uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/,
                        uint32_t /*r3*/,
                        const uint32_t* absl_nullable /*stack*/) {
  return 0u;
}

class UnitTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ewram_.fill(0);
    iwram_.fill(0);
    gMem.ewram = ewram_.data();
    gMem.iwram = iwram_.data();
    gMem.rom = nullptr;
    gMem.rom_size = 0;
    ResetDispatchTable();
    RegisterUnitFunctions();
    RegisterNative(kAddrGetUnitEquippedWeapon, &MockEquippedWeapon);
    RegisterNative(kAddrGetItemHpBonus, &MockItemBonus2);
    RegisterNative(kAddrGetItemPowBonus, &MockItemBonus2);
    RegisterNative(kAddrGetItemSklBonus, &MockItemBonus2);
    RegisterNative(kAddrGetItemSpdBonus, &MockItemBonus2);
    RegisterNative(kAddrGetItemDefBonus, &MockItemBonus0);
    RegisterNative(kAddrGetItemResBonus, &MockItemBonus0);
    RegisterNative(kAddrGetItemLckBonus, &MockItemBonus2);
  }

  void TearDown() override {
    ResetDispatchTable();
    gMem = GbaMemoryMap{};
  }

  alignas(16) std::array<uint8_t, 0x40000> ewram_;
  alignas(16) std::array<uint8_t, 0x8000> iwram_;
};

TEST_F(UnitTest, ClearAndCopyPreserveDeploymentId) {
  GbaPtr<Unit> a(0x02001000u);
  GbaPtr<Unit> b(0x02001080u);
  a->id = 1;
  a->hp = 20;
  a->pow = 11;
  b->id = 2;

  CopyUnit(GbaPtr<const Unit>(a.addr), b);
  EXPECT_EQ(b->id, 2);
  EXPECT_EQ(b->hp, 20);
  EXPECT_EQ(b->pow, 11);

  ClearUnit(b);
  EXPECT_EQ(b->id, 2);
  EXPECT_EQ(b->hp, 0);
  EXPECT_EQ(b->pow, 0);
}

TEST_F(UnitTest, StatGettersApplyBonusesRescuingAndBarrier) {
  GbaPtr<Unit> u(0x02001100u);
  u->max_hp = 25;
  u->hp = 30;
  u->pow = 12;
  u->skl = 11;
  u->spd = 13;
  u->def = 8;
  u->res = 6;
  u->lck = 9;
  u->barrier = 4;

  EXPECT_EQ(GetUnitMaxHp(GbaPtr<const Unit>(u.addr)), 27);
  EXPECT_EQ(GetUnitCurrentHp(u), 27);
  EXPECT_EQ(u->hp, 27);
  EXPECT_EQ(GetUnitPower(GbaPtr<const Unit>(u.addr)), 14);
  EXPECT_EQ(GetUnitSkill(GbaPtr<const Unit>(u.addr)), 13);
  EXPECT_EQ(GetUnitSpeed(GbaPtr<const Unit>(u.addr)), 15);
  EXPECT_EQ(GetUnitDefense(GbaPtr<const Unit>(u.addr)), 8);
  EXPECT_EQ(GetUnitResistance(GbaPtr<const Unit>(u.addr)), 10);
  EXPECT_EQ(GetUnitLuck(GbaPtr<const Unit>(u.addr)), 11);
  EXPECT_EQ(GetUnitMagRange(GbaPtr<const Unit>(u.addr)), 7);

  // Rescuing halves base Skl and Spd before adding weapon bonus.
  u->flags |= kUnitFlagRescuing;
  EXPECT_EQ(GetUnitSkill(GbaPtr<const Unit>(u.addr)), 5 + 2);
  EXPECT_EQ(GetUnitSpeed(GbaPtr<const Unit>(u.addr)), 6 + 2);

  AddUnitHp(u, -100);
  EXPECT_EQ(u->hp, 0);
  SetUnitHp(u, 40);
  EXPECT_EQ(u->hp, 27);
}

TEST_F(UnitTest, AidVisionStatusItemsMagicAndPortraits) {
  GbaPtr<PInfo> p(0x02001200u);
  GbaPtr<JInfo> j(0x02001280u);
  GbaPtr<Unit> u(0x02001300u);
  u->pinfo = GbaPtr<const PInfo>(p.addr);
  u->jinfo = GbaPtr<const JInfo>(j.addr);

  p->bonus_con = 1;
  j->base_con = 6;
  u->bonus_con = 2;
  EXPECT_EQ(GetUnitAid(GbaPtr<const Unit>(u.addr)), 8);

  j->attributes = kUnitAttrMounted;
  EXPECT_EQ(GetUnitAid(GbaPtr<const Unit>(u.addr)), 16);
  p->attributes = kUnitAttrFemale | kUnitAttrThief;
  EXPECT_EQ(GetUnitAid(GbaPtr<const Unit>(u.addr)), 11);

  *GbaPtr<uint8_t>(kChapterVisionAddr) = 3u;
  u->torch = 2u;
  EXPECT_EQ(GetUnitVision(GbaPtr<const Unit>(u.addr)), 10);

  SetUnitStatus(u, static_cast<int>(UnitStatus::kSleep));
  EXPECT_EQ(u->status, 2u);
  EXPECT_EQ(u->status_duration, 5u);
  SetUnitStatus(u, 0);
  EXPECT_EQ(u->status, 0u);
  EXPECT_EQ(u->status_duration, 0u);
  SetUnitStatusExt(u, static_cast<int>(UnitStatus::kPoison), 3);
  EXPECT_EQ(u->status, 1u);
  EXPECT_EQ(u->status_duration, 3u);

  u->items[0] = 0x1001u;
  u->items[2] = 0x0502u;
  EXPECT_EQ(GetUnitItemCount(GbaPtr<const Unit>(u.addr)), 3);

  EXPECT_FALSE(UnitKnowsMagic(GbaPtr<const Unit>(u.addr)));
  u->wexp[5] = 1u;
  EXPECT_TRUE(UnitKnowsMagic(GbaPtr<const Unit>(u.addr)));

  j->fid = 0x0022u;
  EXPECT_EQ(GetUnitFid(GbaPtr<const Unit>(u.addr)), 0x0022);
  p->fid = 0x0011u;
  EXPECT_EQ(GetUnitFid(GbaPtr<const Unit>(u.addr)), 0x0011);
  p->chibi_id = 0x05u;
  EXPECT_EQ(GetUnitMiniFid(GbaPtr<const Unit>(u.addr)), 0x7F05);
}

TEST_F(UnitTest, JobAndPersonInfoTableOffsets) {
  EXPECT_FALSE(static_cast<bool>(GetJobInfo(0)));
  EXPECT_FALSE(static_cast<bool>(GetJobInfo(-1)));
  EXPECT_EQ(GetJobInfo(2).addr, kJInfoTableAddr + 2u * sizeof(JInfo));

  EXPECT_FALSE(static_cast<bool>(GetPersonInfo(0)));
  EXPECT_EQ(GetPersonInfo(3).addr, kPInfoTableAddr + 3u * sizeof(PInfo));
}

}  // namespace
}  // namespace fe7
