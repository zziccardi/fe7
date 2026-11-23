// FE7 US (AE7E) Enemy AI status-staff targeting, door/chest/antitoxin looting,
// and AI map-flood / working-movement-table helpers.
//
// Implements 33 Enemy AI functions (ROM 0x0803B0A4..0x0803C0FC):
//   - Warp, Restore, Silence, Sleep/Berserk, Barrier staff AI & target scoring
//   - Special-item dispatch table, Door Key, Chest Key/Lockpick, Antitoxin AI
//   - Door/chest reachability helpers and 16 AI map-flood wrappers

#ifndef FE7_GAME_AI_AI_STAFF_LOOT_H_
#define FE7_GAME_AI_AI_STAFF_LOOT_H_

#include <cstdint>

#include "game/ai/ai.h"
#include "game/ai/ai_actions.h"
#include "game/ai/ai_opcodes.h"
#include "game/unit.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// Fixed FE7 US ROM table & external-helper addresses used by AI staff/loot.
inline constexpr uint32_t kAiSpecialItemTableAddr = 0x081D3BDCu;
inline constexpr uint32_t kAiDoorTerrainListAddr = 0x08B98AE8u;
inline constexpr uint32_t kAiChestTerrainListAddr = 0x08B98AEAu;
inline constexpr uint32_t kAddrGetOffensiveStaffAccuracy = 0x0802A66Cu;

// FE7 US ROM addresses for the 33 decompiled AI staff, loot & map-flood
// functions. (kAddrAiTryDoorOrChestAction, kAddrAiMapFloodUnitExtended,
// kAddrAiMapFloodNoDoors, kAddrAiMapFloodNoDoorsUnit, kAddrAiMapFloodObstacles,
// and kAddrAiMapFloodRangeWithUnit are declared in ai_opcodes.h /
// ai_actions.h.)
inline constexpr uint32_t kAddrAiTryWarpStaff = 0x0803B0A4u;
inline constexpr uint32_t kAddrAiTryRestoreStaff = 0x0803B1FCu;
inline constexpr uint32_t kAddrAiUnitHasUsableWeaponOrStaff = 0x0803B340u;
inline constexpr uint32_t kAddrAiGetStatusStaffTargetScore = 0x0803B398u;
inline constexpr uint32_t kAddrAiTrySilenceStaff = 0x0803B3ECu;
inline constexpr uint32_t kAddrAiTrySleepOrBerserkStaff = 0x0803B578u;
inline constexpr uint32_t kAddrAiTryBarrierStaff = 0x0803B6FCu;
inline constexpr uint32_t kAddrAiFindBestWarpDestination = 0x0803B83Cu;
inline constexpr uint32_t kAddrAiGetSpecialItemTableIndex = 0x0803B9C4u;
inline constexpr uint32_t kAddrAiTryUseDoorKey = 0x0803BAA8u;
inline constexpr uint32_t kAddrAiTryUseChestKeyOrLockpick = 0x0803BB40u;
inline constexpr uint32_t kAddrAiTryUseAntitoxin = 0x0803BC28u;
inline constexpr uint32_t kAddrAiGetRangeCostIfPassable = 0x0803BC90u;
inline constexpr uint32_t kAddrAiFindReachableDoor = 0x0803BCE8u;
inline constexpr uint32_t kAddrAiFindReachableChest = 0x0803BD3Cu;
inline constexpr uint32_t kAddrAiFindReachableDoorOrChest = 0x0803BD64u;
inline constexpr uint32_t kAddrAiSetMovTableAllPassable = 0x0803BE04u;
inline constexpr uint32_t kAddrAiSetMovTableAllowOneTerrain = 0x0803BE3Cu;
inline constexpr uint32_t kAddrAiSetMovTableAllowTwoTerrains = 0x0803BE6Cu;
inline constexpr uint32_t kAddrAiMapFloodRangeExtended = 0x0803BED0u;
inline constexpr uint32_t kAddrAiMapFloodUnitAllPassable = 0x0803BF00u;
inline constexpr uint32_t kAddrAiMapFloodUnitAllPassableNoUnit = 0x0803BF30u;
inline constexpr uint32_t kAddrAiMapFloodRangeAllowDoor = 0x0803BFC0u;
inline constexpr uint32_t kAddrAiMapFloodRangeAllowDoorNoUnit = 0x0803BFF4u;
inline constexpr uint32_t kAddrAiMapFloodRangeAllowObstacles = 0x0803C024u;
inline constexpr uint32_t kAddrAiMapFloodRangeUnitMov = 0x0803C08Cu;
inline constexpr uint32_t kAddrAiUpdateIgnoreSpecialItemsFlag = 0x0803C0C8u;

// Status & utility staff AI (0x0803B0A4..0x0803B83C).
void AiTryWarpStaff(int slot, uint32_t target_filter_fn);
void AiTryRestoreStaff(int slot, uint32_t target_filter_fn);
bool AiUnitHasUsableWeaponOrStaff(GbaPtr<const Unit> unit);
int AiGetStatusStaffTargetScore(GbaPtr<const Unit> target);
void AiTrySilenceStaff(int slot, uint32_t target_filter_fn);
void AiTrySleepOrBerserkStaff(int slot, uint32_t target_filter_fn);
void AiTryBarrierStaff(int slot, uint32_t target_filter_fn);
bool AiFindBestWarpDestination(GbaPtr<AiPos> out_pos);

// Special-item, door, chest & antitoxin AI (0x0803B9C4..0x0803BD64).
int AiGetSpecialItemTableIndex(uint16_t item);
int AiTryDoorOrChestAction();
void AiTryUseDoorKey(int slot);
void AiTryUseChestKeyOrLockpick(int slot);
void AiTryUseAntitoxin(int slot);
uint8_t AiGetRangeCostIfPassable(int x, int y);
bool AiFindReachableDoor(GbaPtr<Unit> unit, GbaPtr<AiPos> out_pos);
bool AiFindReachableChest(GbaPtr<Unit> unit, GbaPtr<AiPos> out_pos);
bool AiFindReachableDoorOrChest(GbaPtr<Unit> unit, int flags,
                                GbaPtr<AiPos> out_pos);

// AI movement-table & map-flood helpers (0x0803BE04..0x0803C0FC).
void AiSetMovTableAllPassable(GbaPtr<const int8_t> base_table);
void AiSetMovTableAllowOneTerrain(GbaPtr<const int8_t> base_table,
                                  int terrain_id);
void AiSetMovTableAllowTwoTerrains(GbaPtr<const int8_t> base_table,
                                   int terrain_a, int terrain_b);
void AiMapFloodUnitExtended(GbaPtr<Unit> unit);
void AiMapFloodRangeExtended(GbaPtr<Unit> unit);
void AiMapFloodUnitAllPassable(GbaPtr<Unit> unit);
void AiMapFloodUnitAllPassableNoUnit(GbaPtr<Unit> unit);
void AiMapFloodNoDoors(int x, int y, GbaPtr<const int8_t> mov_table);
void AiMapFloodNoDoorsUnit(int x, int y, GbaPtr<Unit> unit);
void AiMapFloodRangeAllowDoor(GbaPtr<Unit> unit);
void AiMapFloodRangeAllowDoorNoUnit(GbaPtr<Unit> unit);
void AiMapFloodRangeAllowObstacles(GbaPtr<Unit> unit);
void AiMapFloodObstacles(GbaPtr<Unit> unit);
void AiMapFloodRangeUnitMov(GbaPtr<Unit> unit);
void AiUpdateIgnoreSpecialItemsFlag(GbaPtr<const Unit> unit);
void AiMapFloodRangeWithUnit(int x, int y, GbaPtr<Unit> unit);

// Registers all 33 AI staff, loot, and map-flood functions.
void RegisterAiStaffLootFunctions();

}  // namespace fe7

#endif  // FE7_GAME_AI_AI_STAFF_LOOT_H_
