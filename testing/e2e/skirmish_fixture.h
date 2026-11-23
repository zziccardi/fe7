// Shared pure-native FE7 E2E test fixture and semantic helpers.
//
// Provides `fe7::e2e::SkirmishIntegrationTest`, a zero-ROM / zero-fallback
// synthetic 8x8 tactical test harness wiring all decompiled FE7 gameplay
// subsystems together, along with declarative options structs, an executable
// 8x8 ASCII terrain loader (`LoadTerrainFromAscii`), automatic zero-fallback
// enforcement in `TearDown()`, and diagnostic map/decision formatters so E2E
// `TEST_F` bodies remain concise and free of direct hexadecimal literals.

#ifndef FE7_TESTING_E2E_SKIRMISH_FIXTURE_H_
#define FE7_TESTING_E2E_SKIRMISH_FIXTURE_H_

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/nullability.h"
#include "game/ai/ai.h"
#include "game/battle.h"
#include "game/item.h"
#include "game/proc.h"
#include "game/support/support.h"
#include "game/unit.h"
#include "gtest/gtest.h"
#include "runtime/gba_ptr.h"

namespace fe7::e2e {

// Synthetic ROM layout offsets (all inside 0x08000000..0x08FFFFFF).
inline constexpr uint32_t kRomBase = 0x08000000u;
inline constexpr uint32_t kRomSize = 0x01000000u;
inline constexpr uint32_t kTerrainAvoAddr = 0x08010000u;
inline constexpr uint32_t kTerrainDefAddr = 0x08010080u;
inline constexpr uint32_t kTerrainResAddr = 0x08010100u;
inline constexpr uint32_t kMovTableFootAddr = 0x08010180u;
inline constexpr uint32_t kLynSupportInfoAddr = 0x08010200u;
inline constexpr uint32_t kKentSupportInfoAddr = 0x08010220u;
inline constexpr uint32_t kJobRankGroup0Addr = 0x08010280u;
inline constexpr uint32_t kJobRankGroup1Addr = 0x08010290u;
inline constexpr uint32_t kAngelicRobeBonusAddr = 0x080102A0u;
inline constexpr uint32_t kScriptATableAddr = 0x08044F00u;
inline constexpr uint32_t kScriptA0Addr = 0x08045000u;
inline constexpr uint32_t kSyntheticProcScrAddr = 0x08046000u;

// Synthetic EWRAM map buffers (each 0x1000 bytes, well away from unit arrays).
inline constexpr uint32_t kEwramBase = 0x02000000u;
inline constexpr uint32_t kTerrainBufAddr = 0x02010000u;
inline constexpr uint32_t kUnitMapBufAddr = 0x02011000u;
inline constexpr uint32_t kMovMapBufAddr = 0x02012000u;
inline constexpr uint32_t kRngMapBufAddr = 0x02013000u;
inline constexpr uint32_t kOtherMapBufAddr = 0x02014000u;
inline constexpr uint32_t kFogMapBufAddr = 0x02015000u;
inline constexpr uint32_t kHiddenMapBufAddr = 0x02016000u;

// Synthetic EWRAM scratch slots (used internally by fixture helpers).
inline constexpr uint32_t kScratchSupportBonusesAddr = 0x02018100u;
inline constexpr uint32_t kScratchBestPosAddr = 0x02018200u;
inline constexpr uint32_t kScratchCombatSimAddr = 0x02018220u;

// Synthetic roster unit IDs.
inline constexpr int kUnitIdLyn = 1;
inline constexpr int kUnitIdKent = 2;
inline constexpr int kUnitIdExtraBlue1 = 3;
inline constexpr int kUnitIdExtraBlue2 = 4;
inline constexpr int kUnitIdBrigandA = 0x81;
inline constexpr int kUnitIdBrigandB = 0x82;

// Synthetic character / class / item IDs.
inline constexpr uint8_t kPidLyn = 1;
inline constexpr uint8_t kPidKent = 2;
inline constexpr uint8_t kPidFlorina = 3;
inline constexpr uint8_t kPidFiora = 4;
inline constexpr uint8_t kPidBrigandA = 10;
inline constexpr uint8_t kPidBrigandB = 11;

inline constexpr uint8_t kJidLord = 1;
inline constexpr uint8_t kJidCavalier = 2;
inline constexpr uint8_t kJidBrigand = 3;
inline constexpr uint8_t kJidBladeLord = 4;
inline constexpr uint8_t kJidAssassin = 5;
inline constexpr uint8_t kJidPegasusKnight = 6;

inline constexpr uint8_t kIidIronSword = 1;
inline constexpr uint8_t kIidIronLance = 2;
inline constexpr uint8_t kIidIronAxe = 3;
inline constexpr uint8_t kIidSteelAxe = 4;
inline constexpr uint8_t kIidAngelicRobe = 5;
inline constexpr uint8_t kIidLancereaver = 6;
inline constexpr uint8_t kIidLuna = 7;
inline constexpr uint8_t kIidHealStaff = 0x4Au;
inline constexpr uint8_t kIidVulnerary = 0x6Bu;

inline constexpr int kMapW = 8;
inline constexpr int kMapH = 8;

// Terrain IDs and movement sentinels used on the synthetic 8x8 skirmish map.
inline constexpr uint8_t kTerrainWall = 0;
inline constexpr uint8_t kTerrainPlains = 1;
inline constexpr uint8_t kTerrainForest = 2;
inline constexpr uint8_t kMoveCostUnreachable = 0xFFu;

// Weapon rank / support rank / AI flag constants for readable test assertions.
inline constexpr uint8_t kWexpERank = 1u;
inline constexpr uint8_t kWexpSRank = 251u;
inline constexpr int kSupportRankC = 1;
inline constexpr int kSupportRankB = 2;
inline constexpr int kSupportRankA = 3;
inline constexpr uint8_t kAiFlagRecovery = 1u;
inline constexpr uint8_t kEncodedRange1To1 = 0x11u;
inline constexpr uint8_t kEncodedRange1To2 = 0x12u;

// PlayStatus / AiState / Proc field offsets (private to fixture helpers).
inline constexpr uint32_t kPlayStatusVisionOffset = 0x0Du;
inline constexpr uint32_t kPlayStatusFactionOffset = 0x0Fu;
inline constexpr uint32_t kPlayStatusTurnOffset = 0x10u;
inline constexpr uint32_t kPlayStatusConfig2Offset = 0x41u;
inline constexpr uint32_t kAiStateDecideStepOffset = 0x79u;
inline constexpr uint32_t kAiStateDangerFilledOffset = 0x7Au;
inline constexpr uint32_t kAiStateRecoveryModeOffset = 0x7Bu;
inline constexpr uint32_t kAiStateHealThresholdOffset = 0x7Cu;
inline constexpr uint32_t kAiStateFlagsOffset = 0x7Du;
inline constexpr uint32_t kProcPayloadCallbackOffset = 0x2Cu;

// AI action IDs, recovery modes, and script opcodes.
inline constexpr uint8_t kAiActionWait = 0u;
inline constexpr uint8_t kAiActionAttack = 1u;
inline constexpr uint8_t kAiActionStaff = 5u;
inline constexpr uint8_t kAiActionUseItem = 6u;
inline constexpr uint8_t kAiRecoveryModeDefault = 0u;
inline constexpr uint8_t kAiRecoveryModeStationary = 2u;
inline constexpr uint8_t kAiScriptOpGoto = 0x03u;
inline constexpr uint8_t kAiScriptOpAttack = 0x05u;

// Deterministic per-test RNG seeds.
inline constexpr int kRngSeedMultiTurn = 0x1234;
inline constexpr int kRngSeedWeaponBreak = 0x5678;
inline constexpr int kRngSeedSpecialCombat = 0x9ABC;
inline constexpr int kRngSeedTacticalFog = 0xC0FFEE;
inline constexpr int kRngSeedStatusFlow = 0x57A705;
inline constexpr int kRngSeedEnemyAiTurn = 0xA1E2E01;
inline constexpr int kRngSeedProcLoop = 0x6C0001;

// Converts a mutable `GbaPtr<T>` to `GbaPtr<const T>` without `.addr` noise.
template <typename T>
constexpr GbaPtr<const T> AsConst(GbaPtr<T> ptr) {
  return GbaPtr<const T>(ptr.addr);
}

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
  uint8_t crit = 0;
  uint8_t encoded_range = 0;
  uint8_t required_wexp = 0;
  uint8_t wexp_gain = 0;
};

struct JobSpec {
  uint8_t id = 0;
  uint8_t promote_jid = 0;
  int8_t mov = 5;
  int8_t con = 5;
  int8_t power_level = 2;
  uint8_t promotion_hp = 0;
  uint8_t promotion_pow = 0;
  uint8_t promotion_skl = 0;
  uint8_t promotion_spd = 0;
  uint8_t promotion_def = 0;
  uint8_t promotion_res = 0;
  uint32_t attributes = 0;
  uint8_t base_wexp_sword = 0;
  uint8_t base_wexp_lance = 0;
};

struct CharSpec {
  uint8_t id = 0;
  uint8_t default_jid = 0;
  uint8_t affinity = 0;
  uint32_t support_info_addr = 0;
  uint8_t growth = 50;
  uint32_t attributes = 0;
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
  uint8_t initial_wexp = kWexpERank;
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

struct AiDecisionExpectation {
  uint8_t action_id = kAiActionWait;
  std::optional<GbaPtr<Unit>> target = std::nullopt;
  std::optional<uint8_t> item_slot = std::nullopt;
};

// Base GoogleTest fixture for pure-native FE7 E2E tactical scenarios.
// Automatically asserts zero unhandled ARM7TDMI fallback calls in `TearDown()`.
class SkirmishIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override;
  void TearDown() override;

  template <typename T>
  void WriteRom(uint32_t gba_addr, const T& value) {
    std::memcpy(rom_.data() + (gba_addr - kRomBase), &value, sizeof(T));
  }

  static constexpr uint16_t MakeItem(uint8_t iid, uint8_t uses) {
    return static_cast<uint16_t>((static_cast<uint32_t>(uses) << 8) | iid);
  }

  static constexpr uint32_t ThumbAddr(uint32_t gba_addr) {
    return gba_addr | 1u;
  }

  // --- Roster & unit manipulation ---
  GbaPtr<Unit> Lyn() const;
  GbaPtr<Unit> Kent() const;
  GbaPtr<Unit> BrigandA() const;
  GbaPtr<Unit> BrigandB() const;

  void ConfigureUnit(int unit_id, const UnitSetupOptions& opts);
  void PlaceUnit(GbaPtr<Unit> unit, int x, int y, bool end_turn = false);
  void SetWeaponExp(GbaPtr<Unit> unit, ItemKind kind, uint8_t wexp);
  void MarkUnitForRecovery(GbaPtr<Unit> unit);
  void SyncUnitMap();

  // --- Map accessors & executable 8x8 ASCII terrain loader ---
  // Legend: '.' = Plains, '#' = Wall, 'F' = Forest.
  void LoadTerrainFromAscii(const std::array<std::string_view, kMapH>& rows);
  std::string FormatMapState() const;

  GbaPtr<GbaPtr<uint8_t>> TerrainMap() const;
  GbaPtr<GbaPtr<uint8_t>> MovementMap() const;
  GbaPtr<GbaPtr<uint8_t>> RangeMap() const;
  GbaPtr<GbaPtr<uint8_t>> FogMap() const;

  // --- PlayStatus / Phase / Support helpers ---
  void SetFogVision(uint8_t vision);
  uint8_t GetPhaseFaction() const;
  void SetPhaseFaction(uint8_t faction);
  uint16_t GetTurnNumber() const;
  void SetTurnNumber(uint16_t turn);
  void SetAutoEndTurn(bool enabled);
  SupportBonuses ComputeSupportBonuses(GbaPtr<const Unit> unit) const;

  // --- Combat exchange driver ---
  void RunCombatExchange(GbaPtr<Unit> attacker, GbaPtr<Unit> defender,
                         uint16_t config_flags);

  // --- Enemy AI state, scratch, decision & ROM table helpers ---
  void SetActiveAiUnit(GbaPtr<Unit> unit);
  bool IsDangerMapFilled() const;
  void SetDangerMapFilled(bool filled);
  void ResetAiDecideState(uint8_t recovery_mode = kAiRecoveryModeDefault);
  void PrepareAiTurn(GbaPtr<Unit> unit, const PrepareAiTurnOptions& opts = {});
  void RecordAiDecision(const AiDecisionOptions& opts);
  void ExpectAiDecision(const AiDecisionExpectation& expected) const;
  void SetAiHealThreshold(uint8_t percent);
  bool FindBestAiPosition(AiPos* absl_nonnull out);
  uint32_t ScoreAiCombat(uint8_t x, uint8_t y, GbaPtr<const Unit> target,
                         uint16_t item_slot = 0);
  uint32_t GetProcDispatchedCallback(GbaPtr<Proc> proc) const;

  // --- ROM table registration helpers ---
  void RegisterItem(const ItemSpec& spec);
  void RegisterJob(const JobSpec& spec);
  void RegisterChar(const CharSpec& spec);
  void WriteSupportPartner(uint32_t rom_addr, const SupportPartnerSpec& spec);
  void WriteAiScriptCmd(uint32_t rom_addr, const AiScriptCmdSpec& spec);
  void RegisterEnemyAiTurnShims();
  void InitEnemyAiTurnRomTables();

  std::vector<uint8_t> ewram_;
  std::vector<uint8_t> iwram_;
  std::vector<uint8_t> rom_;

 private:
  void InitRomTables();
  void InitPaddedMap(uint32_t buf_addr, uint32_t ptr_addr, uint8_t fill,
                     int pad_rows, int pad_cols);
  void InitMaps();
  void InitPlayState();
  void InitRoster();
};

}  // namespace fe7::e2e

#endif  // FE7_TESTING_E2E_SKIRMISH_FIXTURE_H_
