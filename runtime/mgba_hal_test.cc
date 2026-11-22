#include <cstddef>
#include <cstdint>
#include <vector>

#include "gtest/gtest.h"
#include "runtime/dispatch.h"
#include "runtime/gba_hal.h"
#include "runtime/gba_ptr.h"
#include "runtime/rom_loader.h"

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

std::vector<uint8_t> BuildSyntheticThumbRom() {
  std::vector<uint8_t> rom(0x1000, 0x00);
  // Minimal GBA ROM header: ARM B 0x080000C0 at 0x08000000
  rom[0x00] = 0x2E;
  rom[0x01] = 0x00;
  rom[0x02] = 0x00;
  rom[0x03] = 0xEA;
  // Infinite loop at 0x080000C0 (B .)
  rom[0xC0] = 0xFE;
  rom[0xC1] = 0xFF;
  rom[0xC2] = 0xFF;
  rom[0xC3] = 0xEA;

  // 0x08000100: ThumbAdd(r0, r1) -> r0 + r1
  EmitThumb16(rom, 0x100, 0x1840);  // add r0, r0, r1
  EmitThumb16(rom, 0x102, 0x4770);  // bx lr

  // 0x08000110: ThumbCallNativeHelper(r0) -> NativeHelper(r0) + 1
  EmitThumb16(rom, 0x110, 0xB500);  // push {lr}
  EmitThumb16(rom, 0x112, 0xF000);  // bl 0x08000140 (part 1)
  EmitThumb16(rom, 0x114,
              0xF814);  // bl 0x08000140 (part 2: (0x140 - 0x116) / 2 = 0x15?
                        // 0x116+0x28=0x13E; wait: 0x140-0x116=0x2A -> 0x15)
  EmitThumb16(rom, 0x114, 0xF815);  // bl 0x08000140 (part 2)
  EmitThumb16(rom, 0x116, 0x3001);  // add r0, #1
  EmitThumb16(rom, 0x118, 0xBD00);  // pop {pc}

  // 0x08000120: ThumbMutateEwramStruct(GbaPtr<MockUnit> r0) -> r0->hp += 5;
  // return r0->hp;
  EmitThumb16(rom, 0x120, 0x6801);  // ldr r1, [r0, #0]
  EmitThumb16(rom, 0x122, 0x3105);  // add r1, #5
  EmitThumb16(rom, 0x124, 0x6001);  // str r1, [r0, #0]
  EmitThumb16(rom, 0x126, 0x1C08);  // mov r0, r1
  EmitThumb16(rom, 0x128, 0x4770);  // bx lr

  return rom;
}

uint32_t NativeDouble(uint32_t r0, uint32_t, uint32_t, uint32_t,
                      const uint32_t*) {
  return r0 * 2u;
}

uint32_t NativeAdd(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                   const uint32_t*) {
  return r0 + r1;
}

class MgbaHalTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ResetDispatchTable();
    auto rom = LoadSyntheticRom(BuildSyntheticThumbRom());
    ASSERT_TRUE(rom.ok()) << rom.status();
    ASSERT_TRUE(hal::Init(*rom));
  }

  void TearDown() override {
    hal::Shutdown();
    ResetDispatchTable();
  }
};

TEST_F(MgbaHalTest, NativeToFallbackAndEwramMutation) {
  EXPECT_EQ(GbaCall(0x08000101u, 19u, 23u), 42u);

  GbaPtr<MockUnit> unit(0x0202BE4Cu);
  unit->hp = 12u;
  unit->max_hp = 20u;
  const uint32_t new_hp = GbaCall(0x08000121u, unit.addr);
  EXPECT_EQ(new_hp, 17u);
  EXPECT_EQ(unit->hp, 17u);
  EXPECT_EQ(unit->max_hp, 20u);

  const auto report = GetFallbackReport();
  ASSERT_EQ(report.size(), 2u);
}

TEST_F(MgbaHalTest, ReentrantNativeToFallbackToNativeRoundTrip) {
  RegisterNative(0x08000141u, &NativeDouble);
  EXPECT_EQ(GbaCall(0x08000111u, 10u), 21u);
}

TEST_F(MgbaHalTest, DifferentialComparisonThumbVsNative) {
  for (uint32_t a : {0u, 1u, 17u, 0xFFFFFFFFu}) {
    for (uint32_t b : {0u, 2u, 25u, 0x12345678u}) {
      const uint32_t thumb_res = GbaCall(0x08000101u, a, b);
      const uint32_t native_res = NativeAdd(a, b, 0, 0, nullptr);
      EXPECT_EQ(thumb_res, native_res);
    }
  }
}

TEST_F(MgbaHalTest, SwiMmioAndLinkStubsWork) {
  EXPECT_EQ(hal::SwiDiv(42, 6), 7);
  EXPECT_EQ(hal::SwiSqrt(144u), 12u);

  hal::MmioWrite16(0x04000000u, 0x0403u);  // DISPCNT
  EXPECT_EQ(hal::MmioRead16(0x04000000u), 0x0403u);

  EXPECT_FALSE(hal::link::IsBonusDiskUnlocked());
  hal::link::SetBonusDiskUnlocked(true);
  EXPECT_TRUE(hal::link::IsBonusDiskUnlocked());
}

TEST_F(MgbaHalTest, StepFrameProducesAudioSamplesAndSaveQueryIsSafe) {
  hal::StepFrame(kKeyA | kKeyStart);
  std::vector<int16_t> audio(2048 * 2, 0);
  const size_t frames = hal::ReadAudioSamples(audio.data(), 2048);
  // One GBA frame (~16.74 ms) at 48 kHz produces ~800 stereo PCM frames.
  EXPECT_GT(frames, 0u);
  EXPECT_LE(frames, 2048u);

  // Synthetic ROM has no detected save chip yet; querying save size is safe.
  EXPECT_EQ(hal::CopySaveData(nullptr, 0), 0u);
}

}  // namespace
}  // namespace fe7
