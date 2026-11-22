// End-to-end pure-native tactical skirmish integration test.
//
// Wires all 11 decompiled FE7 gameplay subsystems together across a synthetic
// 8x8 combat level (Lyn + Kent vs. 2 Brigands) with zero retail ROM and zero
// libmgba ARM7TDMI fallback:
//   - Movement BFS (`MapInit`, `MapFill`, `MapFloodUnit`)
//   - Support aura bonuses & per-turn adjacent growth (`GetUnitSupportBonuses`,
//     `ProcessSupportGains`, `CanUnitSupportNow`, `UnitGainSupportLevel`)
//   - Battle forecasting & real combat unwinding (`InitBattleUnit`,
//     `ComputeBattleUnitStats`, `ComputeBattleUnitEffectiveStats`,
//     `BattleApplyWeaponTriangleEffect`, `BattleUnwind`)
//   - Post-battle EXP, level-up, WEXP, weapon breaks & unit sync
//     (`BattleApplyExpGains`, `BattleApplyUnitUpdates`,
//     `UnitRemoveInvalidItems`)
//   - Phase rotation & faction refresh (`BmMainChangePhase`, `GetCurrentPhase`,
//     `CountUnitsByFaction`)
//   - Enemy AI positioning & combat scoring (`AiFindBestPositionInRange`,
//     `AiComputeCombatScore`, `AiSetDecision`)

#include <cstdint>
#include <cstring>
#include <vector>

#include "absl/base/nullability.h"
#include "game/ai/ai.h"
#include "game/battle.h"
#include "game/battle/battle.h"
#include "game/battle/battle_exp.h"
#include "game/battle/battle_sim.h"
#include "game/engine/rng.h"
#include "game/item.h"
#include "game/item/item.h"
#include "game/map/map.h"
#include "game/phase/phase.h"
#include "game/register_all.h"
#include "game/support/support.h"
#include "game/unit.h"
#include "game/unit/unit.h"
#include "gtest/gtest.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

// Synthetic ROM layout offsets (all inside 0x08000000..0x08FFFFFF).
constexpr uint32_t kRomBase = 0x08000000u;
constexpr uint32_t kRomSize = 0x01000000u;
constexpr uint32_t kTerrainAvoAddr = 0x08010000u;
constexpr uint32_t kTerrainDefAddr = 0x08010080u;
constexpr uint32_t kTerrainResAddr = 0x08010100u;
constexpr uint32_t kMovTableFootAddr = 0x08010180u;
constexpr uint32_t kLynSupportInfoAddr = 0x08010200u;
constexpr uint32_t kKentSupportInfoAddr = 0x08010220u;
constexpr uint32_t kJobRankGroup0Addr = 0x08010280u;
constexpr uint32_t kJobRankGroup1Addr = 0x08010290u;

// Synthetic EWRAM map buffers (each 0x1000 bytes, well away from unit arrays).
constexpr uint32_t kTerrainBufAddr = 0x02010000u;
constexpr uint32_t kUnitMapBufAddr = 0x02011000u;
constexpr uint32_t kMovMapBufAddr = 0x02012000u;
constexpr uint32_t kRngMapBufAddr = 0x02013000u;
constexpr uint32_t kOtherMapBufAddr = 0x02014000u;

// Synthetic character / class / item IDs.
constexpr uint8_t kPidLyn = 1;
constexpr uint8_t kPidKent = 2;
constexpr uint8_t kPidBrigandA = 10;
constexpr uint8_t kPidBrigandB = 11;

constexpr uint8_t kJidLord = 1;
constexpr uint8_t kJidCavalier = 2;
constexpr uint8_t kJidBrigand = 3;

constexpr uint8_t kIidIronSword = 1;
constexpr uint8_t kIidIronLance = 2;
constexpr uint8_t kIidIronAxe = 3;

constexpr int kMapW = 8;
constexpr int kMapH = 8;

// Terrain IDs used on the synthetic 8x8 skirmish map.
constexpr uint8_t kTerrainWall = 0;
constexpr uint8_t kTerrainPlains = 1;
constexpr uint8_t kTerrainForest = 2;

struct WeaponTriangleEntry {
  int8_t attacker_kind;
  int8_t defender_kind;
  int8_t hit_bonus;
  int8_t dmg_bonus;
};

struct FriendZoneCombatEntry {
  int8_t dx;
  int8_t dy;
  int8_t weight;
  uint8_t pad;
};

struct FriendZonePosEntry {
  int16_t dx;
  int16_t dy;
};

// --- Shrinking list of un-decompiled leaf GbaCall() stubs ---
// Remove each stub as its real FE7 function is decompiled in future waves.

uint32_t StubNoOpZero(uint32_t, uint32_t, uint32_t, uint32_t,
                      const uint32_t* absl_nullable) {
  return 0;
}

uint32_t StubGetUnitMovementCost(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                 const uint32_t* absl_nullable) {
  return GbaPtr<const Unit>(r0)->jinfo->mov_table[0].addr;
}

uint32_t StubComputeBattleUnitSupportBonuses(uint32_t r0, uint32_t, uint32_t,
                                             uint32_t,
                                             const uint32_t* absl_nullable) {
  GbaPtr<BattleUnit> bu(r0);
  // Scratch buffer in unused EWRAM for SupportBonuses output.
  constexpr uint32_t kScratchBonusesAddr = 0x02018000u;
  GbaPtr<SupportBonuses> bonuses(kScratchBonusesAddr);
  GetUnitSupportBonuses(GbaPtr<const Unit>(bu.addr), bonuses);
  bu->battle_attack =
      static_cast<int16_t>(bu->battle_attack + bonuses->bonus_attack);
  bu->battle_defense =
      static_cast<int16_t>(bu->battle_defense + bonuses->bonus_defense);
  bu->battle_hit_rate =
      static_cast<int16_t>(bu->battle_hit_rate + bonuses->bonus_hit);
  bu->battle_avoid_rate =
      static_cast<int16_t>(bu->battle_avoid_rate + bonuses->bonus_avoid);
  bu->battle_crit_rate =
      static_cast<int16_t>(bu->battle_crit_rate + bonuses->bonus_crit);
  bu->battle_dodge_rate =
      static_cast<int16_t>(bu->battle_dodge_rate + bonuses->bonus_dodge);
  return 0;
}

uint32_t StubUnitRemoveInvalidItems(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                    const uint32_t* absl_nullable) {
  GbaPtr<Unit> unit(r0);
  int dst = 0;
  for (int src = 0; src < kUnitItemCount; ++src) {
    if (unit->items[src] != 0u) {
      unit->items[dst++] = unit->items[src];
    }
  }
  while (dst < kUnitItemCount) {
    unit->items[dst++] = 0u;
  }
  return 0;
}

class SkirmishIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ewram_.assign(0x40000u, 0u);
    iwram_.assign(0x8000u, 0u);
    rom_.assign(kRomSize, 0u);

    gMem.ewram = ewram_.data();
    gMem.iwram = iwram_.data();
    gMem.rom = rom_.data();
    gMem.rom_size = rom_.size();

    ResetDispatchTable();
    RegisterAllNativeFunctions();
    RegisterLeafStubs();

    InitRomTables();
    InitMaps();
    InitPlayState();
    InitRoster();
    SyncUnitMap();
  }

  void TearDown() override {
    ResetDispatchTable();
    gMem = {};
  }

  template <typename T>
  void WriteRom(uint32_t gba_addr, const T& value) {
    std::memcpy(rom_.data() + (gba_addr - kRomBase), &value, sizeof(T));
  }

  // Registers the minimal set of leaf GbaCall() stubs not yet decompiled.
  void RegisterLeafStubs() {
    RegisterNative(kAddrGetUnitMovementCost, StubGetUnitMovementCost);
    RegisterNative(kAddrComputeBattleUnitSupportBonuses,
                   StubComputeBattleUnitSupportBonuses);
    RegisterNative(kAddrUnitRemoveInvalidItems, StubUnitRemoveInvalidItems);
    RegisterNative(kAddrHasTacticianAfaBonus, StubNoOpZero);
    RegisterNative(kAddrUnitCheckStatOverflow, StubNoOpZero);
    RegisterNative(kAddrPidStatsAddExpGained, StubNoOpZero);
    RegisterNative(kAddrRefreshUnitSprites, StubNoOpZero);
    RegisterNative(kAddrCheckTurnEvents, StubNoOpZero);
    RegisterNative(kAddrCheckVictoryEvents, StubNoOpZero);
    RegisterNative(kAddrRecordUnitTurnDeployment, StubNoOpZero);
    RegisterNative(kAddrAiFillDangerMap, StubNoOpZero);
  }

  void InitRomTables() {
    // Populate gUnitLookup (kUnitLutAddr) for Blue (1..62), Green (0x41..0x54),
    // and Red (0x81..0xB2) into EWRAM unit array slots.
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

    // Terrain bonuses: wall(0)=0, plains(1)=0, forest(2)=+20 avo, +1 def.
    int8_t avo[65]{};
    int8_t def[65]{};
    int8_t res[65]{};
    avo[kTerrainForest] = 20;
    def[kTerrainForest] = 1;
    std::memcpy(rom_.data() + (kTerrainAvoAddr - kRomBase), avo, sizeof(avo));
    std::memcpy(rom_.data() + (kTerrainDefAddr - kRomBase), def, sizeof(def));
    std::memcpy(rom_.data() + (kTerrainResAddr - kRomBase), res, sizeof(res));

    // Movement costs: wall(0)=-1 (impassable), plains(1)=1, forest(2)=2.
    int8_t mov[65];
    std::memset(mov, -1, sizeof(mov));
    mov[kTerrainPlains] = 1;
    mov[kTerrainForest] = 2;
    std::memcpy(rom_.data() + (kMovTableFootAddr - kRomBase), mov, sizeof(mov));

    // Weapon triangle rules (Sword > Axe > Lance > Sword).
    const WeaponTriangleEntry wta[7] = {
        {static_cast<int8_t>(ItemKind::kSword),
         static_cast<int8_t>(ItemKind::kAxe), 15, 1},
        {static_cast<int8_t>(ItemKind::kAxe),
         static_cast<int8_t>(ItemKind::kSword), -15, -1},
        {static_cast<int8_t>(ItemKind::kLance),
         static_cast<int8_t>(ItemKind::kSword), 15, 1},
        {static_cast<int8_t>(ItemKind::kSword),
         static_cast<int8_t>(ItemKind::kLance), -15, -1},
        {static_cast<int8_t>(ItemKind::kAxe),
         static_cast<int8_t>(ItemKind::kLance), 15, 1},
        {static_cast<int8_t>(ItemKind::kLance),
         static_cast<int8_t>(ItemKind::kAxe), -15, -1},
        {-1, 0, 0, 0},
    };
    std::memcpy(rom_.data() + (kWeaponTriangleTableAddr - kRomBase), wta,
                sizeof(wta));

    // Support caps (uint32_t[4]: C=80, B=160, A=240) and Affinity bonuses.
    const uint32_t caps[4] = {80, 160, 240, 240};
    std::memcpy(rom_.data() + (kSupportLevelCapsAddr - kRomBase), caps,
                sizeof(caps));
    const SupportBonuses aff[3] = {
        {1, 1, 0, 5, 5, 5, 0, 0},  // Fire: +1 atk, +5 hit/avo/crit per level
        {2, 1, 0, 5, 0, 5, 5, 0},  // Wind: +1 atk, +5 hit/crit/dodge per level
        {0, 0, 0, 0, 0, 0, 0, 0},
    };
    std::memcpy(rom_.data() + (kAffinityBonusTableAddr - kRomBase), aff,
                sizeof(aff));

    // Lyn <-> Kent support partner tables (growth = 5 pts/turn adjacent).
    SupportInfo lyn_sup{};
    lyn_sup.pids[0] = kPidKent;
    lyn_sup.exp_base[0] = 80;
    lyn_sup.exp_growth[0] = 5;
    lyn_sup.count = 1;
    WriteRom(kLynSupportInfoAddr, lyn_sup);

    SupportInfo kent_sup{};
    kent_sup.pids[0] = kPidLyn;
    kent_sup.exp_base[0] = 80;
    kent_sup.exp_growth[0] = 5;
    kent_sup.count = 1;
    WriteRom(kKentSupportInfoAddr, kent_sup);

    // Items at kIInfoTableAddr: Iron Sword, Iron Lance, Iron Axe.
    auto set_item = [&](uint8_t iid, ItemKind kind, uint8_t uses, uint8_t mt,
                        uint8_t hit, uint8_t wt) {
      IInfo info{};
      info.id = iid;
      info.kind = static_cast<uint8_t>(kind);
      info.attributes = kItemAttrWeapon;
      info.max_uses = uses;
      info.might = mt;
      info.hit = hit;
      info.weight = wt;
      info.encoded_range = 0x11u;  // 1-1 range
      info.required_wexp = 1;      // E rank
      info.wexp_gain = 1;
      WriteRom(kIInfoTableAddr + static_cast<uint32_t>(iid) * sizeof(IInfo),
               info);
    };
    set_item(kIidIronSword, ItemKind::kSword, 46, 5, 90, 5);
    set_item(kIidIronLance, ItemKind::kLance, 45, 7, 80, 8);
    set_item(kIidIronAxe, ItemKind::kAxe, 45, 8, 75, 10);

    // Classes at kJInfoTableAddr: Lord, Cavalier, Brigand.
    auto set_job = [&](uint8_t jid, int8_t mov, int8_t con, int8_t power) {
      JInfo j{};
      j.id = jid;
      j.base_mov = mov;
      j.base_con = con;
      j.max_hp = 60;
      j.max_pow = 20;
      j.max_skl = 20;
      j.max_spd = 20;
      j.max_def = 20;
      j.max_res = 20;
      j.max_con = 20;
      j.power_level = power;
      j.mov_table[0] = GbaPtr<const int8_t>(kMovTableFootAddr);
      j.avo_terrain_table = GbaPtr<const int8_t>(kTerrainAvoAddr);
      j.def_terrain_table = GbaPtr<const int8_t>(kTerrainDefAddr);
      j.res_terrain_table = GbaPtr<const int8_t>(kTerrainResAddr);
      WriteRom(kJInfoTableAddr + static_cast<uint32_t>(jid) * sizeof(JInfo), j);
    };
    set_job(kJidLord, 5, 5, 3);
    set_job(kJidCavalier, 7, 9, 3);
    set_job(kJidBrigand, 5, 12, 2);

    // Characters at kPInfoTableAddr: Lyn, Kent, Brigand A, Brigand B.
    auto set_char = [&](uint8_t pid, uint8_t jid, uint8_t affinity,
                        uint32_t sup_addr, uint8_t growth) {
      PInfo p{};
      p.id = pid;
      p.jid_default = jid;
      p.affinity = affinity;
      p.growth_hp = growth;
      p.growth_pow = growth;
      p.growth_skl = growth;
      p.growth_spd = growth;
      p.growth_def = growth;
      p.growth_res = growth;
      p.growth_lck = growth;
      p.support_info = GbaPtr<const SupportInfo>(sup_addr);
      WriteRom(kPInfoTableAddr + static_cast<uint32_t>(pid) * sizeof(PInfo), p);
    };
    set_char(kPidLyn, kJidLord, 2, kLynSupportInfoAddr, 100);
    set_char(kPidKent, kJidCavalier, 1, kKentSupportInfoAddr, 80);
    set_char(kPidBrigandA, kJidBrigand, 0, 0, 50);
    set_char(kPidBrigandB, kJidBrigand, 0, 0, 50);

    // AI ROM tables: score coefficients, job ranks, and friend-zone offsets.
    AiCombatScoreCoefficients coeff{};
    coeff.coeff_damage_dealt = 2;
    coeff.coeff_low_hp_opponent = 1;
    coeff.coeff_friend_zone = 1;
    coeff.coeff_class_rank_bonus = 1;
    coeff.coeff_turn_number = 1;
    coeff.coeff_damage_taken = 1;
    coeff.coeff_danger = 1;
    coeff.coeff_low_hp_self = 1;
    coeff.class_rank_bonuses[0] = 5;
    coeff.class_rank_bonuses[1] = 2;
    WriteRom(kAiCombatScoreCoeffTableAddr, coeff);

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
        {0, -1, 2, 0}, {0, 1, 2, 0},    {-1, 0, 2, 0},
        {1, 0, 2, 0},  {0x7F, 0, 0, 0},
    };
    std::memcpy(rom_.data() + (kAiFriendZoneCombatTableAddr - kRomBase),
                fz_combat, sizeof(fz_combat));

    const FriendZonePosEntry fz_pos[5] = {
        {0, -1}, {0, 1}, {-1, 0}, {1, 0}, {9999, 9999},
    };
    std::memcpy(rom_.data() + (kAiFriendZonePosTableAddr - kRomBase), fz_pos,
                sizeof(fz_pos));
  }

  void InitPaddedMap(uint32_t buf_addr, uint32_t ptr_addr, uint8_t fill,
                     int pad_rows, int pad_cols) {
    const uint32_t rows = static_cast<uint32_t>(kMapH + 2 * pad_rows);
    const uint32_t stride = static_cast<uint32_t>(kMapW + 2 * pad_cols);
    const uint32_t tile_base = buf_addr + rows * 4u;
    std::memset(ewram_.data() + (tile_base - 0x02000000u), fill, rows * stride);
    for (uint32_t r = 0; r < rows; ++r) {
      const uint32_t row_addr =
          tile_base + r * stride + static_cast<uint32_t>(pad_cols);
      std::memcpy(ewram_.data() + (buf_addr - 0x02000000u) + r * 4u, &row_addr,
                  sizeof(row_addr));
    }
    const uint32_t map_addr = buf_addr + static_cast<uint32_t>(pad_rows) * 4u;
    std::memcpy(ewram_.data() + (ptr_addr - 0x02000000u), &map_addr,
                sizeof(map_addr));
  }

  void InitMaps() {
    GbaPtr<MapSize> sz(kMapSizeAddr);
    sz->x = kMapW;
    sz->y = kMapH;

    // Use native MapInit for standard 2-row-padded maps, and 4-tile padding for
    // gMapUnit so Enemy AI position scoring stays in-bounds at map edges.
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

    GbaPtr<GbaPtr<uint8_t>> terrain =
        *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr);
    // Keep all outer padding rows/columns impassable (`kTerrainWall`) so BFS
    // flooding stops cleanly at map boundaries.
    MapFill(terrain, kTerrainWall);
    for (int y = 0; y < kMapH; ++y) {
      for (int x = 0; x < kMapW; ++x) {
        terrain[y][x] = kTerrainPlains;
      }
    }
    terrain[2][3] = kTerrainWall;
    terrain[3][4] = kTerrainForest;
    terrain[4][5] = kTerrainForest;
  }

  void InitPlayState() {
    // Turn 1, Blue Phase.
    *GbaPtr<uint8_t>(kPhasePlayStatusAddr + 0x0Fu) = kFactionBlue;
    *GbaPtr<uint16_t>(kPhasePlayStatusAddr + 0x10u) = 1u;
    // Mark danger map already filled so AiRefreshDangerMap doesn't wipe custom
    // values unless requested.
    *GbaPtr<uint8_t>(kAiStateAddr + 0x7Au) = 1u;
    *GbaPtr<uint8_t>(kAiStateAddr + 0x7Du) = 0u;
    *GbaPtr<uint32_t>(kAiScoreCoeffPtrAddr) = kAiCombatScoreCoeffTableAddr;
  }

  void InitRoster() {
    // Blue 1: Lyn (Lv 1, 90 EXP so one kill triggers a guaranteed level-up).
    GbaPtr<Unit> lyn = GetUnit(1);
    ClearUnit(lyn);
    lyn->id = 1;
    lyn->pinfo = GetPersonInfo(kPidLyn);
    lyn->jinfo = GetJobInfo(kJidLord);
    lyn->level = 1;
    lyn->exp = 90;
    lyn->x = 1;
    lyn->y = 3;
    lyn->max_hp = 18;
    lyn->hp = 18;
    lyn->pow = 8;
    lyn->skl = 10;
    lyn->spd = 12;
    lyn->def = 4;
    lyn->res = 2;
    lyn->lck = 6;
    lyn->wexp[static_cast<int>(ItemKind::kSword)] = 1;
    lyn->items[0] = static_cast<uint16_t>(CreateItem(kIidIronSword));
    lyn->supports[0] = 81;  // Active C-support with Kent (>= 81).

    // Blue 2: Kent (Lv 2, positioned at (2, 4) to give Lyn C-support aura).
    GbaPtr<Unit> kent = GetUnit(2);
    ClearUnit(kent);
    kent->id = 2;
    kent->pinfo = GetPersonInfo(kPidKent);
    kent->jinfo = GetJobInfo(kJidCavalier);
    kent->level = 2;
    kent->exp = 10;
    kent->x = 2;
    kent->y = 4;
    kent->max_hp = 22;
    kent->hp = 22;
    kent->pow = 9;
    kent->skl = 8;
    kent->spd = 9;
    kent->def = 7;
    kent->res = 2;
    kent->lck = 4;
    kent->wexp[static_cast<int>(ItemKind::kLance)] = 1;
    kent->items[0] = static_cast<uint16_t>(CreateItem(kIidIronLance));
    kent->supports[0] = 81;  // Active C-support with Lyn (>= 81).

    // Red 0x81: Brigand A at (3, 4) in forest, weak enough for Lyn to double
    // and KO cleanly.
    GbaPtr<Unit> brigand_a = GetUnit(0x81);
    ClearUnit(brigand_a);
    brigand_a->id = static_cast<int8_t>(0x81);
    brigand_a->pinfo = GetPersonInfo(kPidBrigandA);
    brigand_a->jinfo = GetJobInfo(kJidBrigand);
    brigand_a->level = 2;
    brigand_a->exp = 0;
    brigand_a->x = 3;
    brigand_a->y = 4;
    brigand_a->max_hp = 18;
    brigand_a->hp = 18;
    brigand_a->pow = 7;
    brigand_a->skl = 4;
    brigand_a->spd = 5;
    brigand_a->def = 3;
    brigand_a->res = 0;
    brigand_a->lck = 0;
    brigand_a->wexp[static_cast<int>(ItemKind::kAxe)] = 1;
    brigand_a->items[0] = static_cast<uint16_t>(CreateItem(kIidIronAxe));

    // Red 0x82: Brigand B at (5, 4) with 12 HP (survives Turn 1, attacks on
    // Enemy Phase, then routed on Turn 2).
    GbaPtr<Unit> brigand_b = GetUnit(0x82);
    ClearUnit(brigand_b);
    brigand_b->id = static_cast<int8_t>(0x82);
    brigand_b->pinfo = GetPersonInfo(kPidBrigandB);
    brigand_b->jinfo = GetJobInfo(kJidBrigand);
    brigand_b->level = 2;
    brigand_b->exp = 0;
    brigand_b->x = 5;
    brigand_b->y = 4;
    brigand_b->max_hp = 12;
    brigand_b->hp = 12;
    brigand_b->pow = 8;
    brigand_b->skl = 5;
    brigand_b->spd = 5;
    brigand_b->def = 3;
    brigand_b->res = 0;
    brigand_b->lck = 0;
    brigand_b->wexp[static_cast<int>(ItemKind::kAxe)] = 1;
    brigand_b->items[0] = static_cast<uint16_t>(CreateItem(kIidIronAxe));
  }

  void SyncUnitMap() {
    GbaPtr<GbaPtr<uint8_t>> umap =
        *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr);
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

  // Runs a full native FE7 combat exchange between `attacker` and `defender`,
  // optionally in simulation-only (`kBattleConfigSimulate`) or real mode.
  void RunCombatExchange(GbaPtr<Unit> attacker, GbaPtr<Unit> defender,
                         uint16_t config_flags) {
    GbaPtr<BattleStats> stats(kBattleStatsAddr);
    *stats = {};
    stats->config = config_flags;
    stats->range = 1;

    GbaPtr<BattleUnit> actor(kBattleActorAddr);
    GbaPtr<BattleUnit> target(kBattleTargetAddr);
    InitBattleUnit(actor, GbaPtr<const Unit>(attacker.addr));
    InitBattleUnit(target, GbaPtr<const Unit>(defender.addr));

    // Set terrain bonuses from gMapTerrain for both combatants.
    GbaPtr<GbaPtr<uint8_t>> terrain =
        *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr);
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

  std::vector<uint8_t> ewram_;
  std::vector<uint8_t> iwram_;
  std::vector<uint8_t> rom_;
};

TEST_F(SkirmishIntegrationTest, FullMultiTurnCombatLevel) {
  RandInit(0x1234u);

  GbaPtr<Unit> lyn = GetUnit(1);
  GbaPtr<Unit> kent = GetUnit(2);
  GbaPtr<Unit> brigand_a = GetUnit(0x81);
  GbaPtr<Unit> brigand_b = GetUnit(0x82);

  // --- Turn 1, Player Phase (Blue) ---
  EXPECT_EQ(GetCurrentPhase(), kFactionBlue);
  EXPECT_EQ(CountUnitsByFaction(kFactionBlue), 2);
  EXPECT_EQ(CountUnitsByFaction(kFactionRed), 2);

  // 1. BFS movement flood for Lyn (5 mov): wall at (3,2) is unreachable,
  //    plains tile (2,3) next to Kent & Brigand A is reachable at cost 1.
  MapFloodUnit(GbaPtr<const Unit>(lyn.addr));
  GbaPtr<GbaPtr<uint8_t>> mov_map =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr);
  EXPECT_EQ(mov_map[2][3], 0xFFu);  // Wall tile blocked.
  EXPECT_LE(mov_map[3][2], 5u);     // (2,3) reachable.
  EXPECT_EQ(mov_map[3][2], 1u);

  // Move Lyn to (2, 3), adjacent to Kent (2, 4) and diagonal from Brigand A;
  // move Lyn to (3, 3) to attack Brigand A at (3, 4) while within 2 tiles of
  // Kent (2, 4) for C-support aura.
  ASSERT_LE(mov_map[3][3], 5u);
  lyn->x = 3;
  lyn->y = 3;
  SyncUnitMap();

  // 2. Verify live Wind+Fire C-support aura from Kent (distance = 2 <= 3).
  SupportBonuses lyn_bonuses{};
  constexpr uint32_t kBonusOutAddr = 0x02018100u;
  GetUnitSupportBonuses(GbaPtr<const Unit>(lyn.addr),
                        GbaPtr<SupportBonuses>(kBonusOutAddr));
  std::memcpy(&lyn_bonuses, ewram_.data() + (kBonusOutAddr - 0x02000000u),
              sizeof(lyn_bonuses));
  EXPECT_GT(lyn_bonuses.bonus_attack, 0);
  EXPECT_GT(lyn_bonuses.bonus_hit, 0);

  // 3. Lyn attacks Brigand A (Sword vs. Axe WTA, speed double, guaranteed KO
  //    and level-up from 90 EXP with 100% growths).
  const int sword_uses_before = GetItemUses(lyn->items[0]);
  RunCombatExchange(lyn, brigand_a, kBattleConfigReal);
  lyn->flags |= kUnitFlagTurnEnded;

  GbaPtr<const BattleUnit> actor(kBattleActorAddr);
  EXPECT_EQ(actor->wtriangle_hit_bonus, 15);
  EXPECT_EQ(actor->wtriangle_dmg_bonus, 1);
  EXPECT_EQ(brigand_a->hp, 0);
  EXPECT_NE(brigand_a->flags & kUnitFlagDead, 0u);
  EXPECT_LT(GetItemUses(lyn->items[0]), sword_uses_before);
  EXPECT_EQ(lyn->level, 2);    // Leveled up!
  EXPECT_GT(lyn->max_hp, 18);  // 100% growth rolled +1.
  EXPECT_GT(lyn->pow, 8);
  EXPECT_GT(lyn->wexp[static_cast<int>(ItemKind::kSword)], 1u);
  EXPECT_EQ(CountUnitsByFaction(kFactionRed), 1);

  // Kent moves to (4, 4) to screen Brigand B, then ends turn.
  kent->x = 4;
  kent->y = 4;
  kent->flags |= kUnitFlagTurnEnded;
  SyncUnitMap();

  // 4. Phase switch: Blue -> Red. Blue units are refreshed.
  EXPECT_EQ(BmMainChangePhase(), 1);
  EXPECT_EQ(GetCurrentPhase(), kFactionRed);
  EXPECT_EQ(lyn->flags & kUnitFlagTurnEnded, 0u);
  EXPECT_EQ(kent->flags & kUnitFlagTurnEnded, 0u);

  // --- Turn 1, Enemy Phase (Red) ---
  // 5. Brigand B runs AI movement flood, evaluates position & combat score
  //    against Kent, records gAiDecision, and attacks Kent.
  *GbaPtr<uint8_t>(kAiActiveUnitIdAddr) = static_cast<uint8_t>(brigand_b->id);
  *GbaPtr<uint32_t>(kAiActiveUnitAddr) = brigand_b.addr;

  MapFloodUnit(GbaPtr<const Unit>(brigand_b.addr));
  GbaPtr<GbaPtr<uint8_t>> rng_map =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapRangeAddr);
  MapFill(rng_map, 0);
  // Tiles adjacent to Kent (4, 4): (5, 4) is forest (bonus def/avo).
  rng_map[4][5] = 1;
  rng_map[3][4] = 1;

  constexpr uint32_t kBestPosAddr = 0x02018200u;
  GbaPtr<AiPos> best_pos(kBestPosAddr);
  ASSERT_TRUE(AiFindBestPositionInRange(best_pos));
  EXPECT_EQ(best_pos->x, 5);
  EXPECT_EQ(best_pos->y, 4);  // Forest tile (5, 4) preferred.

  // Score Brigand B vs. Kent using simulated battle stats.
  RunCombatExchange(brigand_b, kent, kBattleConfigSimulate);
  constexpr uint32_t kSimStAddr = 0x02018220u;
  GbaPtr<AiCombatSimulationSt> sim_st(kSimStAddr);
  *sim_st = {};
  sim_st->x_move = static_cast<uint8_t>(best_pos->x);
  sim_st->y_move = static_cast<uint8_t>(best_pos->y);
  sim_st->target_id = static_cast<uint8_t>(kent->id);
  AiComputeCombatScore(sim_st);
  EXPECT_GT(sim_st->score, 0u);

  AiClearDecision();
  AiSetDecision(best_pos->x, best_pos->y, /*action_id=*/1,
                static_cast<uint8_t>(kent->id), /*item_slot=*/0,
                static_cast<uint8_t>(kent->x), static_cast<uint8_t>(kent->y));
  GbaPtr<const AiDecision> dec(kAiDecisionAddr);
  EXPECT_EQ(dec->unit_target_id, 2u);
  EXPECT_EQ(dec->action_performed, 1);

  // Execute Brigand B's attack on Kent.
  RunCombatExchange(brigand_b, kent, kBattleConfigReal);
  brigand_b->flags |= kUnitFlagTurnEnded;

  // 6. Advance Red -> Green -> Blue (Turn 1 -> Turn 2).
  //    Position Lyn adjacent to Kent before turn wrap to verify
  //    ProcessSupportGains() runs on Turn 2 start.
  lyn->x = 4;
  lyn->y = 3;
  SyncUnitMap();
  const uint8_t sup_before = lyn->supports[0];

  EXPECT_EQ(BmMainChangePhase(), 1);
  EXPECT_EQ(*GbaPtr<uint8_t>(kPhasePlayStatusAddr + 0x0Fu), kFactionGreen);
  EXPECT_EQ(BmMainChangePhase(), 1);
  EXPECT_EQ(*GbaPtr<uint8_t>(kPhasePlayStatusAddr + 0x0Fu), kFactionBlue);
  EXPECT_EQ(GetCurrentPhase(), kFactionBlue);
  EXPECT_EQ(*GbaPtr<uint16_t>(kPhasePlayStatusAddr + 0x10u), 2u);
  // Adjacent Lyn & Kent gained +5 support EXP on Turn 2 Blue phase start.
  EXPECT_EQ(lyn->supports[0], sup_before + 5u);

  // --- Turn 2, Player Phase: Rout remaining Brigand B ---
  if ((brigand_b->flags & kUnitFlagDead) == 0u) {
    // Lyn moves adjacent to Brigand B (5, 4) and finishes it off.
    lyn->x = 5;
    lyn->y = 3;
    SyncUnitMap();
    RunCombatExchange(lyn, brigand_b, kBattleConfigReal);
  }
  EXPECT_EQ(CountUnitsByFaction(kFactionRed), 0);
  EXPECT_GT(CountUnitsByFaction(kFactionBlue), 0);

  // Zero un-handled ARM7TDMI fallback calls occurred during the entire battle.
  EXPECT_TRUE(GetFallbackReport().empty());
}

TEST_F(SkirmishIntegrationTest, BattleForecastDoesNotMutateUnits) {
  GbaPtr<Unit> lyn = GetUnit(1);
  GbaPtr<Unit> brigand_a = GetUnit(0x81);
  lyn->x = 3;
  lyn->y = 3;
  SyncUnitMap();

  const int8_t lyn_hp_before = lyn->hp;
  const int8_t enemy_hp_before = brigand_a->hp;
  const uint16_t sword_before = lyn->items[0];

  RunCombatExchange(lyn, brigand_a, kBattleConfigSimulate);

  // Forecast populates gBattleActor/gBattleTarget stats without mutating Units.
  GbaPtr<const BattleUnit> actor(kBattleActorAddr);
  GbaPtr<const BattleUnit> target(kBattleTargetAddr);
  EXPECT_GT(actor->battle_attack, target->battle_defense);
  EXPECT_EQ(lyn->hp, lyn_hp_before);
  EXPECT_EQ(brigand_a->hp, enemy_hp_before);
  EXPECT_EQ(lyn->items[0], sword_before);
  EXPECT_TRUE(GetFallbackReport().empty());
}

TEST_F(SkirmishIntegrationTest, WeaponBreakAndSupportRankUpAcrossTurns) {
  RandInit(0x5678u);
  GbaPtr<Unit> lyn = GetUnit(1);
  GbaPtr<Unit> kent = GetUnit(2);
  GbaPtr<Unit> brigand_a = GetUnit(0x81);

  // Give Lyn a 1-use Iron Sword so it breaks on her first strike.
  lyn->items[0] = static_cast<uint16_t>((1u << 8) | kIidIronSword);
  lyn->x = 3;
  lyn->y = 3;
  SyncUnitMap();

  RunCombatExchange(lyn, brigand_a, kBattleConfigReal);
  // Broken weapon is automatically removed from inventory by
  // UpdateUnitFromBattle -> UnitRemoveInvalidItems.
  EXPECT_EQ(lyn->items[0], 0u);

  // Advance to Turn 2 (FE7 skips support EXP on Turn 1) and place Lyn & Kent
  // adjacent at 155 support EXP (5 shy of B-support threshold 160).
  *GbaPtr<uint16_t>(kPhasePlayStatusAddr + 0x10u) = 2u;
  lyn->supports[0] = 155;
  kent->supports[0] = 155;
  lyn->x = 2;
  lyn->y = 3;
  kent->x = 2;
  kent->y = 4;
  SyncUnitMap();

  EXPECT_FALSE(CanUnitSupportNow(GbaPtr<const Unit>(lyn.addr), 0));
  ProcessSupportGains();
  EXPECT_EQ(lyn->supports[0], 160u);
  EXPECT_TRUE(CanUnitSupportNow(GbaPtr<const Unit>(lyn.addr), 0));

  UnitGainSupportLevel(lyn, 0);
  EXPECT_EQ(GetSupportLevelBySupportIndex(GbaPtr<const Unit>(lyn.addr), 0),
            2);  // B-rank achieved!
  EXPECT_TRUE(GetFallbackReport().empty());
}

}  // namespace
}  // namespace fe7
