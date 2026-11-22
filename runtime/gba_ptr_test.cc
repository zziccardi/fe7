#include "runtime/gba_ptr.h"

#include <array>
#include <cstdint>

#include "gtest/gtest.h"

namespace fe7 {
namespace {

struct MockUnit {
  uint32_t id;
  int8_t hp;
  int8_t max_hp;
  uint16_t flags;
};

struct MockBaseProc {
  uint32_t script_addr;
};

struct MockSubProc {
  uint32_t script_addr;
  int32_t timer;
};

class GbaPtrTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ewram_.fill(0);
    iwram_.fill(0);
    rom_.fill(0);
    gMem.ewram = ewram_.data();
    gMem.iwram = iwram_.data();
    gMem.rom = rom_.data();
    gMem.rom_size = rom_.size();
  }

  void TearDown() override { gMem = GbaMemoryMap{}; }

  alignas(16) std::array<uint8_t, 0x40000> ewram_;
  alignas(16) std::array<uint8_t, 0x8000> iwram_;
  alignas(16) std::array<uint8_t, 0x1000> rom_;
};

TEST_F(GbaPtrTest, ResolvesEwramIwramAndRom) {
  GbaPtr<MockUnit> unit(0x0202BE4Cu);
  EXPECT_TRUE(static_cast<bool>(unit));
  unit->id = 0x12345678u;
  unit->hp = 18;
  unit->max_hp = 20;
  unit->flags = 0x0003u;

  EXPECT_EQ(unit->id, 0x12345678u);
  EXPECT_EQ((*unit).hp, 18);

  GbaPtr<uint32_t> iwram_val(0x03000000u);
  *iwram_val = 0xCAFEBABEu;
  EXPECT_EQ(*iwram_val, 0xCAFEBABEu);

  rom_[0x100] = 0x42;
  GbaPtr<const uint8_t> rom_byte(0x08000100u);
  EXPECT_EQ(*rom_byte, 0x42);
}

TEST_F(GbaPtrTest, PointerArithmeticAndSpan) {
  GbaPtr<uint32_t> arr(0x02001000u);
  arr[0] = 10;
  arr[1] = 20;
  arr[2] = 30;

  EXPECT_EQ(*(arr + 1), 20u);
  EXPECT_EQ((arr + 2).addr, 0x02001008u);

  GbaSpan<uint32_t> span(arr, 3);
  EXPECT_EQ(span.size(), 3u);
  EXPECT_FALSE(span.empty());
  EXPECT_EQ(span[2], 30u);
}

TEST_F(GbaPtrTest, ProcAsCastsAddressPreservingType) {
  GbaPtr<MockBaseProc> base(0x02024E20u);
  GbaPtr<MockSubProc> sub = ProcAs<MockSubProc>(base);
  EXPECT_EQ(sub.addr, base.addr);
  sub->timer = 99;
  EXPECT_EQ(sub->timer, 99);
}

#ifndef NDEBUG
TEST_F(GbaPtrTest, DeathOnNullUnalignedOutOfBoundsAndMmio) {
  GbaPtr<uint32_t> null_ptr;
  EXPECT_FALSE(static_cast<bool>(null_ptr));
  EXPECT_DEATH({ (void)null_ptr.get(); }, "Null GBA pointer");

  GbaPtr<uint32_t> unaligned(0x02000002u);
  EXPECT_DEATH({ (void)unaligned.get(); }, "Unaligned GBA pointer");

  GbaPtr<uint32_t> oob_ewram(0x02040000u);
  EXPECT_DEATH({ (void)oob_ewram.get(); }, "EWRAM out of bounds");

  GbaPtr<uint32_t> oob_iwram(0x03008000u);
  EXPECT_DEATH({ (void)oob_iwram.get(); }, "IWRAM out of bounds");

  GbaPtr<uint32_t> oob_rom(0x08001000u);
  EXPECT_DEATH({ (void)oob_rom.get(); }, "ROM out of bounds");

  GbaPtr<uint16_t> mmio(0x04000000u);
  EXPECT_DEATH({ (void)mmio.get(); }, "hal::Mmio");
}
#endif  // NDEBUG

}  // namespace
}  // namespace fe7
