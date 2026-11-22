#include "game/unit/unit.h"

#include <cstdint>
#include <cstring>

#include "absl/base/nullability.h"
#include "game/unit.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

// Calls GetUnitEquippedWeapon() and returns the 16-bit item+uses halfword.
uint16_t EquippedWeapon(GbaPtr<const Unit> unit) {
  return static_cast<uint16_t>(GbaCall(kAddrGetUnitEquippedWeapon, unit.addr) &
                               0xFFFFu);
}

int ItemBonus(uint32_t bonus_fn_addr, uint16_t item) {
  return static_cast<int32_t>(GbaCall(bonus_fn_addr, item));
}

uint32_t ThunkClearUnit(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                        uint32_t /*r3*/,
                        const uint32_t* absl_nullable /*stack*/) {
  ClearUnit(GbaPtr<Unit>(r0));
  return 0u;
}

uint32_t ThunkCopyUnit(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                       uint32_t /*r3*/,
                       const uint32_t* absl_nullable /*stack*/) {
  CopyUnit(GbaPtr<const Unit>(r0), GbaPtr<Unit>(r1));
  return 0u;
}

uint32_t ThunkGetUnitVision(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                            uint32_t /*r3*/,
                            const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetUnitVision(GbaPtr<const Unit>(r0)));
}

uint32_t ThunkSetUnitStatus(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                            uint32_t /*r3*/,
                            const uint32_t* absl_nullable /*stack*/) {
  SetUnitStatus(GbaPtr<Unit>(r0), static_cast<int>(r1));
  return 0u;
}

uint32_t ThunkSetUnitStatusExt(uint32_t r0, uint32_t r1, uint32_t r2,
                               uint32_t /*r3*/,
                               const uint32_t* absl_nullable /*stack*/) {
  SetUnitStatusExt(GbaPtr<Unit>(r0), static_cast<int>(r1),
                   static_cast<int>(r2));
  return 0u;
}

uint32_t ThunkGetUnitItemCount(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                               uint32_t /*r3*/,
                               const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetUnitItemCount(GbaPtr<const Unit>(r0)));
}

uint32_t ThunkGetUnitByPid(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                           uint32_t /*r3*/,
                           const uint32_t* absl_nullable /*stack*/) {
  return GetUnitByPid(static_cast<int>(r0)).addr;
}

uint32_t ThunkGetUnitAid(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                         uint32_t /*r3*/,
                         const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetUnitAid(GbaPtr<const Unit>(r0)));
}

uint32_t ThunkGetUnitMagRange(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                              uint32_t /*r3*/,
                              const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetUnitMagRange(GbaPtr<const Unit>(r0)));
}

uint32_t ThunkUnitKnowsMagic(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                             uint32_t /*r3*/,
                             const uint32_t* absl_nullable /*stack*/) {
  return UnitKnowsMagic(GbaPtr<const Unit>(r0)) ? 1u : 0u;
}

uint32_t ThunkGetUnitCurrentHp(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                               uint32_t /*r3*/,
                               const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetUnitCurrentHp(GbaPtr<Unit>(r0)));
}

uint32_t ThunkGetUnitMaxHp(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                           uint32_t /*r3*/,
                           const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetUnitMaxHp(GbaPtr<const Unit>(r0)));
}

uint32_t ThunkGetUnitPower(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                           uint32_t /*r3*/,
                           const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetUnitPower(GbaPtr<const Unit>(r0)));
}

uint32_t ThunkGetUnitSkill(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                           uint32_t /*r3*/,
                           const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetUnitSkill(GbaPtr<const Unit>(r0)));
}

uint32_t ThunkGetUnitSpeed(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                           uint32_t /*r3*/,
                           const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetUnitSpeed(GbaPtr<const Unit>(r0)));
}

uint32_t ThunkGetUnitDefense(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                             uint32_t /*r3*/,
                             const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetUnitDefense(GbaPtr<const Unit>(r0)));
}

uint32_t ThunkGetUnitResistance(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                                uint32_t /*r3*/,
                                const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetUnitResistance(GbaPtr<const Unit>(r0)));
}

uint32_t ThunkGetUnitLuck(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                          uint32_t /*r3*/,
                          const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetUnitLuck(GbaPtr<const Unit>(r0)));
}

uint32_t ThunkGetUnitFid(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                         uint32_t /*r3*/,
                         const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetUnitFid(GbaPtr<const Unit>(r0)));
}

uint32_t ThunkGetUnitMiniFid(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                             uint32_t /*r3*/,
                             const uint32_t* absl_nullable /*stack*/) {
  return static_cast<uint32_t>(GetUnitMiniFid(GbaPtr<const Unit>(r0)));
}

uint32_t ThunkSetUnitHp(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                        uint32_t /*r3*/,
                        const uint32_t* absl_nullable /*stack*/) {
  SetUnitHp(GbaPtr<Unit>(r0), static_cast<int>(r1));
  return 0u;
}

uint32_t ThunkAddUnitHp(uint32_t r0, uint32_t r1, uint32_t /*r2*/,
                        uint32_t /*r3*/,
                        const uint32_t* absl_nullable /*stack*/) {
  AddUnitHp(GbaPtr<Unit>(r0), static_cast<int>(r1));
  return 0u;
}

uint32_t ThunkGetUnit(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                      uint32_t /*r3*/,
                      const uint32_t* absl_nullable /*stack*/) {
  return GetUnit(static_cast<int>(r0)).addr;
}

uint32_t ThunkGetJobInfo(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                         uint32_t /*r3*/,
                         const uint32_t* absl_nullable /*stack*/) {
  return GetJobInfo(static_cast<int>(r0)).addr;
}

uint32_t ThunkGetPersonInfo(uint32_t r0, uint32_t /*r1*/, uint32_t /*r2*/,
                            uint32_t /*r3*/,
                            const uint32_t* absl_nullable /*stack*/) {
  return GetPersonInfo(static_cast<int>(r0)).addr;
}

}  // namespace

void ClearUnit(GbaPtr<Unit> unit) {
  const int8_t id = unit->id;
  std::memset(unit.get(), 0, sizeof(Unit));
  unit->id = id;
}

void CopyUnit(GbaPtr<const Unit> src, GbaPtr<Unit> dst) {
  const int8_t id = dst->id;
  std::memcpy(dst.get(), src.get(), sizeof(Unit));
  dst->id = id;
}

int GetUnitVision(GbaPtr<const Unit> unit) {
  int vision = static_cast<int>(*GbaPtr<const uint8_t>(kChapterVisionAddr));
  if ((UnitAttributes(*unit) & kUnitAttrThief) != 0u) {
    vision += 5;
  }
  return vision + static_cast<int>(unit->torch);
}

void SetUnitStatus(GbaPtr<Unit> unit, int status) {
  if (status == 0) {
    unit->status = 0u;
    unit->status_duration = 0u;
  } else {
    unit->status = static_cast<uint8_t>(status & 0xF);
    unit->status_duration = 5u;
  }
}

void SetUnitStatusExt(GbaPtr<Unit> unit, int status, int duration) {
  unit->status = static_cast<uint8_t>(status & 0xF);
  unit->status_duration = static_cast<uint8_t>(duration & 0xF);
}

int GetUnitItemCount(GbaPtr<const Unit> unit) {
  for (int i = kUnitItemCount - 1; i >= 0; --i) {
    if (unit->items[i] != 0u) {
      return i + 1;
    }
  }
  return 0;
}

GbaPtr<Unit> GetUnitByPid(int pid) {
  for (int id = 1; id < 0x100; ++id) {
    const GbaPtr<Unit> unit = GetUnit(id);
    if (!unit || !unit->pinfo) {
      continue;
    }
    if (static_cast<int>(unit->pinfo->id) == pid) {
      return unit;
    }
  }
  return GbaPtr<Unit>(0);
}

int GetUnitAid(GbaPtr<const Unit> unit) {
  const uint32_t attrs = UnitAttributes(*unit);
  const int con = static_cast<int>(unit->jinfo->base_con) +
                  static_cast<int>(unit->pinfo->bonus_con) +
                  static_cast<int>(unit->bonus_con);
  if ((attrs & kUnitAttrMounted) == 0u) {
    return con - 1;
  }
  return ((attrs & kUnitAttrFemale) != 0u ? 20 : 25) - con;
}

int GetUnitMagRange(GbaPtr<const Unit> unit) {
  const int power = static_cast<int>(unit->pow) +
                    ItemBonus(kAddrGetItemPowBonus, EquippedWeapon(unit));
  const int range = power / 2;
  return range < 5 ? 5 : range;
}

bool UnitKnowsMagic(GbaPtr<const Unit> unit) {
  // Weapon rank slots 4..7 are Staff, Anima, Light, and Dark.
  return (unit->wexp[4] | unit->wexp[5] | unit->wexp[6] | unit->wexp[7]) != 0u;
}

int GetUnitCurrentHp(GbaPtr<Unit> unit) {
  const int max_hp = GetUnitMaxHp(GbaPtr<const Unit>(unit.addr));
  if (static_cast<int>(unit->hp) > max_hp) {
    unit->hp = static_cast<int8_t>(max_hp);
  }
  return static_cast<int>(unit->hp);
}

int GetUnitMaxHp(GbaPtr<const Unit> unit) {
  return static_cast<int>(unit->max_hp) +
         ItemBonus(kAddrGetItemHpBonus, EquippedWeapon(unit));
}

int GetUnitPower(GbaPtr<const Unit> unit) {
  return static_cast<int>(unit->pow) +
         ItemBonus(kAddrGetItemPowBonus, EquippedWeapon(unit));
}

int GetUnitSkill(GbaPtr<const Unit> unit) {
  const int bonus = ItemBonus(kAddrGetItemSklBonus, EquippedWeapon(unit));
  const int base = (unit->flags & kUnitFlagRescuing) != 0u
                       ? static_cast<int>(unit->skl) / 2
                       : static_cast<int>(unit->skl);
  return base + bonus;
}

int GetUnitSpeed(GbaPtr<const Unit> unit) {
  const int bonus = ItemBonus(kAddrGetItemSpdBonus, EquippedWeapon(unit));
  const int base = (unit->flags & kUnitFlagRescuing) != 0u
                       ? static_cast<int>(unit->spd) / 2
                       : static_cast<int>(unit->spd);
  return base + bonus;
}

int GetUnitDefense(GbaPtr<const Unit> unit) {
  return static_cast<int>(unit->def) +
         ItemBonus(kAddrGetItemDefBonus, EquippedWeapon(unit));
}

int GetUnitResistance(GbaPtr<const Unit> unit) {
  return static_cast<int>(unit->res) +
         ItemBonus(kAddrGetItemResBonus, EquippedWeapon(unit)) +
         static_cast<int>(unit->barrier);
}

int GetUnitLuck(GbaPtr<const Unit> unit) {
  return static_cast<int>(unit->lck) +
         ItemBonus(kAddrGetItemLckBonus, EquippedWeapon(unit));
}

int GetUnitFid(GbaPtr<const Unit> unit) {
  if (unit->pinfo->fid != 0u) {
    return static_cast<int>(unit->pinfo->fid);
  }
  return static_cast<int>(unit->jinfo->fid);
}

int GetUnitMiniFid(GbaPtr<const Unit> unit) {
  if (unit->pinfo->chibi_id != 0u) {
    return 0x7F00 | static_cast<int>(unit->pinfo->chibi_id);
  }
  return GetUnitFid(unit);
}

void SetUnitHp(GbaPtr<Unit> unit, int hp) {
  unit->hp = static_cast<int8_t>(hp);
  const int max_hp = GetUnitMaxHp(GbaPtr<const Unit>(unit.addr));
  if (static_cast<int>(unit->hp) > max_hp) {
    unit->hp = static_cast<int8_t>(max_hp);
  }
}

void AddUnitHp(GbaPtr<Unit> unit, int amount) {
  int hp = static_cast<int>(unit->hp) + amount;
  const int max_hp = GetUnitMaxHp(GbaPtr<const Unit>(unit.addr));
  if (hp > max_hp) {
    hp = max_hp;
  }
  if (hp < 0) {
    hp = 0;
  }
  unit->hp = static_cast<int8_t>(hp);
}

GbaPtr<Unit> GetUnit(int id) {
  const GbaPtr<const GbaPtr<Unit>> lut(kUnitLutAddr);
  return lut[id & 0xFF];
}

GbaPtr<const JInfo> GetJobInfo(int jid) {
  if (jid <= 0) {
    return GbaPtr<const JInfo>(0);
  }
  // Unsigned multiply matches Thumb `muls` wraparound without signed UB.
  const uint32_t addr =
      kJInfoTableAddr +
      static_cast<uint32_t>(jid) * static_cast<uint32_t>(sizeof(JInfo));
  return GbaPtr<const JInfo>(addr);
}

GbaPtr<const PInfo> GetPersonInfo(int pid) {
  if (pid <= 0) {
    return GbaPtr<const PInfo>(0);
  }
  const uint32_t addr =
      kPInfoTableAddr +
      static_cast<uint32_t>(pid) * static_cast<uint32_t>(sizeof(PInfo));
  return GbaPtr<const PInfo>(addr);
}

void RegisterUnitFunctions() {
  RegisterNative(kAddrClearUnit, &ThunkClearUnit);
  RegisterNative(kAddrCopyUnit, &ThunkCopyUnit);
  RegisterNative(kAddrGetUnitVision, &ThunkGetUnitVision);
  RegisterNative(kAddrSetUnitStatus, &ThunkSetUnitStatus);
  RegisterNative(kAddrSetUnitStatusExt, &ThunkSetUnitStatusExt);
  RegisterNative(kAddrGetUnitItemCount, &ThunkGetUnitItemCount);
  RegisterNative(kAddrGetUnitByPid, &ThunkGetUnitByPid);
  RegisterNative(kAddrGetUnitAid, &ThunkGetUnitAid);
  RegisterNative(kAddrGetUnitMagRange, &ThunkGetUnitMagRange);
  RegisterNative(kAddrUnitKnowsMagic, &ThunkUnitKnowsMagic);
  RegisterNative(kAddrGetUnitCurrentHp, &ThunkGetUnitCurrentHp);
  RegisterNative(kAddrGetUnitMaxHp, &ThunkGetUnitMaxHp);
  RegisterNative(kAddrGetUnitPower, &ThunkGetUnitPower);
  RegisterNative(kAddrGetUnitSkill, &ThunkGetUnitSkill);
  RegisterNative(kAddrGetUnitSpeed, &ThunkGetUnitSpeed);
  RegisterNative(kAddrGetUnitDefense, &ThunkGetUnitDefense);
  RegisterNative(kAddrGetUnitResistance, &ThunkGetUnitResistance);
  RegisterNative(kAddrGetUnitLuck, &ThunkGetUnitLuck);
  RegisterNative(kAddrGetUnitFid, &ThunkGetUnitFid);
  RegisterNative(kAddrGetUnitMiniFid, &ThunkGetUnitMiniFid);
  RegisterNative(kAddrSetUnitHp, &ThunkSetUnitHp);
  RegisterNative(kAddrAddUnitHp, &ThunkAddUnitHp);
  RegisterNative(kAddrGetUnit, &ThunkGetUnit);
  RegisterNative(kAddrGetJobInfo, &ThunkGetJobInfo);
  RegisterNative(kAddrGetPersonInfo, &ThunkGetPersonInfo);
}

}  // namespace fe7
