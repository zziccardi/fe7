#include "game/ai/ai_cp.h"

#include <array>
#include <cstdint>

#include "absl/base/nullability.h"
#include "game/ai/ai.h"
#include "game/engine/proc.h"
#include "game/unit.h"
#include "game/unit/unit.h"
#include "gtest/gtest.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

alignas(16) std::array<uint8_t, 0x40000> gTestEwram{};
alignas(16) std::array<uint8_t, 0x8000> gTestIwram{};
alignas(16) std::array<uint8_t, 0x01000000> gTestRom{};

constexpr uint32_t kProcAddr = 0x02014000u;
constexpr uint32_t kPInfoBase = 0x08020000u;
constexpr uint32_t kJInfoBase = 0x08021000u;

int gStubCalls = 0;

uint32_t StubCountAndReturnZero(uint32_t, uint32_t, uint32_t, uint32_t,
                                const uint32_t* absl_nullable) {
  ++gStubCalls;
  return 0u;
}

uint32_t StubReturnOne(uint32_t, uint32_t, uint32_t, uint32_t,
                       const uint32_t* absl_nullable) {
  ++gStubCalls;
  return 1u;
}

class AiCpTest : public ::testing::Test {
 protected:
  void SetUp() override {
    gTestEwram.fill(0);
    gTestIwram.fill(0);
    gTestRom.fill(0);
    gStubCalls = 0;

    gMem.ewram = gTestEwram.data();
    gMem.iwram = gTestIwram.data();
    gMem.rom = gTestRom.data();
    gMem.rom_size = static_cast<uint32_t>(gTestRom.size());

    ResetDispatchTable();
    RegisterProcFunctions();
    RegisterAiFunctions();
    RegisterAiCpFunctions();

    *GbaPtr<uint32_t>(kUnitLutAddr + 0x81u * 4u) = 0x02013100u;
    GbaPtr<Unit> active = GetUnit(0x81);
    active->id = static_cast<int8_t>(0x81);
    active->x = 2;
    active->y = 2;
    active->pinfo = GbaPtr<const PInfo>(kPInfoBase);
    active->jinfo = GbaPtr<const JInfo>(kJInfoBase);
    *GbaPtr<uint32_t>(kAiActiveUnitAddr) = active.addr;
    *GbaPtr<uint8_t>(kAiActiveUnitIdAddr) = 0x81u;
  }

  void TearDown() override { ResetDispatchTable(); }
};

TEST_F(AiCpTest, DispatchActionSetsExpectedCallbackPointer) {
  GbaPtr<Proc> proc(kProcAddr);
  GbaPtr<AiDecision> dec(kAiDecisionAddr);

  dec->action_id = 5u;  // Staff
  CpPerformDispatchAction(proc);
  EXPECT_EQ(*GbaPtr<uint32_t>(proc.addr + 0x2Cu), kAddrCpPerformStaff | 1u);

  dec->action_id = 6u;  // Use item
  CpPerformDispatchAction(proc);
  EXPECT_EQ(*GbaPtr<uint32_t>(proc.addr + 0x2Cu), kAddrCpPerformUseItem | 1u);

  dec->action_id = 9u;  // Ride ballista
  CpPerformDispatchAction(proc);
  EXPECT_EQ(*GbaPtr<uint32_t>(proc.addr + 0x2Cu),
            kAddrCpPerformRideBallista | 1u);
}

TEST_F(AiCpTest, WaitEscapeClearsPinfoWhenNoMuActive) {
  RegisterNative(kAddrMuExistsActive, &StubReturnOne);
  EXPECT_FALSE(CpPerformWaitEscape(GbaPtr<Proc>(kProcAddr)));
  EXPECT_NE(GetUnit(0x81)->pinfo.addr, 0u);

  RegisterNative(kAddrMuExistsActive, &StubCountAndReturnZero);
  EXPECT_TRUE(CpPerformWaitEscape(GbaPtr<Proc>(kProcAddr)));
  EXPECT_EQ(GetUnit(0x81)->pinfo.addr, 0u);
}

TEST_F(AiCpTest, WaitStealWaitsUntilFrameFiveThenClearsTilemaps) {
  RegisterNative(kAddrTmFill, &StubCountAndReturnZero);
  RegisterNative(kAddrEnableBgSync, &StubCountAndReturnZero);

  GbaPtr<Proc> proc(kProcAddr);
  *GbaPtr<uint8_t>(proc.addr + 0x30u) = 4u;
  EXPECT_FALSE(CpPerformWaitSteal(proc));
  EXPECT_EQ(gStubCalls, 0);

  *GbaPtr<uint8_t>(proc.addr + 0x30u) = 5u;
  EXPECT_TRUE(CpPerformWaitSteal(proc));
  EXPECT_EQ(gStubCalls, 3);
}

TEST_F(AiCpTest, ActionNopAndWaitDoneAlwaysReturnTrue) {
  GbaPtr<Proc> proc(kProcAddr);
  EXPECT_TRUE(CpPerformActionNop(proc));
  EXPECT_TRUE(CpPerformWaitDone(proc));
}

}  // namespace
}  // namespace fe7
