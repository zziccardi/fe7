#include <array>
#include <cstdint>

#include "game/proc.h"
#include "game/unit.h"
#include "gtest/gtest.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

struct TestSubProc {
  Proc base;
};

TEST(CanonicalHeadersTest, ProcAndUnitStructsWorkWithGbaPtr) {
  alignas(16) std::array<uint8_t, 0x40000> ewram{};
  alignas(16) std::array<uint8_t, 0x1000> rom{};
  gMem.ewram = ewram.data();
  gMem.iwram = nullptr;
  gMem.rom = rom.data();
  gMem.rom_size = rom.size();

  GbaPtr<Proc> root(0x02024E20u);
  GbaPtr<Proc> child(0x02024E8Cu);
  root->proc_child = child;
  root->proc_lock_cnt = 1;
  child->proc_parent = root;
  EXPECT_EQ(child->proc_parent->proc_lock_cnt, 1u);
  EXPECT_EQ(ProcAs<TestSubProc>(root).addr, root.addr);

  GbaPtr<PInfo> pinfo(0x02001000u);
  GbaPtr<JInfo> jinfo(0x02001100u);
  pinfo->attributes = kUnitAttrLord | kUnitAttrFemale;
  jinfo->attributes = kUnitAttrMounted;

  GbaPtr<Unit> unit(0x0202BE4Cu);
  unit->pinfo = GbaPtr<const PInfo>(pinfo.addr);
  unit->jinfo = GbaPtr<const JInfo>(jinfo.addr);
  EXPECT_EQ(UnitAttributes(*unit),
            kUnitAttrLord | kUnitAttrFemale | kUnitAttrMounted);

  gMem = GbaMemoryMap{};
}

}  // namespace
}  // namespace fe7
