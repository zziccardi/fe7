// FE7 US (AE7E) unit action execution, rescue/drop/give, autoleveling, item
// consumption, key lookup, and stat-booster mechanics.
//
// Implements 26 functions (ROM 0x08016D0C, 0x080176F8..0x080187BC, and
// 0x0802CD28): inventory search & consumption (`FindUnitItemSlot`,
// `UnitHasItem`, `UnitUpdateUsedItem`, `GetUnitLastItem`,
// `GetUnitKeyItemSlotForTerrain`), unit initialization & autoleveling
// (`UnitInitStats`, `UnitApplyAltPinfo`, `UnitInitSupports`,
// `UnitAutolevelWeaponExp`, `UnitAutolevelCore`, `UnitApplyBonusLevels`,
// `UnitAutolevelPlayer`), rescue/drop/give/kill/sync (`CanUnitCarry`,
// `UnitRescue`, `UnitDropRescue`, `UnitGiveRescue`, `KillUnit`,
// `UnitSyncMovement`, `UnitRevealAtPosition`), action startup
// (`UnitBeginAction`, `UnitBeginReMoveAction`), capability flags
// (`GetAidIconFromAttributes`, `GetUnitUseFlags`, `GetEnemyFactionUseFlags`,
// `GetUnitByPidInFaction`), and stat boosters (`ApplyItemStatBoost`).

#ifndef FE7_GAME_ACTION_ACTION_H_
#define FE7_GAME_ACTION_ACTION_H_

#include <cstdint>

#include "game/unit.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// Fixed FE7 US EWRAM/IWRAM addresses used by unit action startup.
inline constexpr uint32_t kActionActiveUnitIdAddr = 0x0202BD48u;
inline constexpr uint32_t kActionMoveOriginAddr = 0x0202BD4Cu;
inline constexpr uint32_t kActionBmStatusAddr = 0x0202BBB8u;
inline constexpr uint32_t kActionPlayStatusAddr = 0x0202BBF8u;
inline constexpr uint32_t kActionDataAddr = 0x0203A85Cu;
inline constexpr uint32_t kActionActiveUnitAddr = 0x03004690u;

// FE7 US ROM addresses for the 26 decompiled unit-action functions.
inline constexpr uint32_t kAddrFindUnitItemSlot = 0x08016D0Cu;
inline constexpr uint32_t kAddrUnitHasItem = 0x080176F8u;
inline constexpr uint32_t kAddrUnitInitStats = 0x08017930u;
inline constexpr uint32_t kAddrUnitApplyAltPinfo = 0x080179BCu;
inline constexpr uint32_t kAddrUnitInitSupports = 0x080179F0u;
inline constexpr uint32_t kAddrUnitAutolevelWeaponExp = 0x08017A1Cu;
inline constexpr uint32_t kAddrUnitAutolevelCore = 0x08017AC0u;
inline constexpr uint32_t kAddrUnitApplyBonusLevels = 0x08017B4Cu;
inline constexpr uint32_t kAddrUnitAutolevelPlayer = 0x08017BC0u;
inline constexpr uint32_t kAddrGetUnitByPidInFaction = 0x08017D70u;
inline constexpr uint32_t kAddrCanUnitCarry = 0x08017DB4u;
inline constexpr uint32_t kAddrUnitRescue = 0x08017DE4u;
inline constexpr uint32_t kAddrUnitDropRescue = 0x08017E08u;
inline constexpr uint32_t kAddrUnitGiveRescue = 0x08017E5Cu;
inline constexpr uint32_t kAddrKillUnit = 0x08017E90u;
inline constexpr uint32_t kAddrUnitSyncMovement = 0x08017F28u;
inline constexpr uint32_t kAddrUnitBeginAction = 0x080180ECu;
inline constexpr uint32_t kAddrUnitBeginReMoveAction = 0x08018164u;
inline constexpr uint32_t kAddrUnitUpdateUsedItem = 0x0801842Cu;
inline constexpr uint32_t kAddrUnitRevealAtPosition = 0x08018504u;
inline constexpr uint32_t kAddrGetUnitKeyItemSlotForTerrain = 0x08018524u;
inline constexpr uint32_t kAddrGetAidIconFromAttributes = 0x08018578u;
inline constexpr uint32_t kAddrGetUnitUseFlags = 0x080185ACu;
inline constexpr uint32_t kAddrGetEnemyFactionUseFlags = 0x08018624u;
inline constexpr uint32_t kAddrGetUnitLastItem = 0x080187BCu;
inline constexpr uint32_t kAddrApplyItemStatBoost = 0x0802CD28u;

// Inventory & key helpers.
int FindUnitItemSlot(GbaPtr<const Unit> unit, int iid);
bool UnitHasItem(GbaPtr<const Unit> unit, int item);
void UnitUpdateUsedItem(GbaPtr<Unit> unit, int item_slot);
int GetUnitKeyItemSlotForTerrain(GbaPtr<const Unit> unit, int terrain);
uint16_t GetUnitLastItem(GbaPtr<const Unit> unit);

// Unit initialization & autoleveling.
void UnitInitStats(GbaPtr<Unit> unit, GbaPtr<const PInfo> pinfo);
void UnitApplyAltPinfo(GbaPtr<Unit> unit);
void UnitInitSupports(GbaPtr<Unit> unit);
void UnitAutolevelWeaponExp(GbaPtr<Unit> unit, GbaPtr<const uint8_t> udef);
void UnitAutolevelCore(GbaPtr<Unit> unit, uint8_t jid, int level_count);
void UnitApplyBonusLevels(GbaPtr<Unit> unit, int level_count);
void UnitAutolevelPlayer(GbaPtr<Unit> unit);

// Rescue, drop, give, kill, movement sync, and action startup.
GbaPtr<Unit> GetUnitByPidInFaction(int pid, int faction);
bool CanUnitCarry(GbaPtr<const Unit> actor, GbaPtr<const Unit> target);
void UnitRescue(GbaPtr<Unit> actor, GbaPtr<Unit> target);
void UnitDropRescue(GbaPtr<Unit> actor, int x, int y);
void UnitGiveRescue(GbaPtr<Unit> actor, GbaPtr<Unit> target);
void KillUnit(GbaPtr<Unit> unit);
void UnitSyncMovement(GbaPtr<Unit> unit);
void UnitBeginAction(GbaPtr<Unit> unit);
void UnitBeginReMoveAction(GbaPtr<Unit> unit);
void UnitRevealAtPosition(GbaPtr<Unit> unit, int x, int y);

// Capability flags & stat boosters.
int GetAidIconFromAttributes(uint32_t attributes);
int GetUnitUseFlags(GbaPtr<const Unit> unit);
int GetEnemyFactionUseFlags();
int ApplyItemStatBoost(GbaPtr<Unit> unit, int item_slot);

// Registers all 26 unit-action functions in the dispatch table.
void RegisterActionFunctions();

}  // namespace fe7

#endif  // FE7_GAME_ACTION_ACTION_H_
