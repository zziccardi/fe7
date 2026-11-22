#include "runtime/diff_harness.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "runtime/dispatch.h"
#include "runtime/gba_hal.h"
#include "runtime/gba_ptr.h"
#include "runtime/rom_loader.h"
#include "runtime/trace_recorder.h"

namespace fe7 {
namespace {

struct MockUnit {
  uint32_t hp;
  uint32_t max_hp;
};

void EmitThumb16(std::vector<uint8_t>& rom, size_t offset, uint16_t insn) {
  rom[offset + 0] = static_cast<uint8_t>(insn & 0xFFu);
  rom[offset + 1] = static_cast<uint8_t>((insn >> 8) & 0xFFu);
}

std::vector<uint8_t> BuildSyntheticRom() {
  std::vector<uint8_t> rom(0x1000, 0x00);
  // ARM B 0x080000C0 at 0x08000000; infinite loop at 0x080000C0.
  rom[0x00] = 0x2E;
  rom[0x03] = 0xEA;
  rom[0xC0] = 0xFE;
  rom[0xC1] = 0xFF;
  rom[0xC2] = 0xFF;
  rom[0xC3] = 0xEA;

  // 0x08000100: ThumbAdd(r0, r1) -> r0 + r1
  EmitThumb16(rom, 0x100, 0x1840);  // add r0, r0, r1
  EmitThumb16(rom, 0x102, 0x4770);  // bx lr

  // 0x08000120: ThumbMutateHp(GbaPtr<MockUnit> r0) -> r0->hp += 5; return hp;
  EmitThumb16(rom, 0x120, 0x6801);  // ldr r1, [r0, #0]
  EmitThumb16(rom, 0x122, 0x3105);  // add r1, #5
  EmitThumb16(rom, 0x124, 0x6001);  // str r1, [r0, #0]
  EmitThumb16(rom, 0x126, 0x1C08);  // mov r0, r1
  EmitThumb16(rom, 0x128, 0x4770);  // bx lr
  return rom;
}

uint32_t NativeAddOk(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                     const uint32_t*) {
  return r0 + r1;
}

uint32_t NativeAddBuggy(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                        const uint32_t*) {
  return r0 + r1 + 1u;
}

uint32_t NativeMutateHpOk(uint32_t r0, uint32_t, uint32_t, uint32_t,
                          const uint32_t*) {
  GbaPtr<MockUnit> unit(r0);
  unit->hp += 5u;
  return unit->hp;
}

uint32_t NativeMutateHpWrongEwram(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                  const uint32_t*) {
  GbaPtr<MockUnit> unit(r0);
  unit->hp += 5u;
  unit->max_hp = 999u;  // Unintended EWRAM side effect!
  return unit->hp;
}

class DiffHarnessTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ResetDispatchTable();
    ResetTraceRecorder();
    auto rom = LoadSyntheticRom(BuildSyntheticRom());
    ASSERT_TRUE(rom.ok()) << rom.status();
    ASSERT_TRUE(hal::Init(*rom));
  }

  void TearDown() override {
    ResetTraceRecorder();
    hal::Shutdown();
    ResetDispatchTable();
  }
};

TEST_F(DiffHarnessTest, FuzzMatchesAndDetectsR0Mismatch) {
  const DiffResult ok_res = DiffFunction(0x08000101u, &NativeAddOk, {}, 32);
  EXPECT_TRUE(ok_res.ok) << ok_res.failure_detail;
  EXPECT_EQ(ok_res.fuzz_checked, 32u);

  const DiffResult bad_res = DiffFunction(0x08000101u, &NativeAddBuggy, {}, 8);
  EXPECT_FALSE(bad_res.ok);
  EXPECT_NE(bad_res.failure_detail.find("r0 mismatch"), std::string::npos);
}

TEST_F(DiffHarnessTest, TraceRoundTripAndEwramDiffDetection) {
  const uint32_t target = 0x08000120u;
  ConfigureTraceRecorder({&target, 1}, 4);

  GbaPtr<MockUnit> unit(0x0202BE4Cu);
  unit->hp = 10u;
  unit->max_hp = 20u;
  MaybeRecordTrace(target, unit.addr, 0, 0, 0, nullptr);
  ASSERT_EQ(GetRecordedTraces().size(), 1u);

  const std::string trace_path = ::testing::TempDir() + "/diff_test.fe7t";
  ASSERT_TRUE(WriteTraceFile(trace_path, GetRecordedTraces()).ok());
  auto loaded = ReadTraceFile(trace_path);
  std::remove(trace_path.c_str());
  ASSERT_TRUE(loaded.ok()) << loaded.status();
  ASSERT_EQ(loaded->size(), 1u);

  const DiffResult ok_res =
      DiffFunction(target, &NativeMutateHpOk, *loaded, /*fuzz_iters=*/0);
  EXPECT_TRUE(ok_res.ok) << ok_res.failure_detail;
  EXPECT_EQ(ok_res.traces_checked, 1u);

  const DiffResult bad_res = DiffFunction(target, &NativeMutateHpWrongEwram,
                                          *loaded, /*fuzz_iters=*/0);
  EXPECT_FALSE(bad_res.ok);
  EXPECT_NE(bad_res.failure_detail.find("EWRAM mismatch"), std::string::npos);
  EXPECT_NE(bad_res.failure_detail.find("0x0202BE50"), std::string::npos);
}

}  // namespace
}  // namespace fe7
