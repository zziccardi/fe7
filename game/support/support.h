// FE7 US (AE7E) support relationships, EXP accumulation, and affinity bonuses.
//
// Implements 22 support-system functions (ROM 0x08026628..0x08026C08):
// partner lookup, C/B/A rank thresholds, per-turn adjacent/rescue support
// growth (`ProcessSupportGains`), support-level unlocks, affinity bonus
// accumulation (`GetUnitSupportBonuses`), and UI icon/character helpers.

#ifndef FE7_GAME_SUPPORT_SUPPORT_H_
#define FE7_GAME_SUPPORT_SUPPORT_H_

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "game/unit.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// Static ROM support partner table pointed to by `PInfo::support_info`
// (24 bytes).
struct SupportInfo {
  uint8_t pids[kUnitSupportCount];        // 0x00: Partner character IDs
  uint8_t exp_base[kUnitSupportCount];    // 0x07: Initial support EXP
  uint8_t exp_growth[kUnitSupportCount];  // 0x0E: Support EXP gained per turn
  uint8_t count;                          // 0x15: Number of valid partners
  uint8_t pad_16[2];                      // 0x16
};

static_assert(sizeof(SupportInfo) == 0x18);
static_assert(offsetof(SupportInfo, exp_base) == 0x07);
static_assert(offsetof(SupportInfo, exp_growth) == 0x0E);
static_assert(offsetof(SupportInfo, count) == 0x15);
static_assert(std::is_trivially_copyable_v<SupportInfo>);

// Affinity bonus table entry (`0x08C9A1C0`) and accumulated combat support
// bonus block (8 bytes).
struct SupportBonuses {
  uint8_t affinity;       // 0x00: Affinity ID (0 terminates ROM table)
  uint8_t bonus_attack;   // 0x01
  uint8_t bonus_defense;  // 0x02
  uint8_t bonus_hit;      // 0x03
  uint8_t bonus_avoid;    // 0x04
  uint8_t bonus_crit;     // 0x05
  uint8_t bonus_dodge;    // 0x06
  uint8_t pad_07;         // 0x07
};

static_assert(sizeof(SupportBonuses) == 8);
static_assert(std::is_trivially_copyable_v<SupportBonuses>);

// Fixed FE7 US ROM/EWRAM addresses for support tables and play state.
inline constexpr uint32_t kSupportBmStatusAddr = 0x0202BBB8u;
inline constexpr uint32_t kSupportPlayStatusAddr = 0x0202BBF8u;
inline constexpr uint32_t kSupportLevelCapsAddr = 0x08B94184u;
inline constexpr uint32_t kAffinityBonusTableAddr = 0x08C9A1C0u;

// FE7 US ROM addresses for the 22 decompiled support functions.
inline constexpr uint32_t kAddrGetUnitSupporterCount = 0x08026628u;
inline constexpr uint32_t kAddrGetUnitSupporterCharacter = 0x08026638u;
inline constexpr uint32_t kAddrGetUnitSupporterUnit = 0x0802664Cu;
inline constexpr uint32_t kAddrGetSupportLevelBySupportIndex = 0x08026694u;
inline constexpr uint32_t kAddrGetUnitTotalSupportLevel = 0x080266B8u;
inline constexpr uint32_t kAddrUnitGainSupportExp = 0x080266E4u;
inline constexpr uint32_t kAddrUnitGainSupportLevel = 0x08026744u;
inline constexpr uint32_t kAddrCanUnitSupportNow = 0x08026778u;
inline constexpr uint32_t kAddrGetUnitSupporterInitialExp = 0x080267F4u;
inline constexpr uint32_t kAddrGetUnitSupporterNum = 0x0802680Cu;
inline constexpr uint32_t kAddrClearUnitSupports = 0x08026844u;
inline constexpr uint32_t kAddrSupportProcessGains = 0x08026898u;
inline constexpr uint32_t kAddrGetAffinityBonusEntry = 0x08026990u;
inline constexpr uint32_t kAddrApplyAffinitySupportBonuses = 0x080269B0u;
inline constexpr uint32_t kAddrInitSupportBonuses = 0x08026A08u;
inline constexpr uint32_t kAddrGetUnitSupportBonuses = 0x08026A18u;
inline constexpr uint32_t kAddrGetUnitAffinityIcon = 0x08026B24u;
inline constexpr uint32_t kAddrGetCharacterAffinityIcon = 0x08026B38u;
inline constexpr uint32_t kAddrGetSupportLevelUiChar = 0x08026B50u;
inline constexpr uint32_t kAddrSetSupportLevelGained = 0x08026BA0u;
inline constexpr uint32_t kAddrHasUnitGainedSupportLevel = 0x08026BF0u;
inline constexpr uint32_t kAddrArePidsAtMaxSupport = 0x08026C08u;

// Returns the number of configured support partners for `unit` (0 if none).
// Matches FE7 US `func_08026628`.
int GetUnitSupporterCount(GbaPtr<const Unit> unit);

// Returns the partner PID at support slot `num` (0 if no `support_info`).
// Matches FE7 US `func_08026638`.
uint8_t GetUnitSupporterCharacter(GbaPtr<const Unit> unit, int num);

// Searches `unit`'s faction for the active `Unit` matching support slot `num`.
// Matches FE7 US `func_0802664C`.
GbaPtr<Unit> GetUnitSupporterUnit(GbaPtr<const Unit> unit, int num);

// Converts `unit->supports[num]` EXP into rank `0` (None), `1` (C), `2` (B), or
// `3` (A). Matches FE7 US `func_08026694`.
int GetSupportLevelBySupportIndex(GbaPtr<const Unit> unit, int num);

// Returns the sum of all support ranks across `unit`'s partners (max 5).
// Matches FE7 US `func_080266B8`.
int GetUnitTotalSupportLevel(GbaPtr<const Unit> unit);

// Adds `support_info->exp_growth[num]` to `unit->supports[num]`, clamping at
// the current rank's cap (`80/160/240/241`), and increments chapter support
// gain tally. Matches FE7 US `func_080266E4`.
void UnitGainSupportExp(GbaPtr<Unit> unit, int num);

// Increments `unit->supports[num]` by 1 to cross the rank threshold and marks
// both partners' `support_bits` for this chapter. Matches FE7 US `0x08026744`.
void UnitGainSupportLevel(GbaPtr<Unit> unit, int num);

// Returns true if `unit` and partner `num` can initiate a support conversation
// right now. Matches FE7 US `func_08026778`.
bool CanUnitSupportNow(GbaPtr<const Unit> unit, int num);

// Returns `support_info->exp_base[num]`, or `-1` if `unit` has no support info.
// Matches FE7 US `func_080267F4`.
int GetUnitSupporterInitialExp(GbaPtr<const Unit> unit, int num);

// Returns the support slot index of `pid` on `unit`, or `-1` if not found.
// Matches FE7 US `func_0802680C`.
int GetUnitSupporterNum(GbaPtr<const Unit> unit, uint8_t pid);

// Zeroes all support EXP between `unit` and every active partner (e.g. on
// unit death). Matches FE7 US `ClearUnitSupports` (0x08026844).
void ClearUnitSupports(GbaPtr<Unit> unit);

// Processes per-turn support EXP gains for all adjacent or paired-rescue blue
// units (called at the start of Blue phase). Matches FE7 US `func_08026898`.
void ProcessSupportGains();

// Looks up the ROM `SupportBonuses` entry for `affinity` (`0x08C9A1C0`).
// Matches FE7 US `func_08026990`.
GbaPtr<const SupportBonuses> GetAffinityBonusEntry(int affinity);

// Adds `level * entry->bonus_*` for `affinity` into `bonuses`.
// Matches FE7 US `func_080269B0`.
void ApplyAffinitySupportBonuses(GbaPtr<SupportBonuses> bonuses, int affinity,
                                 int level);

// Zeroes all 6 stat bonus fields in `bonuses`. Matches FE7 US `func_08026A08`.
void InitSupportBonuses(GbaPtr<SupportBonuses> bonuses);

// Accumulates combat support bonuses from all active partners within 3 tiles
// (halving final sums) and returns a bitmask of contributing partner slots.
// Matches FE7 US `func_08026A18`.
int GetUnitSupportBonuses(GbaPtr<const Unit> unit,
                          GbaPtr<SupportBonuses> bonuses);

// Returns `unit->pinfo->affinity + 0x79` (icon ID), or `-1` if affinity == 0.
// Matches FE7 US `func_08026B24`.
int GetUnitAffinityIcon(GbaPtr<const Unit> unit);

// Returns `GetPersonInfo(pid)->affinity + 0x79`, or `-1` if affinity == 0.
// Matches FE7 US `func_08026B38`.
int GetCharacterAffinityIcon(int pid);

// Returns the FE7 font glyph byte for support rank `level` (`0..3`).
// Matches FE7 US `func_08026B50`.
uint8_t GetSupportLevelUiChar(int level);

// Sets the chapter-gained bit in `support_bits` on both `pid_a` and `pid_b`.
// Matches FE7 US `func_08026BA0`.
void SetSupportLevelGained(uint8_t pid_a, uint8_t pid_b);

// Returns true if bit `num` is set in `unit->support_bits`.
// Matches FE7 US `func_08026BF0`.
bool HasUnitGainedSupportLevel(GbaPtr<const Unit> unit, int num);

// Returns true if `pid_a` and `pid_b` have reached A-rank support (level 3).
// Matches FE7 US `func_08026C08`.
bool ArePidsAtMaxSupport(uint8_t pid_a, uint8_t pid_b);

// Computes and returns `unit`'s accumulated support bonuses by value (usable
// from native C++ without needing a GBA stack buffer).
SupportBonuses ComputeUnitSupportBonuses(GbaPtr<const Unit> unit);

// Registers all 22 support-system functions in the dispatch table.
void RegisterSupportFunctions();

}  // namespace fe7

#endif  // FE7_GAME_SUPPORT_SUPPORT_H_
