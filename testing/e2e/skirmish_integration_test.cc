// End-to-end pure-native tactical skirmish integration test.
//
// Wires all 14 decompiled FE7 gameplay subsystems together across synthetic
// 8x8 combat levels (Lyn + Kent vs. 2 Brigands) with zero retail ROM and zero
// libmgba ARM7TDMI fallback:
//   - Movement BFS (`MapInit`, `MapFill`, `MapFloodUnit`)
//   - Traps, Light Runes & Fog of War (`AddLightRune`, `ApplyLightRuneTerrain`,
//     `AddFireTrap`, `RefreshEntityMaps`, `TickDamagingTraps`, `RemoveTrap`)
//   - Unit Actions, Stat Boosters, Autolevel & Rescue/Drop
//   (`ApplyItemStatBoost`,
//     `UnitApplyBonusLevels`, `CanUnitCarry`, `UnitRescue`, `UnitDropRescue`,
//     `KillUnit`)
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
//   - Enemy AI positioning, combat scoring, recovery, threat-based auto-equip,
//     nearby counting & random move (`AiFindBestPositionInRange`,
//     `AiComputeCombatScore`, `AiSetDecision`, `AiUpdateFactionRecovery`,
//     `AiEquipBestItem`, `AiCountEnemiesNearby`, `AiTryMoveRandomly`)

#include <cstdint>
#include <cstring>
#include <vector>

#include "absl/base/nullability.h"
#include "game/action/action.h"
#include "game/ai/ai.h"
#include "game/ai/ai_actions.h"
#include "game/ai/ai_combat.h"
#include "game/ai/ai_cp.h"
#include "game/ai/ai_decide.h"
#include "game/ai/ai_opcodes.h"
#include "game/ai/ai_script.h"
#include "game/battle.h"
#include "game/battle/battle.h"
#include "game/battle/battle_exp.h"
#include "game/battle/battle_sim.h"
#include "game/engine/proc.h"
#include "game/engine/rng.h"
#include "game/item.h"
#include "game/item/item.h"
#include "game/map/map.h"
#include "game/map/trap.h"
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
constexpr uint32_t kAngelicRobeBonusAddr = 0x080102A0u;
constexpr uint32_t kScriptATableAddr = 0x08044F00u;
constexpr uint32_t kScriptA0Addr = 0x08045000u;

// Synthetic EWRAM map buffers (each 0x1000 bytes, well away from unit arrays).
constexpr uint32_t kEwramBase = 0x02000000u;
constexpr uint32_t kTerrainBufAddr = 0x02010000u;
constexpr uint32_t kUnitMapBufAddr = 0x02011000u;
constexpr uint32_t kMovMapBufAddr = 0x02012000u;
constexpr uint32_t kRngMapBufAddr = 0x02013000u;
constexpr uint32_t kOtherMapBufAddr = 0x02014000u;
constexpr uint32_t kFogMapBufAddr = 0x02015000u;
constexpr uint32_t kHiddenMapBufAddr = 0x02016000u;

// Synthetic EWRAM scratch slots (used internally by fixture helpers).
constexpr uint32_t kScratchSupportBonusesAddr = 0x02018100u;
constexpr uint32_t kScratchBestPosAddr = 0x02018200u;
constexpr uint32_t kScratchCombatSimAddr = 0x02018220u;

// Synthetic roster unit IDs.
constexpr int kUnitIdLyn = 1;
constexpr int kUnitIdKent = 2;
constexpr int kUnitIdBrigandA = 0x81;
constexpr int kUnitIdBrigandB = 0x82;

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
constexpr uint8_t kIidSteelAxe = 4;
constexpr uint8_t kIidAngelicRobe = 5;
constexpr uint8_t kIidHealStaff = 0x4Au;
constexpr uint8_t kIidVulnerary = 0x6Bu;

constexpr int kMapW = 8;
constexpr int kMapH = 8;

// Terrain IDs and movement sentinels used on the synthetic 8x8 skirmish map.
constexpr uint8_t kTerrainWall = 0;
constexpr uint8_t kTerrainPlains = 1;
constexpr uint8_t kTerrainForest = 2;
constexpr uint8_t kMoveCostUnreachable = 0xFFu;

// PlayStatus / AiState / Proc field offsets (used by fixture helpers).
constexpr uint32_t kPlayStatusVisionOffset = 0x0Du;
constexpr uint32_t kPlayStatusFactionOffset = 0x0Fu;
constexpr uint32_t kPlayStatusTurnOffset = 0x10u;
constexpr uint32_t kAiStateDecideStepOffset = 0x79u;
constexpr uint32_t kAiStateDangerFilledOffset = 0x7Au;
constexpr uint32_t kAiStateRecoveryModeOffset = 0x7Bu;
constexpr uint32_t kAiStateHealThresholdOffset = 0x7Cu;
constexpr uint32_t kAiStateFlagsOffset = 0x7Du;
constexpr uint32_t kProcPayloadCallbackOffset = 0x2Cu;

// AI action IDs, recovery modes, and script opcodes.
constexpr uint8_t kAiActionWait = 0u;
constexpr uint8_t kAiActionAttack = 1u;
constexpr uint8_t kAiActionStaff = 5u;
constexpr uint8_t kAiActionUseItem = 6u;
constexpr uint8_t kAiRecoveryModeDefault = 0u;
constexpr uint8_t kAiRecoveryModeStationary = 2u;
constexpr uint8_t kAiScriptOpGoto = 0x03u;
constexpr uint8_t kAiScriptOpAttack = 0x05u;

// Deterministic per-test RNG seeds.
constexpr int kRngSeedMultiTurn = 0x1234;
constexpr int kRngSeedWeaponBreak = 0x5678;
constexpr int kRngSeedTacticalFog = 0xC0FFEE;
constexpr int kRngSeedEnemyAiTurn = 0xA1E2E01;

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

// Declarative options structs for test-only setup helpers (used with C++20
// designated initializers so omitted fields stay zero/defaulted cleanly under
// `-Wextra -Werror`).
struct ItemSpec {
  uint8_t id = 0;
  ItemKind kind = ItemKind::kItem;
  uint32_t attributes = 0;
  uint32_t bonuses_addr = 0;
  uint8_t max_uses = 0;
  uint8_t might = 0;
  uint8_t hit = 0;
  uint8_t weight = 0;
  uint8_t encoded_range = 0;
  uint8_t required_wexp = 0;
  uint8_t wexp_gain = 0;
};

struct JobSpec {
  uint8_t id = 0;
  int8_t mov = 5;
  int8_t con = 5;
  int8_t power_level = 2;
};

struct CharSpec {
  uint8_t id = 0;
  uint8_t default_jid = 0;
  uint8_t affinity = 0;
  uint32_t support_info_addr = 0;
  uint8_t growth = 50;
};

struct SupportPartnerSpec {
  uint8_t partner_pid = 0;
  uint8_t exp_base = 80;
  uint8_t exp_growth = 5;
};

struct AiScriptCmdSpec {
  uint8_t cmd = 0;
  uint8_t arg1 = 0;
  uint8_t arg2 = 0;
  uint8_t arg3 = 0;
};

struct UnitSetupOptions {
  uint8_t pid = 0;
  uint8_t jid = 0;
  int8_t level = 1;
  uint8_t exp = 0;
  int8_t x = 0;
  int8_t y = 0;
  int8_t hp = 18;
  int8_t pow = 0;
  int8_t skl = 0;
  int8_t spd = 0;
  int8_t def = 0;
  int8_t res = 0;
  int8_t lck = 0;
  ItemKind weapon_kind = ItemKind::kSword;
  uint8_t initial_wexp = 1;
  uint8_t weapon_iid = 0;
  uint8_t support0_exp = 0;
};

struct PrepareAiTurnOptions {
  uint8_t recovery_mode = kAiRecoveryModeDefault;
  bool clear_decision = true;
};

struct AiDecisionOptions {
  int16_t x_move = 0;
  int16_t y_move = 0;
  uint8_t action_id = kAiActionAttack;
  GbaPtr<Unit> target = {};
  uint8_t item_slot = 0;
};

// --- Single remaining graphics/VRAM seam ---
// RefreshUnitSprites (0x08025724) decompresses sprite sheets and queues VRAM
// DMA transfers, which are out of scope for a headless pure-logic skirmish.
uint32_t StubNoOpZero(uint32_t, uint32_t, uint32_t, uint32_t,
                      const uint32_t* absl_nullable) {
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
    RegisterNative(kAddrRefreshUnitSprites, StubNoOpZero);

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

  void InitRomTables() {
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

    // Terrain combat tables (65 entries, indexed by terrain ID):
    //   Wall (0): 0 avo, 0 def | Plains (1): 0 avo, 0 def | Forest (2): +20/+1.
    int8_t avo[65]{};
    int8_t def[65]{};
    int8_t res[65]{};
    avo[kTerrainForest] = 20;
    def[kTerrainForest] = 1;
    std::memcpy(rom_.data() + (kTerrainAvoAddr - kRomBase), avo, sizeof(avo));
    std::memcpy(rom_.data() + (kTerrainDefAddr - kRomBase), def, sizeof(def));
    std::memcpy(rom_.data() + (kTerrainResAddr - kRomBase), res, sizeof(res));

    // Foot movement cost table: -1 (0xFF) marks impassable tiles (including
    // outer border padding kTerrainWall=0); Plains=1, Forest=2.
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
        {.attacker_kind = -1,
         .defender_kind = 0,
         .hit_bonus = 0,
         .dmg_bonus = 0},
    };
    std::memcpy(rom_.data() + (kWeaponTriangleTableAddr - kRomBase), wta,
                sizeof(wta));

    // Support EXP thresholds (C=80, B=160, A=240) and Affinity combat bonuses.
    const uint32_t caps[4] = {80, 160, 240, 240};
    std::memcpy(rom_.data() + (kSupportLevelCapsAddr - kRomBase), caps,
                sizeof(caps));
    const SupportBonuses aff[3] = {
        // Affinity 1 (Fire): +1 atk, +5 hit/avo/crit per support rank.
        {.affinity = 1,
         .bonus_attack = 1,
         .bonus_defense = 0,
         .bonus_hit = 5,
         .bonus_avoid = 5,
         .bonus_crit = 5,
         .bonus_dodge = 0,
         .pad_07 = 0},
        // Affinity 2 (Wind): +1 atk, +5 hit/crit/dodge per support rank.
        {.affinity = 2,
         .bonus_attack = 1,
         .bonus_defense = 0,
         .bonus_hit = 5,
         .bonus_avoid = 0,
         .bonus_crit = 5,
         .bonus_dodge = 5,
         .pad_07 = 0},
        {},  // Sentinel (affinity == 0 terminates table).
    };
    std::memcpy(rom_.data() + (kAffinityBonusTableAddr - kRomBase), aff,
                sizeof(aff));

    // Lyn <-> Kent symmetric support partner tables (growth = 5 pts/turn).
    WriteSupportPartner(kLynSupportInfoAddr, {.partner_pid = kPidKent});
    WriteSupportPartner(kKentSupportInfoAddr, {.partner_pid = kPidLyn});

    // Weapons at kIInfoTableAddr: Iron Sword, Iron Lance, Iron Axe, Steel Axe.
    RegisterItem({.id = kIidIronSword,
                  .kind = ItemKind::kSword,
                  .attributes = kItemAttrWeapon,
                  .max_uses = 46,
                  .might = 5,
                  .hit = 90,
                  .weight = 5,
                  .encoded_range = 0x11u,
                  .required_wexp = 1,
                  .wexp_gain = 1});
    RegisterItem({.id = kIidIronLance,
                  .kind = ItemKind::kLance,
                  .attributes = kItemAttrWeapon,
                  .max_uses = 45,
                  .might = 7,
                  .hit = 80,
                  .weight = 8,
                  .encoded_range = 0x11u,
                  .required_wexp = 1,
                  .wexp_gain = 1});
    RegisterItem({.id = kIidIronAxe,
                  .kind = ItemKind::kAxe,
                  .attributes = kItemAttrWeapon,
                  .max_uses = 45,
                  .might = 8,
                  .hit = 75,
                  .weight = 10,
                  .encoded_range = 0x11u,
                  .required_wexp = 1,
                  .wexp_gain = 1});
    RegisterItem({.id = kIidSteelAxe,
                  .kind = ItemKind::kAxe,
                  .attributes = kItemAttrWeapon,
                  .max_uses = 30,
                  .might = 11,
                  .hit = 65,
                  .weight = 15,
                  .encoded_range = 0x11u,
                  .required_wexp = 1,
                  .wexp_gain = 1});

    // Angelic Robe: 1-use stat booster granting +7 max HP.
    ItemStatBonuses robe_bonuses{};
    robe_bonuses.hp = 7;
    WriteRom(kAngelicRobeBonusAddr, robe_bonuses);
    RegisterItem({.id = kIidAngelicRobe,
                  .kind = ItemKind::kItem,
                  .bonuses_addr = kAngelicRobeBonusAddr,
                  .max_uses = 1});

    // Classes at kJInfoTableAddr: Lord, Cavalier, Brigand.
    RegisterJob({.id = kJidLord, .mov = 5, .con = 5, .power_level = 3});
    RegisterJob({.id = kJidCavalier, .mov = 7, .con = 9, .power_level = 3});
    RegisterJob({.id = kJidBrigand, .mov = 5, .con = 12, .power_level = 2});

    // Characters at kPInfoTableAddr: Lyn, Kent, Brigand A, Brigand B.
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

    // AI combat score weights, class-rank groups, and friend-zone offset
    // tables. Each table ends with its FE7 sentinel (0x7F for combat, 9999 for
    // position).
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

    // Chapter event list & flag bitmask tables: point chapter 0's turn and
    // victory lists to an empty (0-terminated) list whose opcode 0 dispatches
    // to native `EventListEndHandler`, so BmMainChangePhase /
    // BmMainCheckVictory run 100% natively without falling back.
    constexpr uint32_t kChapterEventsPtrsAddr = 0x08010300u;
    constexpr uint32_t kEmptyEventListAddr = 0x08010320u;
    const uint8_t bit_masks[8] = {1, 2, 4, 8, 16, 32, 64, 128};
    std::memcpy(rom_.data() + (kFlagBitMaskTableAddr - kRomBase), bit_masks,
                sizeof(bit_masks));
    WriteRom(kEventListHandlerTableAddr, kAddrEventListEndHandler | 1u);
    WriteRom(kEventListHandlerTableAddr + 4u, uint32_t{1u});
    WriteRom(kChapterEventAssetTableAddr, kChapterEventsPtrsAddr);
    WriteRom(kChapterEventsPtrsAddr, kEmptyEventListAddr);
    WriteRom(kVictoryEventTableAddr + 0x0Cu, kEmptyEventListAddr);

    // Top-level Enemy AI ROM tables:
    //   - Faction unit capacities: Blue=62, Green=20, Red=50.
    //   - Recovery HP% thresholds indexed by AI recovery mode.
    //   - Nearby tile offsets: `AiCountEnemiesNearby` starts scanning at
    //     `kAiNearbyOffsetTableAddr - 4` (for `(0, 0)`) until sentinel 0x270F
    //     (9999).
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

  // Initializes an FE7 2D row-pointer map in EWRAM with explicit padding rows
  // and columns around the playable `kMapW x kMapH` grid so negative/overshoot
  // neighbor offsets stay safely in bounds.
  void InitPaddedMap(uint32_t buf_addr, uint32_t ptr_addr, uint8_t fill,
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

  void InitMaps() {
    GbaPtr<MapSize> sz(kMapSizeAddr);
    sz->x = kMapW;
    sz->y = kMapH;

    // Native `MapInit` allocates 2 padding rows/cols, which is enough for
    // 1-tile BFS neighbor probes. `gMapUnit` needs 4-tile padding because
    // Enemy AI friend-zone / nearby scans probe up to ±3 tiles without bounds
    // checks.
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

    // Synthetic 8x8 skirmish map (row-major `map[y][x]`):
    //   Legend: '.' = Plains (cost 1), '#' = Wall (impassable),
    //           'F' = Forest (cost 2, +20 avo, +1 def)
    //   Initial roster: L = Lyn (1,3), K = Kent (2,4),
    //                   A = Brigand A (3,4), B = Brigand B (5,4 in Forest)
    //
    //     x: 0 1 2 3 4 5 6 7
    //   y=0: . . . . . . . .
    //   y=1: . . . . . . . .
    //   y=2: . . . # . . . .   <- Wall at (3,2)
    //   y=3: . L . . F . . .   <- Lyn at (1,3), Forest at (4,3)
    //   y=4: . . K A . B . .   <- Kent (2,4), Brigand A (3,4), Brigand B
    //   (5,4/F) y=5: . . . . . . . . y=6: . . . . . . . . y=7: . . . . . . . .
    //
    // Fill the entire buffer (including outer padding) with `kTerrainWall`
    // first so BFS flooding stops cleanly at map boundaries without clamping.
    GbaPtr<GbaPtr<uint8_t>> terrain = TerrainMap();
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
    SetPhaseFaction(kFactionBlue);
    SetTurnNumber(1);
    // Mark danger map already filled so AiRefreshDangerMap doesn't wipe custom
    // values unless requested.
    SetDangerMapFilled(true);
    *GbaPtr<uint8_t>(kAiStateAddr + kAiStateFlagsOffset) = 0u;
    *GbaPtr<uint32_t>(kAiScoreCoeffPtrAddr) = kAiCombatScoreCoeffTableAddr;
  }

  void ConfigureUnit(int unit_id, const UnitSetupOptions& opts) {
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

  void InitRoster() {
    // Blue 1: Lyn (Lv 1, 90 EXP so one kill triggers a guaranteed level-up;
    // active C-support >= 81 with Kent).
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

    // Blue 2: Kent (Lv 2, positioned at (2, 4) to give Lyn C-support aura).
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

    // Red 0x81: Brigand A at (3, 4), weak enough for Lyn to double & KO.
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

    // Red 0x82: Brigand B in forest at (5, 4) with 12 HP (survives Turn 1,
    // attacks on Enemy Phase, then routed on Turn 2).
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

  // --- Test-only semantic helpers (keep E2E TEST_F bodies hex-free) ---

  static constexpr uint16_t MakeItem(uint8_t iid, uint8_t uses) {
    return static_cast<uint16_t>((static_cast<uint32_t>(uses) << 8) | iid);
  }

  static constexpr uint32_t ThumbAddr(uint32_t gba_addr) {
    return gba_addr | 1u;
  }

  GbaPtr<Unit> Lyn() const { return GetUnit(kUnitIdLyn); }
  GbaPtr<Unit> Kent() const { return GetUnit(kUnitIdKent); }
  GbaPtr<Unit> BrigandA() const { return GetUnit(kUnitIdBrigandA); }
  GbaPtr<Unit> BrigandB() const { return GetUnit(kUnitIdBrigandB); }

  // Moves `unit` to `(x, y)`, optionally marks its turn ended, and refreshes
  // `gMapUnit` so subsequent BFS / AI / support queries see the new tile.
  void PlaceUnit(GbaPtr<Unit> unit, int x, int y, bool end_turn = false) {
    unit->x = static_cast<int8_t>(x);
    unit->y = static_cast<int8_t>(y);
    if (end_turn) {
      unit->flags |= kUnitFlagTurnEnded;
    }
    SyncUnitMap();
  }

  GbaPtr<GbaPtr<uint8_t>> TerrainMap() const {
    return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr);
  }

  GbaPtr<GbaPtr<uint8_t>> MovementMap() const {
    return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr);
  }

  GbaPtr<GbaPtr<uint8_t>> RangeMap() const {
    return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapRangeAddr);
  }

  GbaPtr<GbaPtr<uint8_t>> FogMap() const {
    return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapFogAddr);
  }

  void SetFogVision(uint8_t vision) {
    *GbaPtr<uint8_t>(kMapPlayStatusAddr + kPlayStatusVisionOffset) = vision;
  }

  uint8_t GetPhaseFaction() const {
    return *GbaPtr<const uint8_t>(kPhasePlayStatusAddr +
                                  kPlayStatusFactionOffset);
  }

  void SetPhaseFaction(uint8_t faction) {
    *GbaPtr<uint8_t>(kPhasePlayStatusAddr + kPlayStatusFactionOffset) = faction;
  }

  uint16_t GetTurnNumber() const {
    return *GbaPtr<const uint16_t>(kPhasePlayStatusAddr +
                                   kPlayStatusTurnOffset);
  }

  void SetTurnNumber(uint16_t turn) {
    *GbaPtr<uint16_t>(kPhasePlayStatusAddr + kPlayStatusTurnOffset) = turn;
  }

  SupportBonuses ComputeSupportBonuses(GbaPtr<const Unit> unit) const {
    SupportBonuses out{};
    GetUnitSupportBonuses(unit,
                          GbaPtr<SupportBonuses>(kScratchSupportBonusesAddr));
    std::memcpy(&out, ewram_.data() + (kScratchSupportBonusesAddr - kEwramBase),
                sizeof(out));
    return out;
  }

  void SetActiveAiUnit(GbaPtr<Unit> unit) {
    *GbaPtr<GbaPtr<Unit>>(kAiActiveUnitAddr) = unit;
    *GbaPtr<uint8_t>(kAiActiveUnitIdAddr) = static_cast<uint8_t>(unit->id);
  }

  bool IsDangerMapFilled() const {
    return *GbaPtr<const uint8_t>(kAiStateAddr + kAiStateDangerFilledOffset) !=
           0u;
  }

  void SetDangerMapFilled(bool filled) {
    *GbaPtr<uint8_t>(kAiStateAddr + kAiStateDangerFilledOffset) =
        filled ? 1u : 0u;
  }

  void ResetAiDecideState(uint8_t recovery_mode = kAiRecoveryModeDefault) {
    *GbaPtr<uint8_t>(kAiStateAddr + kAiStateDecideStepOffset) = 0u;
    *GbaPtr<uint8_t>(kAiStateAddr + kAiStateRecoveryModeOffset) = recovery_mode;
  }

  // Configures `unit` as the active Enemy AI unit, resets the CpDecide step &
  // recovery mode in `gAiState`, and optionally clears `gAiDecision`.
  void PrepareAiTurn(GbaPtr<Unit> unit, const PrepareAiTurnOptions& opts = {}) {
    SetActiveAiUnit(unit);
    ResetAiDecideState(opts.recovery_mode);
    if (opts.clear_decision) {
      AiClearDecision();
    }
  }

  // Clears `gAiDecision` and populates a new AI decision record from `opts`.
  void RecordAiDecision(const AiDecisionOptions& opts) {
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

  void SetAiHealThreshold(uint8_t percent) {
    *GbaPtr<uint8_t>(kAiStateAddr + kAiStateHealThresholdOffset) = percent;
  }

  bool FindBestAiPosition(AiPos* absl_nonnull out) {
    GbaPtr<AiPos> scratch(kScratchBestPosAddr);
    *scratch = {};
    const bool found = AiFindBestPositionInRange(scratch);
    *out = *scratch;
    return found;
  }

  uint32_t ScoreAiCombat(uint8_t x, uint8_t y, GbaPtr<const Unit> target,
                         uint16_t item_slot = 0) {
    GbaPtr<AiCombatSimulationSt> sim_st(kScratchCombatSimAddr);
    *sim_st = {};
    sim_st->x_move = x;
    sim_st->y_move = y;
    sim_st->target_id = static_cast<uint8_t>(target->id);
    sim_st->item_slot = item_slot;
    AiComputeCombatScore(sim_st);
    return sim_st->score;
  }

  uint32_t GetProcDispatchedCallback(GbaPtr<Proc> proc) const {
    return *GbaPtr<const uint32_t>(proc.addr + kProcPayloadCallbackOffset);
  }

  // Registers lightweight native shims for 6 non-AI helpers called during
  // `CpDecideMain` / `AiTrySpecialAction` (`SetWorkingBmMap`,
  // `MapAddInBoundedRange`, `MapClearOther`, `MapFloodRangeExtend`,
  // `RemoveUnitFromBallista`, `BattleGenerateSimulation`), keeping the entire
  // enemy turn 100% pure-native with zero ARM7TDMI fallback.
  void RegisterEnemyAiTurnShims() {
    RegisterNative(kAddrSetWorkingBmMap,
                   [](uint32_t r0, uint32_t, uint32_t, uint32_t,
                      const uint32_t* absl_nullable) -> uint32_t {
                     *GbaPtr<uint32_t>(kWorkingMapAddr) = r0;
                     return 0u;
                   });
    RegisterNative(kAddrMapAddInBoundedRange,
                   [](uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                      const uint32_t* absl_nullable) -> uint32_t {
                     const int x = static_cast<int16_t>(r0);
                     const int y = static_cast<int16_t>(r1);
                     const int min_r = static_cast<int16_t>(r2);
                     const int max_r = static_cast<int16_t>(r3);
                     MapAddInRange(x, y, max_r, 1);
                     if (min_r > 1) {
                       MapAddInRange(x, y, min_r - 1, -1);
                     }
                     return 0u;
                   });
    RegisterNative(kAddrMapClearOther, StubNoOpZero);
    RegisterNative(kAddrMapFloodRangeExtend, StubNoOpZero);
    RegisterNative(kAddrRemoveUnitFromBallista, StubNoOpZero);
    RegisterNative(kAddrBattleGenerateSimulation,
                   [](uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                      const uint32_t* absl_nullable) -> uint32_t {
                     GbaPtr<BattleUnit> actor(kBattleActorAddr);
                     GbaPtr<BattleUnit> target(kBattleTargetAddr);
                     InitBattleUnit(actor, GbaPtr<const Unit>(r0));
                     InitBattleUnit(target, GbaPtr<const Unit>(r1));
                     ComputeBattleUnitStats(actor, target);
                     ComputeBattleUnitStats(target, actor);
                     return 0u;
                   });
  }

  void RegisterItem(const ItemSpec& spec) {
    IInfo info{};
    info.id = spec.id;
    info.kind = static_cast<uint8_t>(spec.kind);
    info.attributes = spec.attributes;
    info.bonuses = GbaPtr<const ItemStatBonuses>(spec.bonuses_addr);
    info.max_uses = spec.max_uses;
    info.might = spec.might;
    info.hit = spec.hit;
    info.weight = spec.weight;
    info.encoded_range = spec.encoded_range;
    info.required_wexp = spec.required_wexp;
    info.wexp_gain = spec.wexp_gain;
    WriteRom(kIInfoTableAddr + static_cast<uint32_t>(spec.id) * sizeof(IInfo),
             info);
  }

  void RegisterJob(const JobSpec& spec) {
    JInfo j{};
    j.id = spec.id;
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
    j.mov_table[0] = GbaPtr<const int8_t>(kMovTableFootAddr);
    j.avo_terrain_table = GbaPtr<const int8_t>(kTerrainAvoAddr);
    j.def_terrain_table = GbaPtr<const int8_t>(kTerrainDefAddr);
    j.res_terrain_table = GbaPtr<const int8_t>(kTerrainResAddr);
    WriteRom(kJInfoTableAddr + static_cast<uint32_t>(spec.id) * sizeof(JInfo),
             j);
  }

  void RegisterChar(const CharSpec& spec) {
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
    p.support_info = GbaPtr<const SupportInfo>(spec.support_info_addr);
    WriteRom(kPInfoTableAddr + static_cast<uint32_t>(spec.id) * sizeof(PInfo),
             p);
  }

  void WriteSupportPartner(uint32_t rom_addr, const SupportPartnerSpec& spec) {
    SupportInfo sup{};
    sup.pids[0] = spec.partner_pid;
    sup.exp_base[0] = spec.exp_base;
    sup.exp_growth[0] = spec.exp_growth;
    sup.count = 1;
    WriteRom(rom_addr, sup);
  }

  void WriteAiScriptCmd(uint32_t rom_addr, const AiScriptCmdSpec& spec) {
    AiScriptCmd cmd{};
    cmd.cmd = spec.cmd;
    cmd.arg1 = spec.arg1;
    cmd.arg2 = spec.arg2;
    cmd.arg3 = spec.arg3;
    WriteRom(rom_addr, cmd);
  }

  // Populates synthetic ROM tables driving the 3-stage `CpDecide` pipeline:
  //   1. `kAiDecideFuncsAddr`: Suspend/Recover -> Script A -> Script B -> null.
  //   2. `Script A[0]`: 2-instruction loop `[Attack (100%), Goto 0]`.
  //   3. Staff action table (`Heal` -> `AiTryHealStaff`) + healing item defs.
  //   4. Cardinal adjacent tile offsets `{E, W, S, N}` for staff targeting.
  void InitEnemyAiTurnRomTables() {
    WriteRom<uint32_t>(kAiDecideFuncsAddr + 0u,
                       ThumbAddr(kAddrCpDecideSuspendOrRecover));
    WriteRom<uint32_t>(kAiDecideFuncsAddr + 4u,
                       ThumbAddr(kAddrCpDecideScriptA));
    WriteRom<uint32_t>(kAiDecideFuncsAddr + 8u,
                       ThumbAddr(kAddrCpDecideScriptB));
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
    WriteRom<uint32_t>(kAiScriptOpcodeTableAddr +
                           static_cast<uint32_t>(kAiScriptOpAttack) * 4u,
                       ThumbAddr(kAddrAiScriptCmdAttack));

    // Staff & recovery item definitions: Heal staff (1-1 range) and Vulnerary.
    RegisterItem({.id = kIidHealStaff,
                  .kind = ItemKind::kStaff,
                  .attributes = kItemAttrStaff,
                  .encoded_range = 0x11u,
                  .required_wexp = 1});
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

  std::vector<uint8_t> ewram_;
  std::vector<uint8_t> iwram_;
  std::vector<uint8_t> rom_;
};

TEST_F(SkirmishIntegrationTest, FullMultiTurnCombatLevel) {
  RandInit(kRngSeedMultiTurn);

  GbaPtr<Unit> lyn = Lyn();
  GbaPtr<Unit> kent = Kent();
  GbaPtr<Unit> brigand_a = BrigandA();
  GbaPtr<Unit> brigand_b = BrigandB();

  // --- Turn 1, Player Phase (Blue) ---
  EXPECT_EQ(GetCurrentPhase(), kFactionBlue);
  EXPECT_EQ(CountUnitsByFaction(kFactionBlue), 2);
  EXPECT_EQ(CountUnitsByFaction(kFactionRed), 2);

  // 1. BFS movement flood for Lyn (5 mov): wall at (3,2) is unreachable,
  //    plains tile (2,3) next to Kent & Brigand A is reachable at cost 1.
  //
  //      x: 0 1 2 3 4 5
  //    y=2: . . . # . .   <- Wall (3,2) blocks direct north route
  //    y=3: . . . L F .   <- Lyn moves (1,3) -> (3,3) (dist 2 from Kent)
  //    y=4: . . K A . B   <- Kent (2,4) gives C-support aura; A at (3,4)
  MapFloodUnit(GbaPtr<const Unit>(lyn.addr));
  GbaPtr<GbaPtr<uint8_t>> mov_map = MovementMap();
  EXPECT_EQ(mov_map[2][3], kMoveCostUnreachable);  // Wall tile blocked.
  EXPECT_LE(mov_map[3][2], 5u);                    // (2,3) reachable.
  EXPECT_EQ(mov_map[3][2], 1u);

  // Move Lyn to (3, 3) to attack Brigand A at (3, 4) while within 2 tiles of
  // Kent (2, 4) for C-support aura.
  ASSERT_LE(mov_map[3][3], 5u);
  PlaceUnit(lyn, 3, 3);

  // 2. Verify live Wind+Fire C-support aura from Kent (distance = 2 <= 3).
  const SupportBonuses lyn_bonuses =
      ComputeSupportBonuses(GbaPtr<const Unit>(lyn.addr));
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
  PlaceUnit(kent, 4, 4, /*end_turn=*/true);

  // 4. Phase switch: Blue -> Red. Blue units are refreshed.
  EXPECT_EQ(BmMainChangePhase(), 1);
  EXPECT_EQ(GetCurrentPhase(), kFactionRed);
  EXPECT_EQ(lyn->flags & kUnitFlagTurnEnded, 0u);
  EXPECT_EQ(kent->flags & kUnitFlagTurnEnded, 0u);

  // --- Turn 1, Enemy Phase (Red) ---
  // 5. Brigand B refreshes the AI danger map natively (`AiFillDangerMap`),
  //    runs movement flood, evaluates position & combat score against Kent,
  //    records gAiDecision, and attacks Kent.
  SetActiveAiUnit(brigand_b);
  SetDangerMapFilled(false);
  AiRefreshDangerMap();
  EXPECT_TRUE(IsDangerMapFilled());
  EXPECT_EQ(BmMainCheckVictory(), 1);

  MapFloodUnit(GbaPtr<const Unit>(brigand_b.addr));
  GbaPtr<GbaPtr<uint8_t>> rng_map = RangeMap();
  MapFill(rng_map, 0);
  // Tiles adjacent to Kent (4, 4): (5, 4) is forest (bonus def/avo).
  rng_map[4][5] = 1;
  rng_map[3][4] = 1;

  AiPos best_pos{};
  ASSERT_TRUE(FindBestAiPosition(&best_pos));
  EXPECT_EQ(best_pos.x, 5);
  EXPECT_EQ(best_pos.y, 4);  // Forest tile (5, 4) preferred.

  // Score Brigand B vs. Kent using simulated battle stats.
  RunCombatExchange(brigand_b, kent, kBattleConfigSimulate);
  const uint32_t score = ScoreAiCombat(static_cast<uint8_t>(best_pos.x),
                                       static_cast<uint8_t>(best_pos.y),
                                       GbaPtr<const Unit>(kent.addr));
  EXPECT_GT(score, 0u);

  RecordAiDecision({
      .x_move = best_pos.x,
      .y_move = best_pos.y,
      .action_id = kAiActionAttack,
      .target = kent,
  });
  GbaPtr<const AiDecision> dec(kAiDecisionAddr);
  EXPECT_EQ(dec->unit_target_id, static_cast<uint8_t>(kent->id));
  EXPECT_EQ(dec->action_performed, 1);

  // Execute Brigand B's attack on Kent.
  RunCombatExchange(brigand_b, kent, kBattleConfigReal);
  brigand_b->flags |= kUnitFlagTurnEnded;

  // 6. Advance Red -> Green -> Blue (Turn 1 -> Turn 2).
  //    Position Lyn adjacent to Kent before turn wrap to verify
  //    ProcessSupportGains() runs on Turn 2 start.
  PlaceUnit(lyn, 4, 3);
  const uint8_t sup_before = lyn->supports[0];

  EXPECT_EQ(BmMainChangePhase(), 1);
  EXPECT_EQ(GetPhaseFaction(), kFactionGreen);
  EXPECT_EQ(BmMainChangePhase(), 1);
  EXPECT_EQ(GetPhaseFaction(), kFactionBlue);
  EXPECT_EQ(GetCurrentPhase(), kFactionBlue);
  EXPECT_EQ(GetTurnNumber(), 2u);
  // Adjacent Lyn & Kent gained +5 support EXP on Turn 2 Blue phase start.
  EXPECT_EQ(lyn->supports[0], sup_before + 5u);

  // --- Turn 2, Player Phase: Rout remaining Brigand B ---
  if ((brigand_b->flags & kUnitFlagDead) == 0u) {
    // Lyn moves adjacent to Brigand B (5, 4) and finishes it off.
    PlaceUnit(lyn, 5, 3);
    RunCombatExchange(lyn, brigand_b, kBattleConfigReal);
  }
  EXPECT_EQ(CountUnitsByFaction(kFactionRed), 0);
  EXPECT_GT(CountUnitsByFaction(kFactionBlue), 0);

  // Zero un-handled ARM7TDMI fallback calls occurred during the entire battle.
  EXPECT_TRUE(GetFallbackReport().empty());
}

TEST_F(SkirmishIntegrationTest, BattleForecastDoesNotMutateUnits) {
  GbaPtr<Unit> lyn = Lyn();
  GbaPtr<Unit> brigand_a = BrigandA();
  PlaceUnit(lyn, 3, 3);

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
  RandInit(kRngSeedWeaponBreak);
  GbaPtr<Unit> lyn = Lyn();
  GbaPtr<Unit> kent = Kent();
  GbaPtr<Unit> brigand_a = BrigandA();

  // Give Lyn a 1-use Iron Sword so it breaks on her first strike.
  lyn->items[0] = MakeItem(kIidIronSword, 1);
  PlaceUnit(lyn, 3, 3);

  RunCombatExchange(lyn, brigand_a, kBattleConfigReal);
  // Broken weapon is automatically removed from inventory by
  // UpdateUnitFromBattle -> UnitRemoveInvalidItems.
  EXPECT_EQ(lyn->items[0], 0u);

  // Advance to Turn 2 (FE7 skips support EXP on Turn 1) and place Lyn & Kent
  // adjacent at 155 support EXP (5 shy of B-support threshold 160).
  SetTurnNumber(2);
  lyn->supports[0] = 155;
  kent->supports[0] = 155;
  PlaceUnit(lyn, 2, 3);
  PlaceUnit(kent, 2, 4);

  EXPECT_FALSE(CanUnitSupportNow(GbaPtr<const Unit>(lyn.addr), 0));
  ProcessSupportGains();
  EXPECT_EQ(lyn->supports[0], 160u);
  EXPECT_TRUE(CanUnitSupportNow(GbaPtr<const Unit>(lyn.addr), 0));

  UnitGainSupportLevel(lyn, 0);
  EXPECT_EQ(GetSupportLevelBySupportIndex(GbaPtr<const Unit>(lyn.addr), 0),
            2);  // B-rank achieved!
  EXPECT_TRUE(GetFallbackReport().empty());
}

TEST_F(SkirmishIntegrationTest, TacticalFogTrapsActionsAndTopLevelAi) {
  RandInit(kRngSeedTacticalFog);
  GbaPtr<Unit> lyn = Lyn();
  GbaPtr<Unit> kent = Kent();
  GbaPtr<Unit> brigand_a = BrigandA();
  GbaPtr<Unit> brigand_b = BrigandB();

  // --- 1. Traps, Light Rune & Fog of War (game/map/trap) ---
  // Enable Fog of War (vision = 3) during Blue Phase.
  SetFogVision(3);
  SetPhaseFaction(kFactionBlue);

  GbaPtr<Trap> rune = AddLightRune(2, 2);
  ASSERT_TRUE(static_cast<bool>(rune));
  ApplyLightRuneTerrain();
  EXPECT_EQ(TerrainMap()[2][2],
            kTerrainWall);  // Light Rune sets tile impassable (0)

  GbaPtr<Trap> fire = AddFireTrap(4, 3, /*turn_delay=*/1, /*interval=*/2);
  ASSERT_TRUE(static_cast<bool>(fire));
  EXPECT_EQ(GetTrapAt(4, 3).addr, fire.addr);

  RefreshEntityMaps();
  GbaPtr<GbaPtr<uint8_t>> fog = FogMap();
  EXPECT_GT(fog[lyn->y][lyn->x], 0u);  // Blue unit illuminates its own tile
  EXPECT_EQ(fog[7][7], 0u);            // Far corner remains shrouded

  // Tick and reset damaging traps, then clean up Light Rune.
  TickDamagingTraps();
  ResetExpiredDamagingTraps();
  RemoveTrap(rune);
  EXPECT_FALSE(static_cast<bool>(GetTrapAt(2, 2)));
  TerrainMap()[2][2] = kTerrainPlains;

  // Disable fog for the action/AI portion.
  SetFogVision(0);
  RefreshEntityMaps();

  // --- 2. Unit Actions, Stat Boosters, Autolevel & Rescue/Drop ---
  // Give Lyn an Angelic Robe (+7 HP) in slot 1 and apply it.
  const int8_t lyn_max_hp_before = lyn->max_hp;
  lyn->items[1] = MakeItem(kIidAngelicRobe, 1);
  EXPECT_EQ(FindUnitItemSlot(GbaPtr<const Unit>(lyn.addr), kIidAngelicRobe), 1);
  ApplyItemStatBoost(lyn, 1);
  EXPECT_EQ(lyn->max_hp, lyn_max_hp_before + 7);
  EXPECT_EQ(lyn->items[1], 0u);  // 1-use stat booster consumed

  // Bonus-level Brigand B by 3 levels and verify HP/stats remain valid.
  const int8_t b_hp_before = brigand_b->max_hp;
  UnitApplyBonusLevels(brigand_b, 3);
  EXPECT_GE(brigand_b->max_hp, b_hp_before);

  // Kent rescues Lyn and drops her onto an adjacent tile (1, 4).
  ASSERT_TRUE(CanUnitCarry(GbaPtr<const Unit>(kent.addr),
                           GbaPtr<const Unit>(lyn.addr)));
  UnitRescue(kent, lyn);
  EXPECT_EQ(kent->rescue, static_cast<uint8_t>(lyn->id));
  EXPECT_NE(lyn->flags & kUnitFlagRescued, 0u);

  UnitDropRescue(kent, 1, 4);
  EXPECT_EQ(kent->rescue, 0u);
  EXPECT_EQ(lyn->flags & kUnitFlagRescued, 0u);
  EXPECT_EQ(lyn->x, 1);
  EXPECT_EQ(lyn->y, 4);
  SyncUnitMap();

  // --- 3. Top-Level Enemy AI (Recovery, Threat Auto-Equip, Nearby, Move) ---
  // Switch to Red Phase and wound Brigand A below 50% HP so it enters recovery.
  SetPhaseFaction(kFactionRed);
  brigand_a->hp = 5;
  brigand_b->hp = brigand_b->max_hp;
  AiUpdateFactionRecovery();
  EXPECT_NE(brigand_a->ai_flags & 1u, 0u);
  EXPECT_EQ(brigand_b->ai_flags & 1u, 0u);

  // Equip Brigand B with Iron Axe (Mt 8, slot 0) and Steel Axe (Mt 11, slot 1)
  // within melee threat of Kent; AiEquipBestItem should swap Steel Axe to 0.
  brigand_b->items[0] = MakeItem(kIidIronAxe, 45);
  brigand_b->items[1] = MakeItem(kIidSteelAxe, 30);
  PlaceUnit(brigand_b, 3, 4);
  SetActiveAiUnit(brigand_b);

  GbaPtr<AiDecision> dec(kAiDecisionAddr);
  AiClearDecision();
  dec->x_move = 2;
  dec->y_move = 4;
  dec->action_id = kAiActionWait;

  EXPECT_TRUE(AiCanEquipBestItem());
  AiEquipBestItem();
  EXPECT_EQ(GetItemIid(brigand_b->items[0]), kIidSteelAxe);
  EXPECT_GE(AiCountEnemiesNearby(2, 4), 1);

  // Verify random movement chooses a reachable unoccupied tile.
  AiClearDecision();
  AiTryMoveRandomly();
  EXPECT_EQ(dec->action_performed, 1);

  // Verify KillUnit removes Brigand A from active Red count.
  const int red_before = CountUnitsByFaction(kFactionRed);
  KillUnit(brigand_a);
  EXPECT_EQ(CountUnitsByFaction(kFactionRed), red_before - 1);

  // Zero un-handled ARM7TDMI fallback calls across all 14 subsystems!
  EXPECT_TRUE(GetFallbackReport().empty());
}

TEST_F(SkirmishIntegrationTest, FullEnemyAiTurnDecisionAndExecution) {
  RandInit(kRngSeedEnemyAiTurn);
  RegisterEnemyAiTurnShims();
  InitEnemyAiTurnRomTables();

  GbaPtr<Unit> lyn = Lyn();
  GbaPtr<Unit> brigand_a = BrigandA();
  GbaPtr<Unit> brigand_b = BrigandB();
  GbaPtr<AiDecision> dec(kAiDecisionAddr);
  GbaPtr<Proc> proc(kProcsAddr);

  // Switch to Red (Enemy) phase.
  SetPhaseFaction(kFactionRed);

  // --- Phase A: Wounded enemy recovers with Vulnerary via CpDecideMain ---
  brigand_a->hp = 4;  // Below 50% HP -> recovery mode
  brigand_a->items[1] = MakeItem(kIidVulnerary, 3);
  PrepareAiTurn(brigand_a, {.recovery_mode = kAiRecoveryModeStationary});

  CpDecideMain();
  EXPECT_EQ(dec->action_performed, 1);
  EXPECT_EQ(dec->action_id, kAiActionUseItem);
  EXPECT_EQ(dec->item_slot, 1u);

  CpPerformDispatchAction(proc);
  EXPECT_EQ(GetProcDispatchedCallback(proc), ThumbAddr(kAddrCpPerformUseItem));

  // --- Phase B: Healthy enemy executes Script A attack + combat sim ---
  //   Row y=3 layout:  . . L . B . . .   (Lyn at (2,3), Brigand B at (4,3))
  brigand_b->hp = brigand_b->max_hp;
  brigand_b->ai_a = 0u;
  brigand_b->ai_a_pc = 0u;
  PlaceUnit(lyn, 2, 3);
  PlaceUnit(brigand_b, 4, 3);
  PrepareAiTurn(brigand_b);

  CpDecideMain();
  EXPECT_EQ(dec->action_performed, 1);
  EXPECT_EQ(dec->action_id, kAiActionAttack);
  EXPECT_EQ(dec->unit_target_id, static_cast<uint8_t>(lyn->id));

  // --- Phase C: Staff healer dispatches Heal staff via AiTrySpecialAction ---
  //   Row y=3 layout:  . . L . B A . .   (wounded Brigand A adjacent at (5,3))
  brigand_a->ai_flags |= 1u;  // Flagged for recovery
  brigand_b->wexp[static_cast<int>(ItemKind::kStaff)] = 251u;  // S-rank staves
  brigand_b->items[1] = MakeItem(kIidHealStaff, 30);
  PlaceUnit(brigand_a, 5, 3);

  SetAiHealThreshold(80);
  AiClearDecision();
  EXPECT_TRUE(AiTrySpecialAction(ThumbAddr(kAddrAiIsUnitEnemy)));
  EXPECT_EQ(dec->action_performed, 1);
  EXPECT_EQ(dec->action_id, kAiActionStaff);
  EXPECT_EQ(dec->unit_target_id, static_cast<uint8_t>(brigand_a->id));

  CpPerformDispatchAction(proc);
  EXPECT_EQ(GetProcDispatchedCallback(proc), ThumbAddr(kAddrCpPerformStaff));

  // --- Phase D: CpPerformActionLoop syncs unit coords and breaks proc ---
  dec->action_id = kAiActionWait;
  dec->x_move = 3u;
  dec->y_move = 4u;
  CpPerformDispatchAction(proc);
  EXPECT_EQ(GetProcDispatchedCallback(proc), ThumbAddr(kAddrCpPerformWaitDone));
  CpPerformActionLoop(proc);
  EXPECT_EQ(brigand_b->x, 3);
  EXPECT_EQ(brigand_b->y, 4);

  // Zero un-handled ARM7TDMI fallback calls across the full AI turn!
  EXPECT_TRUE(GetFallbackReport().empty());
}

}  // namespace
}  // namespace fe7
