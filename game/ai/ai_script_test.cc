#include "game/ai/ai_script.h"

#include <array>
#include <cstdint>
#include <cstring>

#include "absl/base/nullability.h"
#include "game/ai/ai.h"
#include "game/ai/ai_decide.h"
#include "game/item.h"
#include "game/item/item.h"
#include "game/map/map.h"
#include "game/map/trap.h"
#include "game/phase/phase.h"
#include "game/unit.h"
#include "game/unit/unit.h"
#include "gtest/gtest.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

alignas(16) std::array<uint8_t, 0x40000u> gTestEwram{};
alignas(16) std::array<uint8_t, 0x8000u> gTestIwram{};
alignas(16) std::array<uint8_t, 0x01000000u> gTestRom{};

constexpr uint32_t kTestPInfoBase = 0x08001000u;
constexpr uint32_t kTestJInfoBase = 0x08002000u;
constexpr uint32_t kTestMoveTableAddr = 0x08003000u;
constexpr uint32_t kTestScriptTableAAddr = 0x08003400u;
constexpr uint32_t kTestScriptTableBAddr = 0x08003440u;
constexpr uint32_t kTestScriptA0Addr = 0x08003500u;
constexpr uint32_t kTestScriptB0Addr = 0x08003600u;
constexpr uint32_t kTestPidListAddr = 0x08003700u;
constexpr uint32_t kTestCustomCbAddr = 0x08003800u;

constexpr uint32_t kMapBufTerrain = 0x02010000u;
constexpr uint32_t kMapBufUnit = 0x02011000u;
constexpr uint32_t kMapBufMovement = 0x02012000u;
constexpr uint32_t kMapBufRange = 0x02013000u;
constexpr uint32_t kMapBufOther = 0x02014000u;
constexpr uint32_t kMapBufHidden = 0x02015000u;
constexpr uint32_t kScratchAddr = 0x02020000u;

uint32_t gCustomCbLastArg = 0u;
uint32_t TestScriptCallback(uint32_t r0, uint32_t, uint32_t, uint32_t,
                            const uint32_t* absl_nullable) {
  gCustomCbLastArg = r0;
  return 0u;
}

class AiScriptTest : public ::testing::Test {
 protected:
  void SetUp() override {
    gTestEwram.fill(0);
    gTestIwram.fill(0);
    gTestRom.fill(0);
    gMem.ewram = gTestEwram.data();
    gMem.iwram = gTestIwram.data();
    gMem.rom = gTestRom.data();
    gMem.rom_size = gTestRom.size();

    ResetDispatchTable();
    RegisterUnitFunctions();
    RegisterItemFunctions();
    RegisterMapFunctions();
    RegisterTrapFunctions();
    RegisterPhaseFunctions();
    RegisterAiFunctions();
    RegisterAiDecideFunctions();
    RegisterAiScriptFunctions();
    RegisterNative(kTestCustomCbAddr, &TestScriptCallback);
    gCustomCbLastArg = 0u;

    auto size = GbaPtr<MapSize>(kMapSizeAddr);
    size->x = 10;
    size->y = 10;
    MapInit(GbaPtr<uint8_t>(kMapBufTerrain),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr), 10, 10);
    MapInit(GbaPtr<uint8_t>(kMapBufUnit),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr), 10, 10);
    MapInit(GbaPtr<uint8_t>(kMapBufMovement),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr), 10, 10);
    MapInit(GbaPtr<uint8_t>(kMapBufRange),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapRangeAddr), 10, 10);
    MapInit(GbaPtr<uint8_t>(kMapBufOther),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kAiMapOtherAddr), 10, 10);
    MapInit(GbaPtr<uint8_t>(kMapBufHidden),
            GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapHiddenAddr), 10, 10);

    MapFill(*GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr), 1);
    MapFillEdges(*GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr), 0);

    auto mov_table = GbaPtr<int8_t>(kTestMoveTableAddr);
    for (int i = 0; i < 65; ++i) {
      mov_table[i] = 1;
    }
    mov_table[0] = -1;

    auto lut = GbaPtr<uint32_t>(kUnitLutAddr);
    for (int i = 0; i < 10; ++i) {
      lut[1 + i] = 0x0202BD50u + static_cast<uint32_t>(i) * sizeof(Unit);
      lut[0x41 + i] = 0x0202DDD0u + static_cast<uint32_t>(i) * sizeof(Unit);
      lut[0x81 + i] = 0x0202CEB8u + static_cast<uint32_t>(i) * sizeof(Unit);
    }

    // Steal priority table: {0x71 (113), 0x72 (114), 0x63 (99), 0xFFFF}.
    auto steal = GbaPtr<uint16_t>(kAiStealPriorityListAddr);
    steal[0] = 0x71u;
    steal[1] = 0x72u;
    steal[2] = 0x63u;
    steal[3] = 0xFFFFu;

    // Script opcode handler table for opcodes 0..3.
    auto ops = GbaPtr<uint32_t>(kAiScriptOpcodeTableAddr);
    ops[0] = kAddrAiScriptCmdConditionalGoto | 1u;
    ops[1] = kAddrAiScriptCmdCallFunc | 1u;
    ops[2] = kAddrAiScriptCmdChangeAi | 1u;
    ops[3] = kAddrAiScriptCmdGoto | 1u;

    *GbaPtr<uint32_t>(kAiScriptTableAPtrAddr) = kTestScriptTableAAddr;
    *GbaPtr<uint32_t>(kAiScriptTableBPtrAddr) = kTestScriptTableBAddr;
    auto tbl_a = GbaPtr<uint32_t>(kTestScriptTableAAddr);
    auto tbl_b = GbaPtr<uint32_t>(kTestScriptTableBAddr);
    tbl_a[0] = kTestScriptA0Addr;
    tbl_b[0] = kTestScriptB0Addr;

    // Seed items: 1=Iron Sword, 0x4A=Heal Staff, 0x68=Chest Key, 0x6A=Lockpick.
    SetupItem(1, kItemAttrWeapon, 0, 5, 46, 0x11);
    SetupItem(0x4A, kItemAttrStaff, 4, 0, 30, 0x11);
    SetupItem(0x68, 0u, 9, 0, 1, 0x00);
    SetupItem(0x6A, 0u, 9, 0, 15, 0x00);
  }

  void SetupItem(int id, uint32_t attrs, uint8_t kind, uint8_t might,
                 uint8_t max_uses, uint8_t encoded_range) {
    GbaPtr<IInfo> info(kIInfoTableAddr +
                       static_cast<uint32_t>(id) * sizeof(IInfo));
    info->id = static_cast<uint8_t>(id);
    info->kind = kind;
    info->attributes = attrs;
    info->max_uses = max_uses;
    info->might = might;
    info->encoded_range = encoded_range;
    info->required_wexp = 1;
  }

  GbaPtr<Unit> MakeUnit(int uid, uint8_t pid, uint8_t jid, int8_t x, int8_t y,
                        int8_t mov, uint32_t attrs = 0u) {
    GbaPtr<PInfo> p(kTestPInfoBase +
                    static_cast<uint32_t>(uid) * sizeof(PInfo));
    p->id = pid;
    GbaPtr<JInfo> j(kTestJInfoBase +
                    static_cast<uint32_t>(uid) * sizeof(JInfo));
    j->id = jid;
    j->base_mov = mov;
    j->attributes = attrs;
    j->mov_table[0] = GbaPtr<const int8_t>(kTestMoveTableAddr);
    j->mov_table[1] = GbaPtr<const int8_t>(kTestMoveTableAddr);
    j->mov_table[2] = GbaPtr<const int8_t>(kTestMoveTableAddr);

    GbaPtr<Unit> u = GetUnit(uid);
    std::memset(u.get(), 0, sizeof(Unit));
    u->pinfo = GbaPtr<const PInfo>(p.addr);
    u->jinfo = GbaPtr<const JInfo>(j.addr);
    u->id = static_cast<int8_t>(uid);
    u->level = 5;
    u->x = x;
    u->y = y;
    u->max_hp = 20;
    u->hp = 20;
    u->pow = 8;
    u->wexp[0] = 100;
    u->wexp[4] = 100;
    (*GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr))[y][x] =
        static_cast<uint8_t>(uid);
    return u;
  }

  void SetActiveUnit(GbaPtr<Unit> u) {
    *GbaPtr<GbaPtr<Unit>>(kAiActiveUnitAddr) = u;
    *GbaPtr<uint8_t>(kAiActiveUnitIdAddr) = static_cast<uint8_t>(u->id);
  }
};

TEST_F(AiScriptTest, StealPriorityChestKeyAndLootableTerrain) {
  EXPECT_EQ(AiGetStealItemPriority(0x71u), 0);
  EXPECT_EQ(AiGetStealItemPriority(0x63u), 2);
  EXPECT_EQ(AiGetStealItemPriority(0x01u), -1);

  auto target = MakeUnit(1, 1, 1, 2, 2, 5);
  target->items[0] = static_cast<uint16_t>((1u << 8) | 0x63u);  // prio 2
  target->items[1] = static_cast<uint16_t>((1u << 8) | 0x71u);  // prio 0
  EXPECT_EQ(AiGetBestStealableItemSlot(GbaPtr<const Unit>(target.addr)), 1);

  // Thief with Lockpick (0x6A) can open chests (terrain 0x21).
  auto thief = MakeUnit(0x81, 10, 5, 4, 4, 6, 4u);
  thief->items[0] = static_cast<uint16_t>((15u << 8) | 0x6Au);
  SetActiveUnit(thief);

  GbaPtr<uint8_t> slot_out(kScratchAddr);
  EXPECT_TRUE(AiFindChestKeySlot(slot_out));
  EXPECT_EQ(*slot_out, 0u);

  auto terrain = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr);
  terrain[3][3] = 0x21u;  // Chest
  terrain[3][4] = 0x03u;  // Village
  EXPECT_TRUE(AiIsSpecialLootableTerrain(3, 3));
  EXPECT_TRUE(AiIsSpecialLootableTerrain(4, 3));
  EXPECT_FALSE(AiIsSpecialLootableTerrain(5, 5));
}

TEST_F(AiScriptTest, SafeLocationClosestValidPosAndManhattan) {
  EXPECT_TRUE(AiIsWithinManhattanDistance(2, 3, 5, 4, 4));
  EXPECT_FALSE(AiIsWithinManhattanDistance(2, 3, 5, 4, 3));

  auto u = MakeUnit(0x81, 10, 1, 4, 4, 4);
  SetActiveUnit(u);
  auto other = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kAiMapOtherAddr);
  MapFill(other, 20);
  other[4][5] = 2;  // safest reachable tile

  GbaPtr<AiPos> out_pos(kScratchAddr);
  EXPECT_TRUE(
      AiFindSafestReachableLocation(GbaPtr<const Unit>(u.addr), out_pos));
  EXPECT_EQ(out_pos->x, 5);
  EXPECT_EQ(out_pos->y, 4);

  // Closest valid position when (4, 4) is occupied by `u`.
  MapFill(other, 0);
  EXPECT_TRUE(
      AiGetUnitClosestValidPosition(GbaPtr<const Unit>(u.addr), 4, 4, out_pos));
  EXPECT_TRUE(out_pos->x != 4 || out_pos->y != 4);
}

TEST_F(AiScriptTest, PidAliveBlueFlagsRecoveryAndStaffCheck) {
  auto blue = MakeUnit(1, 7, 1, 2, 2, 6);
  blue->items[0] = static_cast<uint16_t>((30u << 8) | 0x4Au);  // Heal Staff
  EXPECT_TRUE(AiIsUnitAliveByPid(7));
  EXPECT_FALSE(AiIsUnitAliveByPid(99));
  EXPECT_TRUE(AiUnitHasUsableStaffOrMagic(GbaPtr<const Unit>(blue.addr)));

  AiRefreshAllBlueUnitFlags();
  EXPECT_EQ(*GbaPtr<uint8_t>(kAiStateAddr + 0x85u), 6u);

  auto enemy = MakeUnit(0x81, 10, 1, 5, 5, 5);
  enemy->ai_flags = 1u;
  SetActiveUnit(enemy);
  *GbaPtr<uint8_t>(kPhasePlayStatusAddr + 0x0Fu) = kFactionRed;
  EXPECT_EQ(AiCountAlliesInRecoveryMode(), 1);

  auto range = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapRangeAddr);
  MapFill(range, 0);
  range[5][5] = 1;
  EXPECT_EQ(AiCountAlliesInRangeNeedingRecovery(), 1);
}

TEST_F(AiScriptTest, ScriptInterpreterAndTargetPredicates) {
  auto active = MakeUnit(0x81, 10, 3, 5, 5, 5);
  auto enemy = MakeUnit(1, 2, 4, 2, 2, 5);
  auto ally = MakeUnit(0x82, 11, 3, 6, 5, 5);
  SetActiveUnit(active);

  // Build AI1 script 0:
  // [0] cmd=0x00 (conditional goto label 1 if *kScratchAddr == 7)
  // [1] cmd=0x01 (call TestScriptCallback(0x1234))
  // [2] cmd=0x1B (label 1)
  // [3] cmd=0x02 (change AI: ai_a=2, ai_b=0xFF)
  // [4] cmd=0x03 (goto label 0)
  auto script = GbaPtr<AiScriptCmd>(kTestScriptA0Addr);
  script[0] = {0x00, 2, 0, 1, 7u, kScratchAddr, 0u};
  script[1] = {0x01, 0, 0, 0, 0u, kTestCustomCbAddr | 1u, 0x1234u};
  script[2] = {0x1B, 0, 0, 1, 0u, 0u, 0u};
  script[3] = {0x02, 2, 0xFF, 0, 0u, 0u, 0u};
  script[4] = {0x03, 0, 0, 0, 0u, 0u, 0u};

  // Condition false -> advances pc from 0 to 1.
  *GbaPtr<uint8_t>(kScratchAddr) = 3;
  EXPECT_EQ(AiTryExecScriptA(), 0);
  EXPECT_EQ(active->ai_a_pc, 1u);

  // Step 1: calls TestScriptCallback(0x1234), advances pc to 2.
  EXPECT_EQ(AiTryExecScriptA(), 0);
  EXPECT_EQ(gCustomCbLastArg, 0x1234u);
  EXPECT_EQ(active->ai_a_pc, 2u);

  // Reset pc to 0 with condition true -> jumps past label 1 to pc = 3.
  active->ai_a_pc = 0u;
  *GbaPtr<uint8_t>(kScratchAddr) = 7;
  EXPECT_EQ(AiTryExecScriptA(), 0);
  EXPECT_EQ(active->ai_a_pc, 3u);

  // Step 3: ChangeAi sets ai_a = 2, ai_a_pc = 0.
  EXPECT_EQ(AiTryExecScriptA(), 1);
  EXPECT_EQ(active->ai_a, 2u);
  EXPECT_EQ(active->ai_a_pc, 0u);

  // Target predicates.
  EXPECT_TRUE(AiIsUnitEnemy(GbaPtr<const Unit>(enemy.addr)));
  EXPECT_FALSE(AiIsUnitEnemy(GbaPtr<const Unit>(ally.addr)));
  EXPECT_TRUE(AiIsUnitNotActive(GbaPtr<const Unit>(enemy.addr)));
  EXPECT_FALSE(AiIsUnitNotActive(GbaPtr<const Unit>(active.addr)));

  auto pids = GbaPtr<uint16_t>(kTestPidListAddr);
  pids[0] = 2u;
  pids[1] = 0u;
  AiScriptCmd pred_cmd{};
  pred_cmd.u32_arg4 = 2u;
  pred_cmd.u32_arg8 = kTestPidListAddr;
  *GbaPtr<AiScriptCmd>(kScratchAddr + 0x10u) = pred_cmd;
  *GbaPtr<GbaPtr<const AiScriptCmd>>(kAiScriptCurAddr) =
      GbaPtr<const AiScriptCmd>(kScratchAddr + 0x10u);

  EXPECT_FALSE(AiIsUnitEnemyNotInPidList(GbaPtr<const Unit>(enemy.addr)));
  EXPECT_TRUE(AiIsUnitEnemyOrInPidList(GbaPtr<const Unit>(enemy.addr)));
  EXPECT_TRUE(AiIsUnitEnemyWithPid(GbaPtr<const Unit>(enemy.addr)));

  pred_cmd.u32_arg4 = 4u;
  *GbaPtr<AiScriptCmd>(kScratchAddr + 0x10u) = pred_cmd;
  EXPECT_TRUE(AiIsUnitEnemyWithJid(GbaPtr<const Unit>(enemy.addr)));
}

}  // namespace
}  // namespace fe7
