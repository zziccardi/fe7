// FE7 US (AE7E) Enemy AI decision state, reachability, and combat scoring.

#include "game/ai/ai.h"

#include <cstdint>

#include "absl/base/nullability.h"
#include "game/battle.h"
#include "game/item.h"
#include "game/item/item.h"
#include "game/map/map.h"
#include "game/phase/phase.h"
#include "game/unit.h"
#include "game/unit/unit.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {
namespace {

struct FriendZoneCombatOffset {
  int8_t dx;
  int8_t dy;
  int8_t weight;
  uint8_t pad;
};

struct FriendZonePosOffset {
  int16_t dx;
  int16_t dy;
};

int AbsDiff(int a, int b) { return a >= b ? (a - b) : (b - a); }

GbaPtr<const AiCombatScoreCoefficients> ScoreCoeffsPtr() {
  return *GbaPtr<const GbaPtr<const AiCombatScoreCoefficients>>(
      kAiScoreCoeffPtrAddr);
}

GbaPtr<GbaPtr<uint8_t>> MapOtherPtr() {
  return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kAiMapOtherAddr);
}

// --- Dispatch thunks ---

uint32_t ThunkAiClearDecision(uint32_t, uint32_t, uint32_t, uint32_t,
                              const uint32_t* absl_nullable) {
  AiClearDecision();
  return 0;
}

uint32_t ThunkAiSetDecision(uint32_t r0, uint32_t r1, uint32_t r2, uint32_t r3,
                            const uint32_t* absl_nullable stack_args) {
  AiSetDecision(static_cast<int16_t>(r0), static_cast<int16_t>(r1),
                static_cast<uint8_t>(r2), static_cast<uint8_t>(r3),
                static_cast<uint8_t>(stack_args[0]),
                static_cast<uint8_t>(stack_args[1]),
                static_cast<uint8_t>(stack_args[2]));
  return 0;
}

uint32_t ThunkAiUpdateDecision(uint32_t r0, uint32_t r1, uint32_t r2,
                               uint32_t r3,
                               const uint32_t* absl_nullable stack_args) {
  AiUpdateDecision(static_cast<uint8_t>(r0), static_cast<uint8_t>(r1),
                   static_cast<uint8_t>(r2), static_cast<uint8_t>(r3),
                   static_cast<uint8_t>(stack_args[0]));
  return 0;
}

uint32_t ThunkAiIsWithinReachByBirdsEyeDistance(uint32_t r0, uint32_t r1,
                                                uint32_t r2, uint32_t,
                                                const uint32_t* absl_nullable) {
  return AiIsWithinReachByBirdsEyeDistance(GbaPtr<const Unit>(r0),
                                           GbaPtr<const Unit>(r1),
                                           static_cast<uint16_t>(r2))
             ? 1u
             : 0u;
}

uint32_t ThunkAiCouldReachByBirdsEyeDistance(uint32_t r0, uint32_t r1,
                                             uint32_t r2, uint32_t,
                                             const uint32_t* absl_nullable) {
  return AiCouldReachByBirdsEyeDistance(GbaPtr<const Unit>(r0),
                                        GbaPtr<const Unit>(r1),
                                        static_cast<uint16_t>(r2))
             ? 1u
             : 0u;
}

uint32_t ThunkAiIsInShortList(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                              const uint32_t* absl_nullable) {
  return AiIsInShortList(GbaPtr<const uint16_t>(r0), static_cast<uint16_t>(r1))
             ? 1u
             : 0u;
}

uint32_t ThunkAiIsInByteList(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                             const uint32_t* absl_nullable) {
  return AiIsInByteList(GbaPtr<const uint8_t>(r0), static_cast<uint8_t>(r1))
             ? 1u
             : 0u;
}

uint32_t ThunkAiGetJobRank(uint32_t r0, uint32_t, uint32_t, uint32_t,
                           const uint32_t* absl_nullable) {
  return AiGetJobRank(static_cast<uint8_t>(r0));
}

uint32_t ThunkAiUpdateUnitStaffFlags(uint32_t r0, uint32_t r1, uint32_t,
                                     uint32_t, const uint32_t* absl_nullable) {
  AiUpdateUnitStaffFlags(GbaPtr<Unit>(r0), static_cast<uint16_t>(r1));
  return 0;
}

uint32_t ThunkAiUpdateUnitItemFlags(uint32_t r0, uint32_t r1, uint32_t,
                                    uint32_t, const uint32_t* absl_nullable) {
  AiUpdateUnitItemFlags(GbaPtr<Unit>(r0), static_cast<uint16_t>(r1));
  return 0;
}

uint32_t ThunkAiCountNearbyAllies(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                  const uint32_t* absl_nullable) {
  AiCountNearbyAllies(GbaPtr<Unit>(r0));
  return 0;
}

uint32_t ThunkUnitInitAiFromInfo(uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                                 const uint32_t* absl_nullable) {
  UnitInitAiFromInfo(GbaPtr<Unit>(r0), GbaPtr<const uint8_t>(r1));
  return 0;
}

uint32_t ThunkAiFindBestPositionInRange(uint32_t r0, uint32_t, uint32_t,
                                        uint32_t,
                                        const uint32_t* absl_nullable) {
  return AiFindBestPositionInRange(GbaPtr<AiPos>(r0)) ? 1u : 0u;
}

uint32_t ThunkAiGetDamageDealtCombatScoreComponent(
    uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(AiGetDamageDealtCombatScoreComponent());
}

uint32_t ThunkAiGetOpponentLowHpScoreComponent(uint32_t, uint32_t, uint32_t,
                                               uint32_t,
                                               const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(AiGetOpponentLowHpScoreComponent());
}

uint32_t ThunkAiGetFriendZoneCombatScoreComponent(
    uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(AiGetFriendZoneCombatScoreComponent());
}

uint32_t ThunkAiGetTargetClassCombatScoreComponent(
    uint32_t, uint32_t, uint32_t, uint32_t, const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(AiGetTargetClassCombatScoreComponent());
}

uint32_t ThunkAiGetTurnCombatScoreComponent(uint32_t, uint32_t, uint32_t,
                                            uint32_t,
                                            const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(AiGetTurnCombatScoreComponent());
}

uint32_t ThunkAiGetDamageTakenScoreComponent(uint32_t, uint32_t, uint32_t,
                                             uint32_t,
                                             const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(AiGetDamageTakenScoreComponent());
}

uint32_t ThunkAiGetDangerScoreComponent(uint32_t, uint32_t, uint32_t, uint32_t,
                                        const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(AiGetDangerScoreComponent());
}

uint32_t ThunkAiGetLowHpScoreComponent(uint32_t, uint32_t, uint32_t, uint32_t,
                                       const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(AiGetLowHpScoreComponent());
}

uint32_t ThunkAiComputeCombatScore(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                   const uint32_t* absl_nullable) {
  AiComputeCombatScore(GbaPtr<AiCombatSimulationSt>(r0));
  return 0;
}

uint32_t ThunkAiGetInRangeCombatPositionScoreComponent(
    uint32_t r0, uint32_t r1, uint32_t r2, uint32_t,
    const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(AiGetInRangeCombatPositionScoreComponent(
      static_cast<int32_t>(r0), static_cast<int32_t>(r1),
      GbaPtr<const Unit>(r2)));
}

uint32_t ThunkAiGetTerrainCombatPositionScoreComponent(
    uint32_t r0, uint32_t r1, uint32_t, uint32_t,
    const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(AiGetTerrainCombatPositionScoreComponent(
      static_cast<int32_t>(r0), static_cast<int32_t>(r1)));
}

uint32_t ThunkAiGetFriendZonePositionScoreComponent(
    uint32_t r0, uint32_t r1, uint32_t, uint32_t,
    const uint32_t* absl_nullable) {
  return static_cast<uint32_t>(AiGetFriendZonePositionScoreComponent(
      static_cast<int32_t>(r0), static_cast<int32_t>(r1)));
}

uint32_t ThunkAiRefreshDangerMap(uint32_t, uint32_t, uint32_t, uint32_t,
                                 const uint32_t* absl_nullable) {
  AiRefreshDangerMap();
  return 0;
}

}  // namespace

void AiClearDecision() {
  GbaPtr<uint8_t> dec(kAiDecisionAddr);
  for (int i = 0; i <= 0xA; ++i) {
    dec[i] = 0;
  }
}

void AiSetDecision(int16_t x_move, int16_t y_move, uint8_t action_id,
                   uint8_t target_id, uint8_t item_slot, uint8_t x_target,
                   uint8_t y_target) {
  GbaPtr<AiDecision> dec(kAiDecisionAddr);
  dec->unit_id = *GbaPtr<const uint8_t>(kAiActiveUnitIdAddr);
  dec->x_move = static_cast<uint8_t>(x_move);
  dec->y_move = static_cast<uint8_t>(y_move);
  dec->action_id = action_id;
  dec->unit_target_id = target_id;
  dec->item_slot = item_slot;
  dec->x_target = x_target;
  dec->y_target = y_target;
  dec->action_performed = 1;
}

void AiUpdateDecision(uint8_t action_id, uint8_t target_id, uint8_t item_slot,
                      uint8_t x_target, uint8_t y_target) {
  GbaPtr<AiDecision> dec(kAiDecisionAddr);
  if (action_id != 0xFFu) dec->action_id = action_id;
  if (target_id != 0xFFu) dec->unit_target_id = target_id;
  if (item_slot != 0xFFu) dec->item_slot = item_slot;
  if (x_target != 0xFFu) dec->x_target = x_target;
  if (y_target != 0xFFu) dec->y_target = y_target;
  dec->action_performed = 1;
}

bool AiIsWithinReachByBirdsEyeDistance(GbaPtr<const Unit> unit,
                                       GbaPtr<const Unit> target,
                                       uint16_t item) {
  const int dist = AbsDiff(unit->x, target->x) + AbsDiff(unit->y, target->y);
  const int max_reach =
      unit->bonus_mov + unit->jinfo->base_mov + GetItemMaxRange(item);
  return dist <= max_reach;
}

bool AiCouldReachByBirdsEyeDistance(GbaPtr<const Unit> unit,
                                    GbaPtr<const Unit> target, uint16_t item) {
  const int dist = AbsDiff(unit->x, target->x) + AbsDiff(unit->y, target->y);
  const int max_reach = unit->bonus_mov + unit->jinfo->base_mov +
                        target->bonus_mov + target->jinfo->base_mov +
                        GetItemMaxRange(item);
  return dist <= max_reach;
}

bool AiIsInShortList(GbaPtr<const uint16_t> list, uint16_t item) {
  for (int i = 0; list[i] != 0u; ++i) {
    if (list[i] == item) {
      return true;
    }
  }
  return false;
}

bool AiIsInByteList(GbaPtr<const uint8_t> list, uint8_t item) {
  for (int i = 0; list[i] != 0u; ++i) {
    if (list[i] == item) {
      return true;
    }
  }
  return false;
}

uint8_t AiGetJobRank(uint8_t jid) {
  GbaPtr<const GbaPtr<const uint8_t>> groups(kAiJobRankTableAddr);
  uint8_t rank = 0;
  while (groups[rank]) {
    GbaPtr<const uint8_t> ptr = groups[rank];
    for (int i = 0; ptr[i] != 0u; ++i) {
      if (ptr[i] == jid) {
        return rank;
      }
    }
    ++rank;
  }
  return rank;
}

void AiUpdateUnitStaffFlags(GbaPtr<Unit> unit, uint16_t item) {
  if ((GetItemAttributes(item) & kItemAttrStaff) == 0u) {
    return;
  }
  uint8_t flag = 0x02u;
  switch (GetItemIid(item)) {
    case 0x50:  // Unlock
      flag = 0x08u;
      break;
    case 0x51:  // Barrier
      flag = 0x10u;
      break;
    case 0x52:  // Warp
      flag = 0x20u;
      break;
    default:
      break;
  }
  unit->ai_flags = static_cast<uint8_t>(unit->ai_flags | flag);
}

void AiUpdateUnitItemFlags(GbaPtr<Unit> unit, uint16_t item) {
  uint8_t flags = 0;
  if ((GetItemAttributes(item) & kItemAttrWeapon) != 0u &&
      GetItemMaxRange(item) > 1) {
    flags = 0x40u;
  }
  const int effect = GetItemEffect(item);
  if ((effect >= 1 && effect <= 5) || effect == 0x21 || effect == 0x22) {
    flags |= 0x04u;
  }
  unit->ai_flags = static_cast<uint8_t>(unit->ai_flags | flags);
}

void AiCountNearbyAllies(GbaPtr<Unit> unit) {
  GbaPtr<GbaPtr<uint8_t>> mov_map =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr);
  MapFill(mov_map, 0);
  MapSetInRange(unit->x, unit->y, 1, 8);

  GbaPtr<const MapSize> sz(kMapSizeAddr);
  GbaPtr<GbaPtr<uint8_t>> unit_map =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr);
  uint8_t count = 0;
  for (int y = sz->y - 1; y >= 0; --y) {
    for (int x = sz->x - 1; x >= 0; --x) {
      if (static_cast<int8_t>(mov_map[y][x]) == 0) {
        continue;
      }
      const uint8_t uid = unit_map[y][x];
      if (uid != 0u && AreUnitIdsAllied(unit->id, uid)) {
        ++count;
      }
    }
  }
  unit->unk_46 = count;
}

void UnitInitAiFromInfo(GbaPtr<Unit> unit, GbaPtr<const uint8_t> udef) {
  unit->ai_a = udef[0x0C];
  unit->ai_b = udef[0x0D];
  unit->ai_config =
      static_cast<uint16_t>((unit->ai_config & 0xFFF8u) | udef[0x0E] |
                            (static_cast<uint32_t>(udef[0x0F]) << 8));
}

bool AiFindBestPositionInRange(GbaPtr<AiPos> out_pos) {
  GbaPtr<const MapSize> sz(kMapSizeAddr);
  GbaPtr<GbaPtr<uint8_t>> mov_map =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr);
  GbaPtr<GbaPtr<uint8_t>> rng_map =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapRangeAddr);
  GbaPtr<GbaPtr<uint8_t>> unit_map =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr);
  GbaPtr<GbaPtr<uint8_t>> other_map = MapOtherPtr();
  const uint8_t active_id = *GbaPtr<const uint8_t>(kAiActiveUnitIdAddr);

  uint32_t best_score = 0;
  for (int y = sz->y - 1; y >= 0; --y) {
    for (int x = sz->x - 1; x >= 0; --x) {
      if (mov_map[y][x] > 0x78u) {
        continue;
      }
      if (static_cast<int8_t>(rng_map[y][x]) == 0) {
        continue;
      }
      const uint8_t uid = unit_map[y][x];
      if (uid != 0u && uid != active_id) {
        continue;
      }

      const int32_t raw_score = AiGetTerrainCombatPositionScoreComponent(x, y) +
                                AiGetFriendZonePositionScoreComponent(x, y) -
                                static_cast<int32_t>(other_map[y][x] >> 3);
      const uint32_t biased = static_cast<uint32_t>(raw_score) + 0x7FFFFFFFu;
      if (biased > best_score) {
        out_pos->x = static_cast<int16_t>(x);
        out_pos->y = static_cast<int16_t>(y);
        best_score = biased;
      }
    }
  }
  return best_score != 0u;
}

int AiGetDamageDealtCombatScoreComponent() {
  GbaPtr<const BattleUnit> target(kBattleTargetAddr);
  if (target->unit.hp == 0) {
    return 50;
  }
  GbaPtr<const BattleUnit> actor(kBattleActorAddr);
  int expected = (actor->battle_attack - target->battle_defense) *
                 actor->battle_effective_hit_rate;
  if (expected < 0) {
    expected = 0;
  }
  int score = (expected / 100) * ScoreCoeffsPtr()->coeff_damage_dealt;
  if (score > 40) {
    score = 40;
  }
  return score;
}

int AiGetOpponentLowHpScoreComponent() {
  GbaPtr<const BattleUnit> target(kBattleTargetAddr);
  int score = (20 - target->unit.hp) * ScoreCoeffsPtr()->coeff_low_hp_opponent;
  return score < 0 ? 0 : score;
}

int AiGetFriendZoneCombatScoreComponent() {
  GbaPtr<const BattleUnit> actor(kBattleActorAddr);
  GbaPtr<const MapSize> sz(kMapSizeAddr);
  GbaPtr<GbaPtr<uint8_t>> unit_map =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr);
  GbaPtr<const FriendZoneCombatOffset> tbl(kAiFriendZoneCombatTableAddr);

  int sum = 0;
  for (int i = 0; static_cast<uint8_t>(tbl[i].dx) != 0x7Fu; ++i) {
    const uint8_t nx = static_cast<uint8_t>(
        static_cast<uint8_t>(actor->unit.x) + static_cast<uint8_t>(tbl[i].dx));
    const uint8_t ny = static_cast<uint8_t>(
        static_cast<uint8_t>(actor->unit.y) + static_cast<uint8_t>(tbl[i].dy));
    if (static_cast<int>(nx) >= sz->x || static_cast<int>(ny) >= sz->y) {
      continue;
    }
    const uint8_t uid = unit_map[ny][nx];
    if (uid != 0u && AreUnitIdsAllied(actor->unit.id, uid)) {
      sum += tbl[i].weight;
    }
  }

  int score = sum * ScoreCoeffsPtr()->coeff_friend_zone;
  if (score > 10) {
    score = 10;
  }
  return score;
}

int AiGetTargetClassCombatScoreComponent() {
  GbaPtr<const BattleUnit> target(kBattleTargetAddr);
  const uint8_t rank = AiGetJobRank(target->unit.jinfo->id);
  GbaPtr<const AiCombatScoreCoefficients> coeffs = ScoreCoeffsPtr();
  int score = coeffs->coeff_class_rank_bonus * coeffs->class_rank_bonuses[rank];
  if (score > 20) {
    score = 20;
  }
  return score;
}

int AiGetTurnCombatScoreComponent() {
  const uint16_t turn = *GbaPtr<const uint16_t>(kAiPlayStatusAddr + 0x10u);
  return static_cast<int>(turn) * ScoreCoeffsPtr()->coeff_turn_number;
}

int AiGetDamageTakenScoreComponent() {
  GbaPtr<const BattleUnit> target(kBattleTargetAddr);
  if (target->weapon == 0u) {
    return -10;
  }
  GbaPtr<const BattleUnit> actor(kBattleActorAddr);
  int expected = (target->battle_attack - actor->battle_defense) *
                 target->battle_effective_hit_rate;
  if (expected < 0) {
    expected = 0;
  }
  int score = (expected / 100) * ScoreCoeffsPtr()->coeff_damage_taken;
  if (score > 40) {
    score = 40;
  }
  return score;
}

int AiGetDangerScoreComponent() {
  GbaPtr<const BattleUnit> actor(kBattleActorAddr);
  const uint8_t danger = MapOtherPtr()[actor->unit.y][actor->unit.x];
  int score = (danger >> 3) * ScoreCoeffsPtr()->coeff_danger;
  if (score > 20) {
    score = 20;
  }
  return score;
}

int AiGetLowHpScoreComponent() {
  GbaPtr<const BattleUnit> actor(kBattleActorAddr);
  int score = (20 - actor->unit.hp) * ScoreCoeffsPtr()->coeff_low_hp_self;
  return score < 0 ? 0 : score;
}

void AiComputeCombatScore(GbaPtr<AiCombatSimulationSt> st) {
  const uint8_t weight_id = *GbaPtr<const uint8_t>(kAiStateAddr + 0x7Du);
  *GbaPtr<uint32_t>(kAiScoreCoeffPtrAddr) =
      kAiCombatScoreCoeffTableAddr +
      static_cast<uint32_t>(weight_id) * sizeof(AiCombatScoreCoefficients);

  const int dmg_dealt = AiGetDamageDealtCombatScoreComponent();
  int score = dmg_dealt;
  score += AiGetOpponentLowHpScoreComponent();
  score += AiGetFriendZoneCombatScoreComponent();
  score += AiGetTargetClassCombatScoreComponent();
  score += AiGetTurnCombatScoreComponent();
  score -= AiGetDamageTakenScoreComponent();
  score -= AiGetDangerScoreComponent();
  score -= AiGetLowHpScoreComponent();

  if (score < 0) {
    score = 0;
  }
  if (score != 0) {
    score *= 40;
  } else {
    score = dmg_dealt;
  }
  st->score = static_cast<uint32_t>(score);
}

int AiGetInRangeCombatPositionScoreComponent(int x, int y,
                                             GbaPtr<const Unit> target) {
  const int dist = AbsDiff(target->x, x) + AbsDiff(target->y, y);
  const uint16_t weapon = static_cast<uint16_t>(GetUnitEquippedWeapon(target));
  if (weapon == 0u) {
    return 0;
  }
  if (dist > GetItemMaxRange(weapon) || dist < GetItemMinRange(weapon)) {
    return 50;
  }
  return 0;
}

int AiGetTerrainCombatPositionScoreComponent(int x, int y) {
  GbaPtr<GbaPtr<uint8_t>> terrain_map =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr);
  const uint8_t tid = terrain_map[y][x];
  GbaPtr<const Unit> active =
      *GbaPtr<const GbaPtr<const Unit>>(kAiActiveUnitAddr);
  GbaPtr<const JInfo> j = active->jinfo;
  return static_cast<int>(j->avo_terrain_table[tid]) +
         static_cast<int>(j->def_terrain_table[tid]) +
         static_cast<int>(j->res_terrain_table[tid]);
}

int AiGetFriendZonePositionScoreComponent(int x, int y) {
  GbaPtr<GbaPtr<uint8_t>> unit_map =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr);
  const uint8_t active_id = *GbaPtr<const uint8_t>(kAiActiveUnitIdAddr);
  GbaPtr<const FriendZonePosOffset> tbl(kAiFriendZonePosTableAddr);

  int score = 0;
  for (int i = 0; tbl[i].dx != 9999; ++i) {
    const uint8_t uid = unit_map[y + tbl[i].dy][x + tbl[i].dx];
    if (uid == 0u) {
      continue;
    }
    score += AreUnitIdsAllied(active_id, uid) ? 5 : -5;
  }
  return score;
}

void AiMakeMoveRangeMapsForUnitAndWeapon(GbaPtr<const Unit> unit,
                                         uint16_t weapon) {
  MapFloodUnit(unit);
  GbaPtr<GbaPtr<uint8_t>> rng_map =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapRangeAddr);
  MapFill(rng_map, 0);

  GbaPtr<const MapSize> sz(kMapSizeAddr);
  GbaPtr<GbaPtr<uint8_t>> mov_map =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr);
  for (int y = sz->y - 1; y >= 0; --y) {
    for (int x = sz->x - 1; x >= 0; --x) {
      if (mov_map[y][x] > 0x78u) {
        continue;
      }
      const int min_r = static_cast<int16_t>(GetItemMinRange(weapon));
      const int max_r = static_cast<int16_t>(GetItemMaxRange(weapon));
      MapSetInRange(x, y, min_r, max_r);
    }
  }
}

void AiFillDangerMap() {
  // Mirror FE7 US quirk: `best_item` (r8) and `best_might` (sb) are initialized
  // once before the 1..0xBF unit loop rather than reset per unit.
  uint16_t best_item = 0;
  uint8_t best_might = 0;
  const uint8_t active_id = *GbaPtr<const uint8_t>(kAiActiveUnitIdAddr);
  GbaPtr<const Unit> active =
      *GbaPtr<const GbaPtr<const Unit>>(kAiActiveUnitAddr);
  GbaPtr<const MapSize> sz(kMapSizeAddr);
  GbaPtr<GbaPtr<uint8_t>> rng_map =
      *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapRangeAddr);
  GbaPtr<GbaPtr<uint8_t>> other_map = MapOtherPtr();

  for (int id = 1; id <= 0xBF; ++id) {
    GbaPtr<Unit> unit = GetUnit(id);
    if (!unit || !unit->pinfo) {
      continue;
    }
    if ((unit->flags & 0x0001000Du) != 0u) {
      continue;
    }
    if (AreUnitIdsAllied(active_id, static_cast<uint8_t>(unit->id))) {
      continue;
    }

    GbaPtr<const Unit> unit_c(unit.addr);
    for (int slot = 0; slot < kUnitItemCount && unit->items[slot] != 0u;
         ++slot) {
      const uint16_t item = unit->items[slot];
      if (!CanUnitUseWeapon(unit_c, item)) {
        continue;
      }
      if (GetItemMight(item) > static_cast<int>(best_might)) {
        best_item = item;
        best_might = static_cast<uint8_t>(GetItemMight(item));
      }
    }

    if (best_item == 0u) {
      continue;
    }
    if (!AiCouldReachByBirdsEyeDistance(active, unit_c, best_item)) {
      continue;
    }

    AiMakeMoveRangeMapsForUnitAndWeapon(unit_c, best_item);
    for (int y = sz->y - 1; y >= 0; --y) {
      for (int x = sz->x - 1; x >= 0; --x) {
        if (static_cast<int8_t>(rng_map[y][x]) == 0) {
          continue;
        }
        const int danger_add =
            (GetUnitPower(unit_c) + static_cast<int>(best_might)) >> 1;
        other_map[y][x] = static_cast<uint8_t>(other_map[y][x] + danger_add);
      }
    }
  }
}

void AiRefreshDangerMap() {
  GbaPtr<uint8_t> danger_filled(kAiStateAddr + 0x7Au);
  if (*danger_filled != 0u) {
    return;
  }
  *danger_filled = 1u;
  MapFill(MapOtherPtr(), 0);
  AiFillDangerMap();
}

void RegisterAiFunctions() {
  RegisterNative(kAddrAiMakeMoveRangeMapsForUnitAndWeapon,
                 [](uint32_t r0, uint32_t r1, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   AiMakeMoveRangeMapsForUnitAndWeapon(
                       GbaPtr<const Unit>(r0), static_cast<uint16_t>(r1));
                   return 0u;
                 });
  RegisterNative(kAddrAiFillDangerMap,
                 [](uint32_t, uint32_t, uint32_t, uint32_t,
                    const uint32_t* absl_nullable) -> uint32_t {
                   AiFillDangerMap();
                   return 0u;
                 });
  RegisterNative(kAddrAiClearDecision, ThunkAiClearDecision);
  RegisterNative(kAddrAiSetDecision, ThunkAiSetDecision);
  RegisterNative(kAddrAiUpdateDecision, ThunkAiUpdateDecision);
  RegisterNative(kAddrAiIsWithinReachByBirdsEyeDistance,
                 ThunkAiIsWithinReachByBirdsEyeDistance);
  RegisterNative(kAddrAiCouldReachByBirdsEyeDistance,
                 ThunkAiCouldReachByBirdsEyeDistance);
  RegisterNative(kAddrAiIsInShortList, ThunkAiIsInShortList);
  RegisterNative(kAddrAiIsInByteList, ThunkAiIsInByteList);
  RegisterNative(kAddrAiGetJobRank, ThunkAiGetJobRank);
  RegisterNative(kAddrAiUpdateUnitStaffFlags, ThunkAiUpdateUnitStaffFlags);
  RegisterNative(kAddrAiUpdateUnitItemFlags, ThunkAiUpdateUnitItemFlags);
  RegisterNative(kAddrAiCountNearbyAllies, ThunkAiCountNearbyAllies);
  RegisterNative(kAddrUnitInitAiFromInfo, ThunkUnitInitAiFromInfo);
  RegisterNative(kAddrAiFindBestPositionInRange,
                 ThunkAiFindBestPositionInRange);
  RegisterNative(kAddrAiGetDamageDealtCombatScoreComponent,
                 ThunkAiGetDamageDealtCombatScoreComponent);
  RegisterNative(kAddrAiGetOpponentLowHpScoreComponent,
                 ThunkAiGetOpponentLowHpScoreComponent);
  RegisterNative(kAddrAiGetFriendZoneCombatScoreComponent,
                 ThunkAiGetFriendZoneCombatScoreComponent);
  RegisterNative(kAddrAiGetTargetClassCombatScoreComponent,
                 ThunkAiGetTargetClassCombatScoreComponent);
  RegisterNative(kAddrAiGetTurnCombatScoreComponent,
                 ThunkAiGetTurnCombatScoreComponent);
  RegisterNative(kAddrAiGetDamageTakenScoreComponent,
                 ThunkAiGetDamageTakenScoreComponent);
  RegisterNative(kAddrAiGetDangerScoreComponent,
                 ThunkAiGetDangerScoreComponent);
  RegisterNative(kAddrAiGetLowHpScoreComponent, ThunkAiGetLowHpScoreComponent);
  RegisterNative(kAddrAiComputeCombatScore, ThunkAiComputeCombatScore);
  RegisterNative(kAddrAiGetInRangeCombatPositionScoreComponent,
                 ThunkAiGetInRangeCombatPositionScoreComponent);
  RegisterNative(kAddrAiGetTerrainCombatPositionScoreComponent,
                 ThunkAiGetTerrainCombatPositionScoreComponent);
  RegisterNative(kAddrAiGetFriendZonePositionScoreComponent,
                 ThunkAiGetFriendZonePositionScoreComponent);
  RegisterNative(kAddrAiRefreshDangerMap, ThunkAiRefreshDangerMap);
}

}  // namespace fe7
