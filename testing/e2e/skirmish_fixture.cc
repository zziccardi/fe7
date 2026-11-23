#include "testing/e2e/skirmish_fixture.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>

#include "absl/base/nullability.h"
#include "game/ai/ai.h"
#include "game/ai/ai_actions.h"
#include "game/ai/ai_combat.h"
#include "game/ai/ai_cp.h"
#include "game/ai/ai_decide.h"
#include "game/ai/ai_opcodes.h"
#include "game/ai/ai_script.h"
#include "game/ai/ai_staff_loot.h"
#include "game/battle.h"
#include "game/battle/battle.h"
#include "game/battle/battle_exp.h"
#include "game/battle/battle_sim.h"
#include "game/item.h"
#include "game/item/item.h"
#include "game/map/map.h"
#include "game/map/trap.h"
#include "game/phase/phase.h"
#include "game/proc.h"
#include "game/register_all.h"
#include "game/support/support.h"
#include "game/unit.h"
#include "game/unit/unit.h"
#include "gtest/gtest.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7::e2e {
namespace {

struct WeaponTriangleEntry {
  int8_t attacker_kind;
  int8_t defender_kind;
  int8_t hit_bonus;
  int8_t dmg_bonus;
};

struct FriendZoneCombatEntry {
  int8_t dx = 0;
  int8_t dy = 0;
  int8_t weight = 0;
  uint8_t pad = 0;
};

struct FriendZonePosEntry {
  int16_t dx = 0;
  int16_t dy = 0;
};

// No-op stub for graphics/VRAM seams out of scope for headless logic tests
// (e.g., `RefreshUnitSprites` 0x08025724).
uint32_t StubNoOpZero(uint32_t, uint32_t, uint32_t, uint32_t,
                      const uint32_t* absl_nullable) {
  return 0u;
}

}  // namespace

void SkirmishIntegrationTest::SetUp() {
  ewram_.assign(0x40000u, 0u);
  iwram_.assign(0x8000u, 0u);
  rom_.assign(kRomSize, 0u);

  gMem.ewram = ewram_.data();
  gMem.iwram = iwram_.data();
  gMem.rom = rom_.data();
  gMem.rom_size = rom_.size();

  ResetDispatchTable();
  RegisterAllNativeFunctions();
  RegisterNative(kAddrRefreshUnitSprites, StubNoOpZero);

  InitRomTables();
  InitMaps();
  InitPlayState();
  InitRoster();
  SyncUnitMap();
}

void SkirmishIntegrationTest::TearDown() {
  const auto report = GetFallbackReport();
  if (!report.empty()) {
    std::ostringstream oss;
    oss << "Unexpected ARM7TDMI fallback call(s) during E2E test:";
    for (const auto& [addr, hits] : report) {
      oss << "\n  0x" << std::hex << std::uppercase << std::setw(8)
          << std::setfill('0') << addr << std::dec << " (" << hits
          << " hit(s))";
    }
    ADD_FAILURE() << oss.str();
  }
  ResetDispatchTable();
  gMem = {};
}

GbaPtr<Unit> SkirmishIntegrationTest::Lyn() const {
  return GetUnit(kUnitIdLyn);
}

GbaPtr<Unit> SkirmishIntegrationTest::Kent() const {
  return GetUnit(kUnitIdKent);
}

GbaPtr<Unit> SkirmishIntegrationTest::GreenAlly() const {
  return GetUnit(kUnitIdGreenAlly);
}

GbaPtr<Unit> SkirmishIntegrationTest::BrigandA() const {
  return GetUnit(kUnitIdBrigandA);
}

GbaPtr<Unit> SkirmishIntegrationTest::BrigandB() const {
  return GetUnit(kUnitIdBrigandB);
}

GbaPtr<Unit> SkirmishIntegrationTest::BrigandC() const {
  return GetUnit(kUnitIdBrigandC);
}

void SkirmishIntegrationTest::ConfigureUnit(int unit_id,
                                            const UnitSetupOptions& opts) {
  GbaPtr<Unit> u = GetUnit(unit_id);
  ClearUnit(u);
  u->id = static_cast<int8_t>(unit_id);
  u->pinfo = GetPersonInfo(opts.pid);
  u->jinfo = GetJobInfo(opts.jid);
  u->level = opts.level;
  u->exp = opts.exp;
  u->x = opts.x;
  u->y = opts.y;
  u->max_hp = opts.hp;
  u->hp = opts.hp;
  u->pow = opts.pow;
  u->skl = opts.skl;
  u->spd = opts.spd;
  u->def = opts.def;
  u->res = opts.res;
  u->lck = opts.lck;
  u->wexp[static_cast<int>(opts.weapon_kind)] = opts.initial_wexp;
  if (opts.weapon_iid != 0u) {
    u->items[0] = static_cast<uint16_t>(CreateItem(opts.weapon_iid));
  }
  u->supports[0] = opts.support0_exp;
}

void SkirmishIntegrationTest::PlaceUnit(GbaPtr<Unit> unit, int x, int y,
                                        bool end_turn) {
  unit->x = static_cast<int8_t>(x);
  unit->y = static_cast<int8_t>(y);
  if (end_turn) {
    unit->flags |= kUnitFlagTurnEnded;
  }
  SyncUnitMap();
}

void SkirmishIntegrationTest::SetWeaponExp(GbaPtr<Unit> unit, ItemKind kind,
                                           uint8_t wexp) {
  unit->wexp[static_cast<int>(kind)] = wexp;
}

void SkirmishIntegrationTest::MarkUnitForRecovery(GbaPtr<Unit> unit) {
  unit->ai_flags |= kAiFlagRecovery;
}

void SkirmishIntegrationTest::SyncUnitMap() {
  GbaPtr<GbaPtr<uint8_t>> umap = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr);
  for (int y = 0; y < kMapH; ++y) {
    for (int x = 0; x < kMapW; ++x) {
      umap[y][x] = 0u;
    }
  }
  for (int id = 1; id < 0xC0; ++id) {
    GbaPtr<Unit> u = GetUnit(id);
    if (!u || !u->pinfo || (u->flags & kUnitFlagsUnavailable) != 0u) {
      continue;
    }
    if (u->x >= 0 && u->x < kMapW && u->y >= 0 && u->y < kMapH) {
      umap[u->y][u->x] = static_cast<uint8_t>(u->id);
    }
  }
}

void SkirmishIntegrationTest::LoadTerrainFromAscii(
    const std::array<std::string_view, kMapH>& rows) {
  GbaPtr<GbaPtr<uint8_t>> terrain = TerrainMap();
  // Fill the entire buffer (including outer padding) with `kTerrainWall` so
  // BFS flooding stops cleanly at map boundaries without clamping.
  MapFill(terrain, kTerrainWall);
  for (int y = 0; y < kMapH; ++y) {
    ASSERT_EQ(rows[static_cast<size_t>(y)].size(), static_cast<size_t>(kMapW));
    for (int x = 0; x < kMapW; ++x) {
      const char ch = rows[static_cast<size_t>(y)][static_cast<size_t>(x)];
      switch (ch) {
        case '.':
          terrain[y][x] = kTerrainPlains;
          break;
        case '#':
          terrain[y][x] = kTerrainWall;
          break;
        case 'F':
          terrain[y][x] = kTerrainForest;
          break;
        case 'C':
          terrain[y][x] = kTerrainChest;
          break;
        default:
          ADD_FAILURE() << "Unknown terrain glyph '" << ch << "' at (" << x
                        << ", " << y << ")";
          terrain[y][x] = kTerrainPlains;
          break;
      }
    }
  }
}

std::string SkirmishIntegrationTest::FormatMapState() const {
  GbaPtr<GbaPtr<uint8_t>> terrain = TerrainMap();
  GbaPtr<GbaPtr<uint8_t>> umap = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr);
  std::ostringstream oss;
  oss << "  x: 0 1 2 3 4 5 6 7\n";
  for (int y = 0; y < kMapH; ++y) {
    oss << "y=" << y << ":";
    for (int x = 0; x < kMapW; ++x) {
      char glyph = '.';
      if (terrain[y][x] == kTerrainWall) {
        glyph = '#';
      } else if (terrain[y][x] == kTerrainForest) {
        glyph = 'F';
      }
      const uint8_t uid = umap[y][x];
      if (uid == kUnitIdLyn) {
        glyph = 'L';
      } else if (uid == kUnitIdKent) {
        glyph = 'K';
      } else if (uid == kUnitIdBrigandA) {
        glyph = 'A';
      } else if (uid == kUnitIdBrigandB) {
        glyph = 'B';
      } else if (uid != 0u) {
        glyph = (uid & 0x80u) != 0u ? 'E' : 'U';
      }
      oss << ' ' << glyph;
    }
    oss << '\n';
  }
  return oss.str();
}

GbaPtr<GbaPtr<uint8_t>> SkirmishIntegrationTest::TerrainMap() const {
  return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr);
}

GbaPtr<GbaPtr<uint8_t>> SkirmishIntegrationTest::MovementMap() const {
  return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr);
}

GbaPtr<GbaPtr<uint8_t>> SkirmishIntegrationTest::RangeMap() const {
  return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapRangeAddr);
}

GbaPtr<GbaPtr<uint8_t>> SkirmishIntegrationTest::FogMap() const {
  return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapFogAddr);
}

void SkirmishIntegrationTest::SetFogVision(uint8_t vision) {
  *GbaPtr<uint8_t>(kMapPlayStatusAddr + kPlayStatusVisionOffset) = vision;
}

uint8_t SkirmishIntegrationTest::GetPhaseFaction() const {
  return *GbaPtr<const uint8_t>(kPhasePlayStatusAddr +
                                kPlayStatusFactionOffset);
}

void SkirmishIntegrationTest::SetPhaseFaction(uint8_t faction) {
  *GbaPtr<uint8_t>(kPhasePlayStatusAddr + kPlayStatusFactionOffset) = faction;
}

uint16_t SkirmishIntegrationTest::GetTurnNumber() const {
  return *GbaPtr<const uint16_t>(kPhasePlayStatusAddr + kPlayStatusTurnOffset);
}

void SkirmishIntegrationTest::SetTurnNumber(uint16_t turn) {
  *GbaPtr<uint16_t>(kPhasePlayStatusAddr + kPlayStatusTurnOffset) = turn;
}

void SkirmishIntegrationTest::SetAutoEndTurn(bool enabled) {
  GbaPtr<uint8_t> config2(kPhasePlayStatusAddr + kPlayStatusConfig2Offset);
  if (enabled) {
    *config2 &= static_cast<uint8_t>(~0x40u);
  } else {
    *config2 |= 0x40u;
  }
}

void SkirmishIntegrationTest::SetTacticianState(bool enabled,
                                                uint8_t affinity_id,
                                                uint16_t raw_stars,
                                                uint8_t chapter_mode) {
  WriteRom<uint32_t>(kTacticianAffinityTableAddr, affinity_id);
  *GbaPtr<uint8_t>(kPhasePlayStatusAddr + kPlayStatusChapterModeOffset) =
      chapter_mode;
  *GbaPtr<uint8_t>(kPhasePlayStatusAddr + kPlayStatusTacticianByteOffset) =
      enabled ? 1u : 0u;
  *GbaPtr<uint16_t>(kPhasePlayStatusAddr + kPlayStatusTacticianStarsOffset) =
      static_cast<uint16_t>(raw_stars << 4);
}

SupportBonuses SkirmishIntegrationTest::ComputeSupportBonuses(
    GbaPtr<const Unit> unit) const {
  SupportBonuses out{};
  GetUnitSupportBonuses(unit,
                        GbaPtr<SupportBonuses>(kScratchSupportBonusesAddr));
  std::memcpy(&out, ewram_.data() + (kScratchSupportBonusesAddr - kEwramBase),
              sizeof(out));
  return out;
}

void SkirmishIntegrationTest::RunCombatExchange(GbaPtr<Unit> attacker,
                                                GbaPtr<Unit> defender,
                                                uint16_t config_flags) {
  GbaPtr<BattleStats> stats(kBattleStatsAddr);
  *stats = {};
  stats->config = config_flags;
  stats->range = 1;

  GbaPtr<BattleUnit> actor(kBattleActorAddr);
  GbaPtr<BattleUnit> target(kBattleTargetAddr);
  InitBattleUnit(actor, AsConst(attacker));
  InitBattleUnit(target, AsConst(defender));

  // Set terrain bonuses from gMapTerrain for both combatants.
  GbaPtr<GbaPtr<uint8_t>> terrain = TerrainMap();
  const uint8_t a_tid = terrain[attacker->y][attacker->x];
  const uint8_t d_tid = terrain[defender->y][defender->x];
  actor->terrain_id = a_tid;
  actor->terrain_avoid = attacker->jinfo->avo_terrain_table[a_tid];
  actor->terrain_defense = attacker->jinfo->def_terrain_table[a_tid];
  actor->terrain_resistance = attacker->jinfo->res_terrain_table[a_tid];
  target->terrain_id = d_tid;
  target->terrain_avoid = defender->jinfo->avo_terrain_table[d_tid];
  target->terrain_defense = defender->jinfo->def_terrain_table[d_tid];
  target->terrain_resistance = defender->jinfo->res_terrain_table[d_tid];

  // Equip weapons & check counterattack eligibility.
  actor->weapon_slot_index = 0;
  actor->weapon = attacker->items[0];
  actor->weapon_before = actor->weapon;
  actor->weapon_type = static_cast<uint8_t>(GetItemKind(actor->weapon));
  actor->weapon_attributes = GetItemAttributes(actor->weapon);
  actor->can_counter = 1;
  BattleUnitTargetSetEquippedWeapon(target);
  target->weapon = target->weapon_before;
  target->weapon_type = static_cast<uint8_t>(GetItemKind(target->weapon));
  target->weapon_attributes = GetItemAttributes(target->weapon);
  target->can_counter = 1;
  BattleInitTargetCanCounter();

  BattleApplyWeaponTriangleEffect(actor, target);
  ComputeBattleUnitStats(actor, target);
  ComputeBattleUnitStats(target, actor);
  ComputeBattleUnitEffectiveStats(actor, target);
  ComputeBattleUnitEffectiveStats(target, actor);

  BattleUnwind();

  if ((config_flags & kBattleConfigReal) != 0u) {
    BattleApplyExpGains();
    BattleApplyUnitUpdates();
    // Mark defeated units dead (mirroring FE7 post-battle cleanup).
    if (attacker->hp == 0) {
      attacker->flags |= kUnitFlagDead;
    }
    if (defender->hp == 0) {
      defender->flags |= kUnitFlagDead;
    }
    SyncUnitMap();
  }
}

void SkirmishIntegrationTest::SetActiveAiUnit(GbaPtr<Unit> unit) {
  *GbaPtr<GbaPtr<Unit>>(kAiActiveUnitAddr) = unit;
  *GbaPtr<uint8_t>(kAiActiveUnitIdAddr) = static_cast<uint8_t>(unit->id);
}

bool SkirmishIntegrationTest::IsDangerMapFilled() const {
  return *GbaPtr<const uint8_t>(kAiStateAddr + kAiStateDangerFilledOffset) !=
         0u;
}

void SkirmishIntegrationTest::SetDangerMapFilled(bool filled) {
  *GbaPtr<uint8_t>(kAiStateAddr + kAiStateDangerFilledOffset) =
      filled ? 1u : 0u;
}

void SkirmishIntegrationTest::ResetAiDecideState(uint8_t recovery_mode) {
  *GbaPtr<uint8_t>(kAiStateAddr + kAiStateDecideStepOffset) = 0u;
  *GbaPtr<uint8_t>(kAiStateAddr + kAiStateRecoveryModeOffset) = recovery_mode;
}

void SkirmishIntegrationTest::PrepareAiTurn(GbaPtr<Unit> unit,
                                            const PrepareAiTurnOptions& opts) {
  SetActiveAiUnit(unit);
  ResetAiDecideState(opts.recovery_mode);
  if (opts.clear_decision) {
    AiClearDecision();
  }
}

void SkirmishIntegrationTest::RecordAiDecision(const AiDecisionOptions& opts) {
  AiClearDecision();
  const uint8_t target_id =
      opts.target ? static_cast<uint8_t>(opts.target->id) : 0u;
  const uint8_t target_x =
      opts.target ? static_cast<uint8_t>(opts.target->x) : 0u;
  const uint8_t target_y =
      opts.target ? static_cast<uint8_t>(opts.target->y) : 0u;
  AiSetDecision(opts.x_move, opts.y_move, opts.action_id, target_id,
                opts.item_slot, target_x, target_y);
}

void SkirmishIntegrationTest::ExpectAiDecision(
    const AiDecisionExpectation& expected) const {
  SCOPED_TRACE(FormatMapState());
  GbaPtr<const AiDecision> dec(kAiDecisionAddr);
  EXPECT_EQ(dec->action_performed, 1);
  EXPECT_EQ(dec->action_id, expected.action_id);
  if (expected.target.has_value()) {
    EXPECT_EQ(dec->unit_target_id,
              static_cast<uint8_t>((*expected.target)->id));
  }
  if (expected.item_slot.has_value()) {
    EXPECT_EQ(dec->item_slot, *expected.item_slot);
  }
}

void SkirmishIntegrationTest::SetAiHealThreshold(uint8_t percent) {
  *GbaPtr<uint8_t>(kAiStateAddr + kAiStateHealThresholdOffset) = percent;
}

void SkirmishIntegrationTest::SetAiSpecialItemFlags(uint32_t flags) {
  *GbaPtr<uint32_t>(kAiStateAddr + kAiStateSpecialItemFlagsOffset) = flags;
}

bool SkirmishIntegrationTest::FindBestAiPosition(AiPos* absl_nonnull out) {
  GbaPtr<AiPos> scratch(kScratchBestPosAddr);
  *scratch = {};
  const bool found = AiFindBestPositionInRange(scratch);
  *out = *scratch;
  return found;
}

uint32_t SkirmishIntegrationTest::ScoreAiCombat(uint8_t x, uint8_t y,
                                                GbaPtr<const Unit> target,
                                                uint16_t item_slot) {
  GbaPtr<AiCombatSimulationSt> sim_st(kScratchCombatSimAddr);
  *sim_st = {};
  sim_st->x_move = x;
  sim_st->y_move = y;
  sim_st->target_id = static_cast<uint8_t>(target->id);
  sim_st->item_slot = item_slot;
  AiComputeCombatScore(sim_st);
  return sim_st->score;
}

uint32_t SkirmishIntegrationTest::GetProcDispatchedCallback(
    GbaPtr<Proc> proc) const {
  return *GbaPtr<const uint32_t>(proc.addr + kProcPayloadCallbackOffset);
}

void SkirmishIntegrationTest::RegisterItem(const ItemSpec& spec) {
  IInfo info{};
  info.id = spec.id;
  info.kind = static_cast<uint8_t>(spec.kind);
  info.attributes = spec.attributes;
  info.bonuses = GbaPtr<const ItemStatBonuses>(spec.bonuses_addr);
  info.max_uses = spec.max_uses;
  info.might = spec.might;
  info.hit = spec.hit;
  info.weight = spec.weight;
  info.crit = spec.crit;
  info.encoded_range = spec.encoded_range;
  info.required_wexp = spec.required_wexp;
  info.weapon_effect = spec.weapon_effect;
  info.wexp_gain = spec.wexp_gain;
  WriteRom(kIInfoTableAddr + static_cast<uint32_t>(spec.id) * sizeof(IInfo),
           info);
}

void SkirmishIntegrationTest::RegisterJob(const JobSpec& spec) {
  JInfo j{};
  j.id = spec.id;
  j.jid_promote = spec.promote_jid;
  j.base_con = spec.con;
  j.base_mov = spec.mov;
  j.max_hp = 60;
  j.max_pow = 20;
  j.max_skl = 20;
  j.max_spd = 20;
  j.max_def = 20;
  j.max_res = 20;
  j.max_con = 20;
  j.power_level = spec.power_level;
  j.promotion_hp = spec.promotion_hp;
  j.promotion_pow = spec.promotion_pow;
  j.promotion_skl = spec.promotion_skl;
  j.promotion_spd = spec.promotion_spd;
  j.promotion_def = spec.promotion_def;
  j.promotion_res = spec.promotion_res;
  j.attributes = spec.attributes;
  j.wexp[static_cast<int>(ItemKind::kSword)] = spec.base_wexp_sword;
  j.wexp[static_cast<int>(ItemKind::kLance)] = spec.base_wexp_lance;
  j.mov_table[0] = GbaPtr<const int8_t>(kMovTableFootAddr);
  j.avo_terrain_table = GbaPtr<const int8_t>(kTerrainAvoAddr);
  j.def_terrain_table = GbaPtr<const int8_t>(kTerrainDefAddr);
  j.res_terrain_table = GbaPtr<const int8_t>(kTerrainResAddr);
  WriteRom(kJInfoTableAddr + static_cast<uint32_t>(spec.id) * sizeof(JInfo), j);
}

void SkirmishIntegrationTest::RegisterChar(const CharSpec& spec) {
  PInfo p{};
  p.id = spec.id;
  p.jid_default = spec.default_jid;
  p.affinity = spec.affinity;
  p.growth_hp = spec.growth;
  p.growth_pow = spec.growth;
  p.growth_skl = spec.growth;
  p.growth_spd = spec.growth;
  p.growth_def = spec.growth;
  p.growth_res = spec.growth;
  p.growth_lck = spec.growth;
  p.attributes = spec.attributes;
  p.support_info = GbaPtr<const SupportInfo>(spec.support_info_addr);
  WriteRom(kPInfoTableAddr + static_cast<uint32_t>(spec.id) * sizeof(PInfo), p);
}

void SkirmishIntegrationTest::WriteSupportPartner(
    uint32_t rom_addr, const SupportPartnerSpec& spec) {
  SupportInfo sup{};
  sup.pids[0] = spec.partner_pid;
  sup.exp_base[0] = spec.exp_base;
  sup.exp_growth[0] = spec.exp_growth;
  sup.count = 1;
  WriteRom(rom_addr, sup);
}

void SkirmishIntegrationTest::WriteAiScriptCmd(uint32_t rom_addr,
                                               const AiScriptCmdSpec& spec) {
  AiScriptCmd cmd{};
  cmd.cmd = spec.cmd;
  cmd.arg1 = spec.arg1;
  cmd.arg2 = spec.arg2;
  cmd.arg3 = spec.arg3;
  WriteRom(rom_addr, cmd);
}

void SkirmishIntegrationTest::InitEnemyAiTurnRomTables() {
  // 3-stage `CpDecide` pipeline: Suspend/Recover -> Script A -> Script B -> 0.
  WriteRom<uint32_t>(kAiDecideFuncsAddr + 0u,
                     ThumbAddr(kAddrCpDecideSuspendOrRecover));
  WriteRom<uint32_t>(kAiDecideFuncsAddr + 4u, ThumbAddr(kAddrCpDecideScriptA));
  WriteRom<uint32_t>(kAiDecideFuncsAddr + 8u, ThumbAddr(kAddrCpDecideScriptB));
  WriteRom<uint32_t>(kAiDecideFuncsAddr + 12u, 0u);

  // Script A[0]: op Attack (100% chance) -> op Goto 0.
  WriteRom<uint32_t>(kAiScriptTableAPtrAddr, kScriptATableAddr);
  WriteRom<uint32_t>(kScriptATableAddr, kScriptA0Addr);
  WriteAiScriptCmd(kScriptA0Addr, {.cmd = kAiScriptOpAttack, .arg1 = 100u});
  WriteAiScriptCmd(kScriptA0Addr + sizeof(AiScriptCmd),
                   {.cmd = kAiScriptOpGoto, .arg3 = 0u});

  WriteRom<uint32_t>(
      kAiScriptOpcodeTableAddr + static_cast<uint32_t>(kAiScriptOpGoto) * 4u,
      ThumbAddr(kAddrAiScriptCmdGoto));
  WriteRom<uint32_t>(
      kAiScriptOpcodeTableAddr + static_cast<uint32_t>(kAiScriptOpAttack) * 4u,
      ThumbAddr(kAddrAiScriptCmdAttack));

  // Staff & recovery item definitions: Heal staff (1-1 range) and Vulnerary.
  RegisterItem({.id = kIidHealStaff,
                .kind = ItemKind::kStaff,
                .attributes = kItemAttrStaff,
                .encoded_range = kEncodedRange1To1,
                .required_wexp = kWexpERank});
  RegisterItem({.id = kIidVulnerary, .kind = ItemKind::kItem});

  WriteRom<uint32_t>(kAiStaffActionTableAddr + 0u, kIidHealStaff);
  WriteRom<uint32_t>(kAiStaffActionTableAddr + 4u,
                     ThumbAddr(kAddrAiTryHealStaff));
  WriteRom<uint32_t>(kAiStaffActionTableAddr + 8u, 0u);

  constexpr int32_t kCardOffsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  for (int i = 0; i < 4; ++i) {
    WriteRom<int32_t>(kAiAdjacentOffsetsAddr + static_cast<uint32_t>(i) * 8u,
                      kCardOffsets[i][0]);
    WriteRom<int32_t>(
        kAiAdjacentOffsetsAddr + static_cast<uint32_t>(i) * 8u + 4u,
        kCardOffsets[i][1]);
  }
}

void SkirmishIntegrationTest::InitThiefLootRomTables() {
  // Steal priority list: Vulnerary (0-terminated uint8_t list).
  const uint8_t steal_list[2] = {kIidVulnerary, 0u};
  std::memcpy(rom_.data() + (kAiStealPriorityListAddr - kRomBase), steal_list,
              sizeof(steal_list));

  // Chest terrain list (0-terminated) and passable foot cost for chests.
  const uint8_t chest_list[2] = {kTerrainChest, 0u};
  std::memcpy(rom_.data() + (kAiChestTerrainListAddr - kRomBase), chest_list,
              sizeof(chest_list));
  WriteRom<int8_t>(kMovTableFootAddr + kTerrainChest, int8_t{1});

  // Cardinal offsets (int8_t[4][2]) used by AiTryStealAction.
  const int8_t steal_offsets[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  std::memcpy(rom_.data() + (kAiCardinalOffsetsAddr - kRomBase), steal_offsets,
              sizeof(steal_offsets));

  // Register Thief class + Antitoxin/Lockpick items.
  RegisterJob({.id = kJidThief,
               .mov = 6,
               .con = 6,
               .power_level = 2,
               .attributes = kUnitAttrThief | kUnitAttrSteal});
  RegisterItem({.id = kIidAntitoxin, .kind = ItemKind::kItem, .max_uses = 3});
  RegisterItem({.id = kIidLockpick, .kind = ItemKind::kItem, .max_uses = 15});
}

void SkirmishIntegrationTest::InitRomTables() {
  // FE7 resolves 8-bit Unit::id -> EWRAM Unit* via a 256-entry ROM lookup
  // table (`gUnitLookup` at kUnitLutAddr). Populate the three active faction
  // pools at their exact retail EWRAM bases:
  //   - Blue  (Player): IDs 0x01..0x3E (62 slots) at 0x0202BD50
  //   - Green (Ally):   IDs 0x41..0x54 (20 slots) at 0x0202D970
  //   - Red   (Enemy):  IDs 0x81..0xB2 (50 slots) at 0x0202CEB8
  auto set_lut = [&](uint32_t first_id, uint32_t count, uint32_t base_ewram) {
    for (uint32_t i = 0; i < count; ++i) {
      const uint32_t id = first_id + i;
      const uint32_t unit_addr =
          base_ewram + i * static_cast<uint32_t>(sizeof(Unit));
      WriteRom(kUnitLutAddr + id * 4u, unit_addr);
    }
  };
  set_lut(0x01u, 62u, 0x0202BD50u);
  set_lut(0x41u, 20u, 0x0202D970u);
  set_lut(0x81u, 50u, 0x0202CEB8u);

  // Terrain combat tables (65 entries): Forest (2) grants +20 avo, +1 def.
  int8_t avo[65]{};
  int8_t def[65]{};
  int8_t res[65]{};
  avo[kTerrainForest] = 20;
  def[kTerrainForest] = 1;
  std::memcpy(rom_.data() + (kTerrainAvoAddr - kRomBase), avo, sizeof(avo));
  std::memcpy(rom_.data() + (kTerrainDefAddr - kRomBase), def, sizeof(def));
  std::memcpy(rom_.data() + (kTerrainResAddr - kRomBase), res, sizeof(res));

  // Foot movement costs: -1 marks impassable (Wall=0); Plains=1, Forest=2.
  int8_t mov[65];
  std::memset(mov, -1, sizeof(mov));
  mov[kTerrainPlains] = 1;
  mov[kTerrainForest] = 2;
  std::memcpy(rom_.data() + (kMovTableFootAddr - kRomBase), mov, sizeof(mov));

  // Weapon triangle rules (Sword > Axe > Lance > Sword), terminated by -1.
  const WeaponTriangleEntry wta[7] = {
      {.attacker_kind = static_cast<int8_t>(ItemKind::kSword),
       .defender_kind = static_cast<int8_t>(ItemKind::kAxe),
       .hit_bonus = 15,
       .dmg_bonus = 1},
      {.attacker_kind = static_cast<int8_t>(ItemKind::kAxe),
       .defender_kind = static_cast<int8_t>(ItemKind::kSword),
       .hit_bonus = -15,
       .dmg_bonus = -1},
      {.attacker_kind = static_cast<int8_t>(ItemKind::kLance),
       .defender_kind = static_cast<int8_t>(ItemKind::kSword),
       .hit_bonus = 15,
       .dmg_bonus = 1},
      {.attacker_kind = static_cast<int8_t>(ItemKind::kSword),
       .defender_kind = static_cast<int8_t>(ItemKind::kLance),
       .hit_bonus = -15,
       .dmg_bonus = -1},
      {.attacker_kind = static_cast<int8_t>(ItemKind::kAxe),
       .defender_kind = static_cast<int8_t>(ItemKind::kLance),
       .hit_bonus = 15,
       .dmg_bonus = 1},
      {.attacker_kind = static_cast<int8_t>(ItemKind::kLance),
       .defender_kind = static_cast<int8_t>(ItemKind::kAxe),
       .hit_bonus = -15,
       .dmg_bonus = -1},
      {.attacker_kind = -1, .defender_kind = 0, .hit_bonus = 0, .dmg_bonus = 0},
  };
  std::memcpy(rom_.data() + (kWeaponTriangleTableAddr - kRomBase), wta,
              sizeof(wta));

  // Support EXP thresholds (C=80, B=160, A=240) and Affinity combat bonuses.
  const uint32_t caps[4] = {80, 160, 240, 240};
  std::memcpy(rom_.data() + (kSupportLevelCapsAddr - kRomBase), caps,
              sizeof(caps));
  const SupportBonuses aff[3] = {
      {.affinity = 1,
       .bonus_attack = 1,
       .bonus_defense = 0,
       .bonus_hit = 5,
       .bonus_avoid = 5,
       .bonus_crit = 5,
       .bonus_dodge = 0,
       .pad_07 = 0},
      {.affinity = 2,
       .bonus_attack = 1,
       .bonus_defense = 0,
       .bonus_hit = 5,
       .bonus_avoid = 0,
       .bonus_crit = 5,
       .bonus_dodge = 5,
       .pad_07 = 0},
      {},
  };
  std::memcpy(rom_.data() + (kAffinityBonusTableAddr - kRomBase), aff,
              sizeof(aff));

  // Lyn <-> Kent symmetric support partner tables (growth = 5 pts/turn).
  WriteSupportPartner(kLynSupportInfoAddr, {.partner_pid = kPidKent});
  WriteSupportPartner(kKentSupportInfoAddr, {.partner_pid = kPidLyn});

  // Base weapons & Angelic Robe.
  RegisterItem({.id = kIidIronSword,
                .kind = ItemKind::kSword,
                .attributes = kItemAttrWeapon,
                .max_uses = 46,
                .might = 5,
                .hit = 90,
                .weight = 5,
                .encoded_range = kEncodedRange1To1,
                .required_wexp = kWexpERank,
                .wexp_gain = 1});
  RegisterItem({.id = kIidIronLance,
                .kind = ItemKind::kLance,
                .attributes = kItemAttrWeapon,
                .max_uses = 45,
                .might = 7,
                .hit = 80,
                .weight = 8,
                .encoded_range = kEncodedRange1To1,
                .required_wexp = kWexpERank,
                .wexp_gain = 1});
  RegisterItem({.id = kIidIronAxe,
                .kind = ItemKind::kAxe,
                .attributes = kItemAttrWeapon,
                .max_uses = 45,
                .might = 8,
                .hit = 75,
                .weight = 10,
                .encoded_range = kEncodedRange1To1,
                .required_wexp = kWexpERank,
                .wexp_gain = 1});
  RegisterItem({.id = kIidSteelAxe,
                .kind = ItemKind::kAxe,
                .attributes = kItemAttrWeapon,
                .max_uses = 30,
                .might = 11,
                .hit = 65,
                .weight = 15,
                .encoded_range = kEncodedRange1To1,
                .required_wexp = kWexpERank,
                .wexp_gain = 1});

  ItemStatBonuses robe_bonuses{};
  robe_bonuses.hp = 7;
  WriteRom(kAngelicRobeBonusAddr, robe_bonuses);
  RegisterItem({.id = kIidAngelicRobe,
                .kind = ItemKind::kItem,
                .bonuses_addr = kAngelicRobeBonusAddr,
                .max_uses = 1});

  // Base classes & characters.
  RegisterJob({.id = kJidLord, .mov = 5, .con = 5, .power_level = 3});
  RegisterJob({.id = kJidCavalier, .mov = 7, .con = 9, .power_level = 3});
  RegisterJob({.id = kJidBrigand, .mov = 5, .con = 12, .power_level = 2});

  RegisterChar({.id = kPidLyn,
                .default_jid = kJidLord,
                .affinity = 2,
                .support_info_addr = kLynSupportInfoAddr,
                .growth = 100});
  RegisterChar({.id = kPidKent,
                .default_jid = kJidCavalier,
                .affinity = 1,
                .support_info_addr = kKentSupportInfoAddr,
                .growth = 80});
  RegisterChar({.id = kPidBrigandA, .default_jid = kJidBrigand});
  RegisterChar({.id = kPidBrigandB, .default_jid = kJidBrigand});

  // AI combat score weights, class-rank groups, and friend-zone offset tables.
  WriteRom(kAiCombatScoreCoeffTableAddr, AiCombatScoreCoefficients{
                                             .coeff_damage_dealt = 2,
                                             .coeff_low_hp_opponent = 1,
                                             .coeff_friend_zone = 1,
                                             .coeff_class_rank_bonus = 1,
                                             .coeff_turn_number = 1,
                                             .coeff_damage_taken = 1,
                                             .coeff_danger = 1,
                                             .coeff_low_hp_self = 1,
                                             .class_rank_bonuses = {5, 2},
                                         });

  const uint8_t grp0[2] = {kJidLord, 0};
  const uint8_t grp1[2] = {kJidCavalier, 0};
  std::memcpy(rom_.data() + (kJobRankGroup0Addr - kRomBase), grp0,
              sizeof(grp0));
  std::memcpy(rom_.data() + (kJobRankGroup1Addr - kRomBase), grp1,
              sizeof(grp1));
  const uint32_t rank_ptrs[3] = {kJobRankGroup0Addr, kJobRankGroup1Addr, 0};
  std::memcpy(rom_.data() + (kAiJobRankTableAddr - kRomBase), rank_ptrs,
              sizeof(rank_ptrs));

  const FriendZoneCombatEntry fz_combat[5] = {
      {.dx = 0, .dy = -1, .weight = 2},   {.dx = 0, .dy = 1, .weight = 2},
      {.dx = -1, .dy = 0, .weight = 2},   {.dx = 1, .dy = 0, .weight = 2},
      {.dx = 0x7F, .dy = 0, .weight = 0},  // Sentinel
  };
  std::memcpy(rom_.data() + (kAiFriendZoneCombatTableAddr - kRomBase),
              fz_combat, sizeof(fz_combat));

  const FriendZonePosEntry fz_pos[5] = {
      {.dx = 0, .dy = -1}, {.dx = 0, .dy = 1},       {.dx = -1, .dy = 0},
      {.dx = 1, .dy = 0},  {.dx = 9999, .dy = 9999},  // Sentinel
  };
  std::memcpy(rom_.data() + (kAiFriendZonePosTableAddr - kRomBase), fz_pos,
              sizeof(fz_pos));

  // Chapter event list & flag bitmask tables: empty (0-terminated) event lists
  // dispatching opcode 0 to native `EventListEndHandler`.
  constexpr uint32_t kChapterEventsPtrsAddr = 0x08010300u;
  constexpr uint32_t kEmptyEventListAddr = 0x08010320u;
  const uint8_t bit_masks[8] = {1, 2, 4, 8, 16, 32, 64, 128};
  std::memcpy(rom_.data() + (kFlagBitMaskTableAddr - kRomBase), bit_masks,
              sizeof(bit_masks));
  WriteRom(kEventListHandlerTableAddr, ThumbAddr(kAddrEventListEndHandler));
  WriteRom(kEventListHandlerTableAddr + 4u, uint32_t{1u});
  WriteRom(kChapterEventAssetTableAddr, kChapterEventsPtrsAddr);
  WriteRom(kChapterEventsPtrsAddr, kEmptyEventListAddr);
  WriteRom(kVictoryEventTableAddr + 0x0Cu, kEmptyEventListAddr);

  // Top-level Enemy AI ROM tables: faction counts, recovery thresholds, and
  // nearby tile offsets (`kAiNearbyOffsetTableAddr - 4` until 0x270F).
  const int32_t faction_counts[3] = {62, 20, 50};
  std::memcpy(rom_.data() + (kAiFactionUnitCountsAddr - kRomBase),
              faction_counts, sizeof(faction_counts));
  const uint8_t rec_thresholds[8] = {100, 50, 0, 0, 80, 30, 0, 0};
  std::memcpy(rom_.data() + (kAiRecoveryThresholdTableAddr - kRomBase),
              rec_thresholds, sizeof(rec_thresholds));
  const AiPos nearby_offsets[6] = {
      {.x = 0, .y = 0}, {.x = 1, .y = 0},  {.x = -1, .y = 0},
      {.x = 0, .y = 1}, {.x = 0, .y = -1}, {.x = 0x270F, .y = 0},
  };
  std::memcpy(rom_.data() + (kAiNearbyOffsetTableAddr - 4u - kRomBase),
              nearby_offsets, sizeof(nearby_offsets));
}

void SkirmishIntegrationTest::InitPaddedMap(uint32_t buf_addr,
                                            uint32_t ptr_addr, uint8_t fill,
                                            int pad_rows, int pad_cols) {
  const uint32_t rows = static_cast<uint32_t>(kMapH + 2 * pad_rows);
  const uint32_t stride = static_cast<uint32_t>(kMapW + 2 * pad_cols);
  const uint32_t tile_base = buf_addr + rows * 4u;
  std::memset(ewram_.data() + (tile_base - kEwramBase), fill, rows * stride);
  for (uint32_t r = 0; r < rows; ++r) {
    const uint32_t row_addr =
        tile_base + r * stride + static_cast<uint32_t>(pad_cols);
    std::memcpy(ewram_.data() + (buf_addr - kEwramBase) + r * 4u, &row_addr,
                sizeof(row_addr));
  }
  const uint32_t map_addr = buf_addr + static_cast<uint32_t>(pad_rows) * 4u;
  std::memcpy(ewram_.data() + (ptr_addr - kEwramBase), &map_addr,
              sizeof(map_addr));
}

void SkirmishIntegrationTest::InitMaps() {
  GbaPtr<MapSize> sz(kMapSizeAddr);
  sz->x = kMapW;
  sz->y = kMapH;

  // Native `MapInit` allocates 2 padding rows/cols for 1-tile BFS probes.
  // `gMapUnit` needs 4-tile padding because Enemy AI scans up to ±3 tiles
  // without bounds checks.
  MapInit(GbaPtr<uint8_t>(kTerrainBufAddr),
          GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr), kMapW, kMapH);
  InitPaddedMap(kUnitMapBufAddr, kMapUnitAddr, 0u, /*pad_rows=*/4,
                /*pad_cols=*/4);
  MapInit(GbaPtr<uint8_t>(kMovMapBufAddr),
          GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr), kMapW, kMapH);
  MapInit(GbaPtr<uint8_t>(kRngMapBufAddr),
          GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapRangeAddr), kMapW, kMapH);
  MapInit(GbaPtr<uint8_t>(kOtherMapBufAddr),
          GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kAiMapOtherAddr), kMapW, kMapH);
  MapInit(GbaPtr<uint8_t>(kFogMapBufAddr),
          GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapFogAddr), kMapW, kMapH);
  MapInit(GbaPtr<uint8_t>(kHiddenMapBufAddr),
          GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapHiddenAddr), kMapW, kMapH);
  InitTraps();

  // Default 8x8 skirmish terrain ('.' = Plains, '#' = Wall, 'F' = Forest).
  // Initial roster: Lyn (1,3), Kent (2,4), Brigand A (3,4), Brigand B (5,4/F).
  LoadTerrainFromAscii({
      "........",  // y=0
      "........",  // y=1
      "...#....",  // y=2: Wall at (3,2)
      "....F...",  // y=3: Forest at (4,3)
      ".....F..",  // y=4: Forest at (5,4)
      "........",  // y=5
      "........",  // y=6
      "........",  // y=7
  });
}

void SkirmishIntegrationTest::InitPlayState() {
  SetPhaseFaction(kFactionBlue);
  SetTurnNumber(1);
  SetDangerMapFilled(true);
  *GbaPtr<uint8_t>(kAiStateAddr + kAiStateFlagsOffset) = 0u;
  *GbaPtr<uint32_t>(kAiScoreCoeffPtrAddr) = kAiCombatScoreCoeffTableAddr;
}

void SkirmishIntegrationTest::InitRoster() {
  // Blue 1: Lyn (Lv 1, 90 EXP so one kill triggers a guaranteed level-up).
  ConfigureUnit(kUnitIdLyn, {
                                .pid = kPidLyn,
                                .jid = kJidLord,
                                .level = 1,
                                .exp = 90,
                                .x = 1,
                                .y = 3,
                                .hp = 18,
                                .pow = 8,
                                .skl = 10,
                                .spd = 12,
                                .def = 4,
                                .res = 2,
                                .lck = 6,
                                .weapon_kind = ItemKind::kSword,
                                .weapon_iid = kIidIronSword,
                                .support0_exp = 81,
                            });

  // Blue 2: Kent (Lv 2, positioned at (2,4) for Lyn C-support aura).
  ConfigureUnit(kUnitIdKent, {
                                 .pid = kPidKent,
                                 .jid = kJidCavalier,
                                 .level = 2,
                                 .exp = 10,
                                 .x = 2,
                                 .y = 4,
                                 .hp = 22,
                                 .pow = 9,
                                 .skl = 8,
                                 .spd = 9,
                                 .def = 7,
                                 .res = 2,
                                 .lck = 4,
                                 .weapon_kind = ItemKind::kLance,
                                 .weapon_iid = kIidIronLance,
                                 .support0_exp = 81,
                             });

  // Red 0x81: Brigand A at (3,4), weak enough for Lyn to double & KO.
  ConfigureUnit(kUnitIdBrigandA, {
                                     .pid = kPidBrigandA,
                                     .jid = kJidBrigand,
                                     .level = 2,
                                     .x = 3,
                                     .y = 4,
                                     .hp = 18,
                                     .pow = 7,
                                     .skl = 4,
                                     .spd = 5,
                                     .def = 3,
                                     .weapon_kind = ItemKind::kAxe,
                                     .weapon_iid = kIidIronAxe,
                                 });

  // Red 0x82: Brigand B in forest at (5,4) with 12 HP.
  ConfigureUnit(kUnitIdBrigandB, {
                                     .pid = kPidBrigandB,
                                     .jid = kJidBrigand,
                                     .level = 2,
                                     .x = 5,
                                     .y = 4,
                                     .hp = 12,
                                     .pow = 8,
                                     .skl = 5,
                                     .spd = 5,
                                     .def = 3,
                                     .weapon_kind = ItemKind::kAxe,
                                     .weapon_iid = kIidIronAxe,
                                 });
}

}  // namespace fe7::e2e
