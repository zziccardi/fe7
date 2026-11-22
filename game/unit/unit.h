// FE7 US (AE7E) unit lookup, lifecycle helpers, and stat getters.
//
// Implements Wave 2 core unit accessors (ROM 0x08017508..0x08018D38):
//   - Lookup tables: GetUnit(), GetUnitByPid(), GetJobInfo(), GetPersonInfo()
//   - Lifecycle & status: ClearUnit(), CopyUnit(), SetUnitStatus(),
//     SetUnitStatusExt(), SetUnitHp(), AddUnitHp()
//   - Stat & property getters: GetUnitCurrentHp(), GetUnitMaxHp(),
//     GetUnitPower(), GetUnitSkill(), GetUnitSpeed(), GetUnitDefense(),
//     GetUnitResistance(), GetUnitLuck(), GetUnitAid(), GetUnitMagRange(),
//     GetUnitVision(), GetUnitItemCount(), UnitKnowsMagic(), GetUnitFid(),
//     GetUnitMiniFid().
//
// Equipped-weapon stat bonuses dispatch through GbaCall() so un-decompiled
// item functions work seamlessly until the item subsystem is decompiled.

#ifndef FE7_GAME_UNIT_UNIT_H_
#define FE7_GAME_UNIT_UNIT_H_

#include <cstdint>

#include "game/unit.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// Fixed FE7 US ROM/EWRAM addresses used by the unit subsystem.
inline constexpr uint32_t kUnitLutAddr = 0x08B92EB0u;        // Unit*[256]
inline constexpr uint32_t kJInfoTableAddr = 0x08BE015Cu;     // JInfo[]
inline constexpr uint32_t kPInfoTableAddr = 0x08BDCE18u;     // PInfo[]
inline constexpr uint32_t kChapterVisionAddr = 0x0202BC05u;  // gPlayStatus+D

// External item helper ROM addresses called via GbaCall() until Wave 2b.
inline constexpr uint32_t kAddrGetItemHpBonus = 0x08016040u;
inline constexpr uint32_t kAddrGetItemPowBonus = 0x0801606Cu;
inline constexpr uint32_t kAddrGetItemSklBonus = 0x08016098u;
inline constexpr uint32_t kAddrGetItemSpdBonus = 0x080160C4u;
inline constexpr uint32_t kAddrGetItemDefBonus = 0x080160F0u;
inline constexpr uint32_t kAddrGetItemResBonus = 0x0801611Cu;
inline constexpr uint32_t kAddrGetItemLckBonus = 0x08016148u;
inline constexpr uint32_t kAddrGetUnitEquippedWeapon = 0x08016764u;

// FE7 US ROM addresses for the 25 decompiled unit functions.
inline constexpr uint32_t kAddrClearUnit = 0x08017508u;
inline constexpr uint32_t kAddrCopyUnit = 0x08017530u;
inline constexpr uint32_t kAddrGetUnitVision = 0x080175BCu;
inline constexpr uint32_t kAddrSetUnitStatus = 0x080175E8u;
inline constexpr uint32_t kAddrSetUnitStatusExt = 0x08017600u;
inline constexpr uint32_t kAddrGetUnitItemCount = 0x080176DCu;
inline constexpr uint32_t kAddrGetUnitByPid = 0x08017D34u;
inline constexpr uint32_t kAddrGetUnitAid = 0x08018450u;
inline constexpr uint32_t kAddrGetUnitMagRange = 0x080184B4u;
inline constexpr uint32_t kAddrUnitKnowsMagic = 0x080184DCu;
inline constexpr uint32_t kAddrGetUnitCurrentHp = 0x08018A70u;
inline constexpr uint32_t kAddrGetUnitMaxHp = 0x08018AB0u;
inline constexpr uint32_t kAddrGetUnitPower = 0x08018AD0u;
inline constexpr uint32_t kAddrGetUnitSkill = 0x08018AF0u;
inline constexpr uint32_t kAddrGetUnitSpeed = 0x08018B30u;
inline constexpr uint32_t kAddrGetUnitDefense = 0x08018B70u;
inline constexpr uint32_t kAddrGetUnitResistance = 0x08018B90u;
inline constexpr uint32_t kAddrGetUnitLuck = 0x08018BB8u;
inline constexpr uint32_t kAddrGetUnitFid = 0x08018BD8u;
inline constexpr uint32_t kAddrGetUnitMiniFid = 0x08018BF4u;
inline constexpr uint32_t kAddrSetUnitHp = 0x08018C40u;
inline constexpr uint32_t kAddrAddUnitHp = 0x08018C7Cu;
inline constexpr uint32_t kAddrGetUnit = 0x08018D0Cu;
inline constexpr uint32_t kAddrGetJobInfo = 0x08018D20u;
inline constexpr uint32_t kAddrGetPersonInfo = 0x08018D38u;

// Zeroes all fields of `unit` while preserving its slot `id`.
// Matches FE7 US `ClearUnit` (0x08017508).
void ClearUnit(GbaPtr<Unit> unit);

// Copies `*src` into `*dst` while preserving `dst->id`.
// Matches FE7 US `func_08017530`.
void CopyUnit(GbaPtr<const Unit> src, GbaPtr<Unit> dst);

// Returns `unit`'s fog-of-war vision radius (chapter base + thief + torch).
// Matches FE7 US `GetUnitVision` (0x080175BC).
int GetUnitVision(GbaPtr<const Unit> unit);

// Sets `unit`'s status with default duration 5 (or clears both if `status==0`).
// Matches FE7 US `SetUnitStatus` (0x080175E8).
void SetUnitStatus(GbaPtr<Unit> unit, int status);

// Sets `unit`'s status and explicit turn duration (both 4-bit fields).
// Matches FE7 US `func_08017600`.
void SetUnitStatusExt(GbaPtr<Unit> unit, int status, int duration);

// Returns the number of occupied item slots (highest nonzero index + 1).
// Matches FE7 US `GetUnitItemCount` (0x080176DC).
int GetUnitItemCount(GbaPtr<const Unit> unit);

// Returns the first active unit matching character ID `pid`, or null.
// Matches FE7 US `GetUnitByPid` (0x08017D34).
GbaPtr<Unit> GetUnitByPid(int pid);

// Returns `unit`'s rescue Aid value based on mount, gender, and Constitution.
// Matches FE7 US `GetUnitAid` (0x08018450).
int GetUnitAid(GbaPtr<const Unit> unit);

// Returns `unit`'s staff range (`max(5, power / 2)`).
// Matches FE7 US `GetUnitMagRange` (0x080184B4).
int GetUnitMagRange(GbaPtr<const Unit> unit);

// Returns true iff `unit` has nonzero rank in Staff, Anima, Light, or Dark.
// Matches FE7 US `UnitKnowsMagic` (0x080184DC).
bool UnitKnowsMagic(GbaPtr<const Unit> unit);

// Clamps `unit->hp` to GetUnitMaxHp() if over maximum and returns current HP.
// Matches FE7 US `GetUnitCurrentHp` (0x08018A70).
int GetUnitCurrentHp(GbaPtr<Unit> unit);

// Returns `unit->max_hp` plus equipped weapon HP bonus.
// Matches FE7 US `GetUnitMaxHp` (0x08018AB0).
int GetUnitMaxHp(GbaPtr<const Unit> unit);

// Returns `unit->pow` plus equipped weapon Pow/Mag bonus.
// Matches FE7 US `GetUnitPower` (0x08018AD0).
int GetUnitPower(GbaPtr<const Unit> unit);

// Returns `unit->skl` (halved if rescuing) plus equipped weapon Skl bonus.
// Matches FE7 US `GetUnitSkill` (0x08018AF0).
int GetUnitSkill(GbaPtr<const Unit> unit);

// Returns `unit->spd` (halved if rescuing) plus equipped weapon Spd bonus.
// Matches FE7 US `GetUnitSpeed` (0x08018B30).
int GetUnitSpeed(GbaPtr<const Unit> unit);

// Returns `unit->def` plus equipped weapon Def bonus.
// Matches FE7 US `GetUnitDefense` (0x08018B70).
int GetUnitDefense(GbaPtr<const Unit> unit);

// Returns `unit->res` plus equipped weapon Res bonus plus Barrier boost.
// Matches FE7 US `GetUnitResistance` (0x08018B90).
int GetUnitResistance(GbaPtr<const Unit> unit);

// Returns `unit->lck` plus equipped weapon Lck bonus.
// Matches FE7 US `GetUnitLuck` (0x08018BB8).
int GetUnitLuck(GbaPtr<const Unit> unit);

// Returns `unit`'s portrait ID (`pinfo->fid`, falling back to `jinfo->fid`).
// Matches FE7 US `GetUnitFid` (0x08018BD8).
int GetUnitFid(GbaPtr<const Unit> unit);

// Returns `unit`'s mini-portrait ID (`0x7F00 | chibi_id` if set, else Fid).
// Matches FE7 US `func_08018BF4`.
int GetUnitMiniFid(GbaPtr<const Unit> unit);

// Sets `unit->hp = hp` and clamps down to GetUnitMaxHp().
// Matches FE7 US `func_08018C40`.
void SetUnitHp(GbaPtr<Unit> unit, int hp);

// Adds `amount` to `unit->hp` and clamps to `[0, GetUnitMaxHp()]`.
// Matches FE7 US `func_08018C7C`.
void AddUnitHp(GbaPtr<Unit> unit, int amount);

// Looks up a unit slot by its 8-bit deployment ID via FE7's ROM LUT.
// Matches FE7 US `GetUnit` (0x08018D0C).
GbaPtr<Unit> GetUnit(int id);

// Returns a ROM pointer to class entry `jid`, or null when `jid <= 0`.
// Matches FE7 US `GetJobInfo` (0x08018D20).
GbaPtr<const JInfo> GetJobInfo(int jid);

// Returns a ROM pointer to character entry `pid`, or null when `pid <= 0`.
// Matches FE7 US `func_08018D38`.
GbaPtr<const PInfo> GetPersonInfo(int pid);

// EWRAM PID stats array (`0x0203E790`, 16 bytes per entry) and ROM addresses.
inline constexpr uint32_t kPidStatsTableAddr = 0x0203E790u;
inline constexpr uint32_t kAddrUnitCheckStatOverflow = 0x08017C64u;
inline constexpr uint32_t kAddrPidStatsAddExpGained = 0x080A009Cu;
inline constexpr uint32_t kAddrRecordUnitTurnDeployment = 0x080A00FCu;
inline constexpr uint32_t kAddrPidStatsAddScore = 0x080A0248u;

// Clamps `unit`'s stats (HP, Pow, Skl, Spd, Def, Res, Lck, Con, Mov) against
// faction/class caps. Matches FE7 US `UnitCheckStatOverflow` (0x08017C64).
void UnitCheckStatOverflow(GbaPtr<Unit> unit);

// Adds `delta` to character `pid`'s 16-bit PID stats score (clamped to
// `[0, 0x4000]`). Matches FE7 US `func_080A0248`.
void PidStatsAddScore(uint8_t pid, int delta);

// Records `exp_gain` in character `pid`'s 12-bit EXP counter (capped at 4000)
// and adds `exp_gain` to the PID score. Matches `PidStatsAddExpGained`
// (0x080A009C).
void PidStatsAddExpGained(uint8_t pid, int exp_gain);

// Applies the per-turn deployment score penalty (-8) to character `pid`.
// Matches FE7 US `func_080A00FC`.
void RecordUnitTurnDeployment(uint8_t pid);

// Registers all 29 native unit functions in the global dispatch table.
void RegisterUnitFunctions();

}  // namespace fe7

#endif  // FE7_GAME_UNIT_UNIT_H_
