#include "game/engine/proc.h"

#include <array>
#include <cstdint>

#include "absl/base/nullability.h"
#include "game/proc.h"
#include "gtest/gtest.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

int gCallbackCount = 0;
int gEndCallbackCount = 0;

uint32_t TestCallFn(uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/,
                    uint32_t /*r3*/, const uint32_t* absl_nullable /*stack*/) {
  ++gCallbackCount;
  return 0u;
}

uint32_t TestEndFn(uint32_t /*r0*/, uint32_t /*r1*/, uint32_t /*r2*/,
                   uint32_t /*r3*/, const uint32_t* absl_nullable /*stack*/) {
  ++gEndCallbackCount;
  return 0u;
}

class ProcTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ewram_.fill(0);
    iwram_.fill(0);
    rom_.fill(0);
    gMem.ewram = ewram_.data();
    gMem.iwram = iwram_.data();
    gMem.rom = rom_.data();
    gMem.rom_size = rom_.size();
    gCallbackCount = 0;
    gEndCallbackCount = 0;
    ResetDispatchTable();
    RegisterProcFunctions();
    InitProcs();
  }

  void TearDown() override {
    ResetDispatchTable();
    gMem = GbaMemoryMap{};
  }

  alignas(16) std::array<uint8_t, 0x40000> ewram_;
  alignas(16) std::array<uint8_t, 0x8000> iwram_;
  alignas(16) std::array<uint8_t, 0x2000> rom_;
};

TEST_F(ProcTest, SpawnSleepCallAndEndLifecycle) {
  constexpr uint32_t kCallAddr = 0x08001000u;
  constexpr uint32_t kEndAddr = 0x08001010u;
  RegisterNative(kCallAddr, &TestCallFn);
  RegisterNative(kEndAddr, &TestEndFn);

  // Place a 5-instruction ProcScr in EWRAM.
  GbaPtr<ProcScr> scr(0x02000400u);
  scr[0] = {static_cast<int16_t>(ProcCmd::kOnEnd), 0,
            GbaPtr<const void>(kEndAddr | 1u)};
  scr[1] = {static_cast<int16_t>(ProcCmd::kCall), 0,
            GbaPtr<const void>(kCallAddr | 1u)};
  scr[2] = {static_cast<int16_t>(ProcCmd::kSleep), 2, GbaPtr<const void>(0)};
  scr[3] = {static_cast<int16_t>(ProcCmd::kCall), 0,
            GbaPtr<const void>(kCallAddr | 1u)};
  scr[4] = {static_cast<int16_t>(ProcCmd::kEnd), 0, GbaPtr<const void>(0)};

  const GbaPtr<Proc> proc =
      SpawnProc(GbaPtr<const ProcScr>(scr.addr), kProcTree1);
  ASSERT_TRUE(static_cast<bool>(proc));
  EXPECT_EQ(gCallbackCount, 1);
  EXPECT_EQ(proc->proc_sleep_clock, 2);
  EXPECT_EQ(proc->proc_repeat_func, kAddrProcSleepRepeat | 1u);

  const GbaPtr<Proc> root = *GbaPtr<GbaPtr<Proc>>(kProcTreeRootsAddr + 4u);
  EXPECT_EQ(root, proc);

  // Tick 1: sleep_clock 2 -> 1.
  ProcRun(root);
  EXPECT_EQ(gCallbackCount, 1);
  EXPECT_EQ(proc->proc_sleep_clock, 1);

  // Tick 2: sleep_clock 1 -> 0 -> ProcBreak() clears repeat_func.
  ProcRun(root);
  EXPECT_EQ(gCallbackCount, 1);
  EXPECT_EQ(proc->proc_repeat_func, 0u);

  // Tick 3: resumes script, runs second kCall and kEnd.
  ProcRun(root);
  EXPECT_EQ(gCallbackCount, 2);
  EXPECT_EQ(gEndCallbackCount, 1);
  EXPECT_FALSE(static_cast<bool>(ProcFind(GbaPtr<const ProcScr>(scr.addr))));
}

TEST_F(ProcTest, BlockingChildLocksAndUnlocksParent) {
  GbaPtr<ProcScr> parent_scr(0x02000500u);
  parent_scr[0] = {static_cast<int16_t>(ProcCmd::kBlock), 0,
                   GbaPtr<const void>(0)};

  GbaPtr<ProcScr> child_scr(0x02000540u);
  child_scr[0] = {static_cast<int16_t>(ProcCmd::kBlock), 0,
                  GbaPtr<const void>(0)};

  const GbaPtr<Proc> parent =
      SpawnProc(GbaPtr<const ProcScr>(parent_scr.addr), kProcTree2);
  EXPECT_EQ(parent->proc_lock_cnt, 0u);

  const GbaPtr<Proc> child =
      SpawnProcLocking(GbaPtr<const ProcScr>(child_scr.addr), parent);
  ASSERT_TRUE(static_cast<bool>(child));
  EXPECT_EQ(parent->proc_lock_cnt, 1u);
  EXPECT_FALSE(static_cast<bool>(
      ProcFindNonBlocked(GbaPtr<const ProcScr>(parent_scr.addr))));

  ProcEnd(child);
  EXPECT_EQ(parent->proc_lock_cnt, 0u);
  EXPECT_EQ(ProcFindNonBlocked(GbaPtr<const ProcScr>(parent_scr.addr)), parent);
}

TEST_F(ProcTest, MarkLockReleaseAndGoto) {
  GbaPtr<ProcScr> scr(0x02000600u);
  scr[0] = {static_cast<int16_t>(ProcCmd::kMark), 7, GbaPtr<const void>(0)};
  scr[1] = {static_cast<int16_t>(ProcCmd::kBlock), 0, GbaPtr<const void>(0)};
  scr[2] = {static_cast<int16_t>(ProcCmd::kLabel), 42, GbaPtr<const void>(0)};
  scr[3] = {static_cast<int16_t>(ProcCmd::kEnd), 0, GbaPtr<const void>(0)};

  const GbaPtr<Proc> proc =
      SpawnProc(GbaPtr<const ProcScr>(scr.addr), kProcTree3);
  EXPECT_EQ(ProcFindMarked(7), proc);

  ProcLockEachMarked(7);
  EXPECT_EQ(proc->proc_lock_cnt, 1u);
  ProcReleaseEachMarked(7);
  EXPECT_EQ(proc->proc_lock_cnt, 0u);

  ProcGoto(proc, 42);
  EXPECT_EQ(proc->proc_script_pc, GbaPtr<const ProcScr>(scr.addr) + 2);
  ProcRun(proc);
  EXPECT_FALSE(static_cast<bool>(ProcFindMarked(7)));
}

}  // namespace
}  // namespace fe7
