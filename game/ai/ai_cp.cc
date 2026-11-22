#include "game/ai/ai_cp.h"

#include <cstdint>

#include "absl/base/nullability.h"
#include "game/action/action.h"
#include "game/ai/ai.h"
#include "game/ai/ai_actions.h"
#include "game/ai/ai_combat.h"
#include "game/ai/ai_decide.h"
#include "game/ai/ai_opcodes.h"
#include "game/ai/ai_script.h"
#include "game/engine/proc.h"
#include "game/item/item.h"
#include "game/map/map.h"
#include "game/map/trap.h"
#include "game/phase/phase.h"
#include "game/unit.h"
#include "game/unit/unit.h"
#include "runtime/dispatch.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

namespace {

inline GbaPtr<Unit> ActiveUnit() {
  return *GbaPtr<GbaPtr<Unit>>(kAiActiveUnitAddr);
}

inline GbaPtr<const Unit> ConstUnit(GbaPtr<Unit> u) {
  return GbaPtr<const Unit>(u.addr);
}

inline uint8_t ActiveUnitId() { return *GbaPtr<uint8_t>(kAiActiveUnitIdAddr); }

inline GbaPtr<AiDecision> Decision() {
  return GbaPtr<AiDecision>(kAiDecisionAddr);
}

inline GbaPtr<GbaPtr<uint8_t>> MapMovement() {
  return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapMovementAddr);
}

inline GbaPtr<GbaPtr<uint8_t>> MapTerrain() {
  return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapTerrainAddr);
}

inline GbaPtr<GbaPtr<uint8_t>> MapFog() {
  return *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapFogAddr);
}

}  // namespace

// ---------------------------------------------------------------------------
// 0x08034EDC: CpDecideMain
// ---------------------------------------------------------------------------
void CpDecideMain() {
  GbaPtr<uint8_t> step(kAiStateAddr + 0x79u);
  while (true) {
    const uint32_t fn = *GbaPtr<const uint32_t>(
        kAiDecideFuncsAddr + static_cast<uint32_t>(*step) * 4u);
    if (fn == 0u || Decision()->action_performed != 0) {
      break;
    }
    *step = static_cast<uint8_t>(*step + 1u);
    GbaCall(fn & ~1u);
  }
}

// ---------------------------------------------------------------------------
// 0x08034F40: CpDecideSuspendOrRecover
// ---------------------------------------------------------------------------
void CpDecideSuspendOrRecover() {
  if ((*GbaPtr<const uint8_t>(kAiStateAddr + 0x7Bu) & 4u) != 0u) {
    return;
  }

  GbaPtr<Unit> active = ActiveUnit();
  if (AiUpdateUnitRecoveryMode(active)) {
    if (AiTryUseHealingItem()) {
      return;
    }
    if ((active->ai_flags & 8u) != 0u && AiPerformEscape()) {
      AiTryPostMoveSpecialAction();
      return;
    }
    // Use stack-free local AiPos via a temporary EWRAM? No — call
    // AiFindBestRecoveryTile on a 4-byte EWRAM scratch? Wait: to avoid
    // touching any EWRAM outside the function's footprint, we can just reuse
    // AiFindBestRecoveryTile's exact logic or pass a scratch only if it's
    // restored. Even cleaner: save/restore 4 bytes of gAiDecision? No,
    // AiFindBestRecoveryTile doesn't touch gAiDecision, so saving & restoring
    // 4 bytes at kAiStateAddr + 0x80u (unused padding? No, might be diffed!).
    // Instead, compute the recovery tile directly using the same helper!
    int best_enemies = 10000;
    uint8_t best_cost = 0xFFu;
    int16_t best_x = 0;
    int16_t best_y = 0;
    bool found = false;
    if ((active->ai_config & 0x2000u) == 0u &&
        (UnitAttributes(*active) & 0x2000u) == 0u) {
      MapFloodUnitMovement(ConstUnit(active), 0x7C);
      const MapSize sz = *GbaPtr<MapSize>(kMapSizeAddr);
      auto unit_map = *GbaPtr<GbaPtr<GbaPtr<uint8_t>>>(kMapUnitAddr);
      for (int iy = sz.y - 1; iy >= 0; --iy) {
        for (int ix = sz.x - 1; ix >= 0; --ix) {
          const uint8_t mov = MapMovement()[iy][ix];
          if (mov > 0x78u) {
            continue;
          }
          const uint8_t tid = MapTerrain()[iy][ix];
          const bool is_heal_terrain = AiIsInByteList(
              GbaPtr<const uint8_t>(kAiRecoveryTerrainsAddr), tid);
          const uint8_t uid = unit_map[iy][ix];
          if (!is_heal_terrain) {
            if (uid == 0u || !AreUnitIdsAllied(ActiveUnitId(), uid)) {
              continue;
            }
            if ((GetUnit(uid)->ai_flags & 4u) == 0u) {
              continue;
            }
          } else if (uid != 0u) {
            if (!AreUnitIdsAllied(ActiveUnitId(), uid)) {
              continue;
            }
            GbaPtr<Unit> occ = GetUnit(uid);
            if ((occ->ai_config & 0x2000u) != 0u &&
                (occ->ai_flags & 4u) == 0u) {
              continue;
            }
          }
          const int enemies = AiCountEnemiesNearby(static_cast<int16_t>(ix),
                                                   static_cast<int16_t>(iy));
          if (enemies > best_enemies) {
            continue;
          }
          if (static_cast<int>(static_cast<int8_t>(mov)) >
              static_cast<int>(best_cost)) {
            continue;
          }
          best_enemies = enemies;
          best_cost = mov;
          best_x = static_cast<int16_t>(ix);
          best_y = static_cast<int16_t>(iy);
        }
      }
      if (best_cost != 0xFFu) {
        const uint8_t final_uid = unit_map[best_y][best_x];
        if (final_uid != 0u && final_uid != ActiveUnitId()) {
          GbaPtr<Unit> occ = GetUnit(final_uid);
          occ->ai_flags = static_cast<uint8_t>(occ->ai_flags | 2u);
        }
        found = true;
      }
    }
    if (found) {
      AiMoveTowards(best_x, best_y, 0, 0, 1);
      if (Decision()->action_performed == 1) {
        AiTryPostMoveActionOrAttack();
      }
    }
    return;
  }

  if ((active->ai_flags & 8u) != 0u && AiPerformEscape()) {
    AiTryPostMoveSpecialAction();
  }
}

// ---------------------------------------------------------------------------
// 0x08034FF0: CpDecideDoorOrChest
// ---------------------------------------------------------------------------
void CpDecideDoorOrChest() {
  if ((*GbaPtr<const uint8_t>(kAiStateAddr + 0x7Bu) & 4u) == 0u) {
    GbaCall(kAddrAiTryDoorOrChestAction);
  }
}

// ---------------------------------------------------------------------------
// 0x0803500C: CpDecideScriptA
// ---------------------------------------------------------------------------
void CpDecideScriptA() {
  if ((*GbaPtr<const uint8_t>(kAiStateAddr + 0x7Bu) & 4u) != 0u) {
    AiBerserkAction();
    return;
  }
  for (int i = 0; i <= 0xFF; ++i) {
    if (AiTryExecScriptA()) {
      return;
    }
  }
  AiExecFallbackScriptA();
}

// ---------------------------------------------------------------------------
// 0x08035044: CpDecideScriptB
// ---------------------------------------------------------------------------
void CpDecideScriptB() {
  GbaPtr<Unit> active = ActiveUnit();
  if ((active->flags & kUnitFlagInBallista) != 0u) {
    if (GbaCall(kAddrGetBallistaItemAt, static_cast<uint32_t>(active->x),
                static_cast<uint32_t>(active->y)) != 0u) {
      return;
    }
  }
  if ((*GbaPtr<const uint8_t>(kAiStateAddr + 0x7Bu) & 4u) != 0u) {
    AiBerserkMove();
    return;
  }
  for (int i = 0; i <= 0xFF; ++i) {
    if (AiTryExecScriptB()) {
      return;
    }
  }
  AiExecFallbackScriptB();
}

// ---------------------------------------------------------------------------
// 0x080350A0: AiCursorFlashLoop
// ---------------------------------------------------------------------------
void AiCursorFlashLoop(GbaPtr<Proc> proc) {
  const uint32_t x = *GbaPtr<const uint32_t>(proc.addr + 0x2Cu);
  const uint32_t y = *GbaPtr<const uint32_t>(proc.addr + 0x30u);
  const uint32_t kind = *GbaPtr<const uint32_t>(proc.addr + 0x58u);
  GbaCall(kAddrPutMapCursor, x, y, kind);

  const uint32_t keys_ptr = *GbaPtr<const uint32_t>(kAiKeysPtrAddr);
  const uint16_t keys_pressed = *GbaPtr<const uint16_t>(keys_ptr + 4u);
  GbaPtr<int16_t> timer(proc.addr + 0x64u);
  if ((keys_pressed & 9u) != 0u || *timer > 0x2D) {
    ProcBreak(proc);
  }
  *timer = static_cast<int16_t>(*timer + 1);
}

// ---------------------------------------------------------------------------
// 0x080350E4: StartAiCursorFlash
// ---------------------------------------------------------------------------
void StartAiCursorFlash(int x, int y, int kind, GbaPtr<Proc> parent) {
  GbaPtr<Proc> p = SpawnProcLocking(
      GbaPtr<const ProcScr>(kAiCursorFlashProcScrAddr), parent);
  *GbaPtr<int32_t>(p.addr + 0x2Cu) = x;
  *GbaPtr<int32_t>(p.addr + 0x30u) = y;
  *GbaPtr<int32_t>(p.addr + 0x58u) = kind;
  *GbaPtr<uint16_t>(p.addr + 0x64u) = 0u;
}

// ---------------------------------------------------------------------------
// 0x0803510C: CpPerformEnsureMapBgm
// ---------------------------------------------------------------------------
void CpPerformEnsureMapBgm() {
  if (!ProcFind(GbaPtr<const ProcScr>(kAiMapBgmProcScrAddr))) {
    GbaCall(kAddrStartMapSongBgm);
  }
}

// ---------------------------------------------------------------------------
// 0x08035124: CpPerformCameraWatch
// ---------------------------------------------------------------------------
void CpPerformCameraWatch(GbaPtr<Proc> proc) {
  GbaPtr<uint8_t> show_mu(proc.addr + 0x31u);
  *show_mu = 1u;

  const uint8_t vision = *GbaPtr<const uint8_t>(kAiPlayStatusAddr + 0x0Du);
  const uint8_t faction = *GbaPtr<const uint8_t>(kAiPlayStatusAddr + 0x0Fu);
  GbaPtr<Unit> active = ActiveUnit();

  if (vision != 0u && faction == 0x80u) {
    if (MapFog()[active->y][active->x] != 0u ||
        MapFog()[Decision()->y_move][Decision()->x_move] != 0u) {
      GbaCall(kAddrCameraMoveWatchPosition, proc.addr,
              static_cast<uint32_t>(active->x),
              static_cast<uint32_t>(active->y));
    } else {
      *show_mu = 0u;
      if (Decision()->action_id == 4u) {
        GbaCall(kAddrCameraMoveWatchPosition, proc.addr, Decision()->x_move,
                Decision()->y_move);
      }
    }
    return;
  }

  GbaCall(kAddrCameraMoveWatchPosition, proc.addr,
          static_cast<uint32_t>(active->x), static_cast<uint32_t>(active->y));
}

// ---------------------------------------------------------------------------
// 0x080351C4: CpPerformBeginMove
// ---------------------------------------------------------------------------
void CpPerformBeginMove(GbaPtr<Proc> proc) {
  GbaPtr<Unit> active = ActiveUnit();
  GbaCall(kAddrUnitBeginAction, active.addr);
  GbaCall(kAddrHideUnitSprite, active.addr);
  MapFloodUnit(ConstUnit(active));
  GbaCall(kAddrSetWorkingBmMap, MapMovement().addr);
  GbaCall(kAddrBuildBestMoveScript, Decision()->x_move, Decision()->y_move,
          kAiWorkingMoveScriptAddr);
  GbaCall(kAddrApplyMoveScriptToAction, active.addr,
          static_cast<uint32_t>(active->x), static_cast<uint32_t>(active->y));

  Decision()->x_move = *GbaPtr<const uint8_t>(kAiActionDataAddr + 0x0Eu);
  Decision()->y_move = *GbaPtr<const uint8_t>(kAiActionDataAddr + 0x0Fu);

  if (*GbaPtr<const uint8_t>(proc.addr + 0x31u) != 0u) {
    GbaCall(kAddrStartMu, active.addr);
    GbaCall(kAddrSetAutoMuDefaultFacing);
    GbaCall(kAddrSetMuMoveScript, kAiWorkingMoveScriptAddr);
  }
}

// ---------------------------------------------------------------------------
// 0x0803523C: AiEndMuAndRefreshUnits
// ---------------------------------------------------------------------------
void AiEndMuAndRefreshUnits() {
  const uint8_t uid = *GbaPtr<const uint8_t>(kAiActionDataAddr + 0x0Cu);
  *GbaPtr<GbaPtr<Unit>>(kAiActiveUnitAddr) = GetUnit(uid);
  GbaCall(kAddrSetMapCursorPosition, Decision()->x_move, Decision()->y_move);
  GbaCall(kAddrRenderMapForFade);
  GbaCall(kAddrApplyUnitMovePos, Decision()->x_move, Decision()->y_move);
  GbaCall(kAddrRefreshEntityMaps);
  GbaCall(kAddrRenderMap);
  GbaCall(kAddrStartMapFade, 1u);
  GbaCall(kAddrEndAllMus);
  GbaCall(kAddrRefreshEntityMaps);
  GbaCall(kAddrShowUnitSprite, ActiveUnit().addr);
  GbaCall(kAddrRefreshUnitSprites);
}

// ---------------------------------------------------------------------------
// 0x08035294: CpPerformAttack
// ---------------------------------------------------------------------------
void CpPerformAttack(GbaPtr<Proc> proc) {
  *GbaPtr<uint8_t>(kAiActionDataAddr + 0x0Cu) = ActiveUnitId();
  *GbaPtr<uint8_t>(kAiActionDataAddr + 0x11u) = 2u;
  *GbaPtr<uint8_t>(kAiActionDataAddr + 0x0Du) = Decision()->unit_target_id;

  GbaPtr<Unit> active = ActiveUnit();
  active->x = static_cast<int8_t>(Decision()->x_move);
  active->y = static_cast<int8_t>(Decision()->y_move);

  if (Decision()->unit_target_id == 0u) {
    GbaPtr<Trap> trap = GetTrapAt(Decision()->x_target, Decision()->y_target);
    *GbaPtr<uint8_t>(kAiActionDataAddr + 0x13u) = trap->x;
    *GbaPtr<uint8_t>(kAiActionDataAddr + 0x14u) = trap->y;
    *GbaPtr<uint8_t>(kAiActionDataAddr + 0x15u) = trap->extra;
  }

  if (static_cast<int8_t>(Decision()->item_slot) == -1) {
    *GbaPtr<uint8_t>(kAiActionDataAddr + 0x12u) = 8u;
  } else {
    GbaCall(kAddrUnitEquipItemSlot, active.addr, Decision()->item_slot);
    *GbaPtr<uint8_t>(kAiActionDataAddr + 0x12u) = 0u;
  }

  GbaCall(kAddrDoAction, proc.addr);
}

// ---------------------------------------------------------------------------
// 0x0803530C: CpPerformEscapeMove
// ---------------------------------------------------------------------------
void CpPerformEscapeMove(GbaPtr<Proc> proc) {
  const uint8_t dir = Decision()->x_target;
  if (dir != 5u && *GbaPtr<const uint8_t>(proc.addr + 0x31u) != 0u) {
    GbaCall(kAddrSetMuMoveScript,
            kAiEscapeMoveScriptsAddr + static_cast<uint32_t>(dir) * 3u);
  }
}

// ---------------------------------------------------------------------------
// 0x0803534C: CpPerformSteal
// ---------------------------------------------------------------------------
void CpPerformSteal(GbaPtr<Proc> proc) {
  GbaPtr<Unit> target = GetUnit(Decision()->unit_target_id);
  const uint16_t item = target->items[Decision()->item_slot];
  GbaCall(kAddrUnitAddItem, ActiveUnit().addr, item);
  GbaCall(kAddrUnitRemoveItem, target.addr, Decision()->item_slot);
  GbaCall(kAddrStartStealPopup, item, proc.addr);
}

// ---------------------------------------------------------------------------
// 0x08035398: CpPerformPillageOrChest
// ---------------------------------------------------------------------------
bool CpPerformPillageOrChest(GbaPtr<Proc> proc) {
  const uint8_t mx = Decision()->x_move;
  const uint8_t my = Decision()->y_move;
  if (MapTerrain()[my][mx] == 0x21u) {
    GbaPtr<Unit> active = ActiveUnit();
    active->x = static_cast<int8_t>(mx);
    active->y = static_cast<int8_t>(my);
    *GbaPtr<uint8_t>(kAiActionDataAddr + 0x11u) = 0x17u;
    *GbaPtr<uint8_t>(kAiActionDataAddr + 0x12u) = Decision()->item_slot;
    GbaCall(kAddrDoItemAction, proc.addr);
  } else {
    GbaCall(kAddrPillageVillageAt,
            static_cast<uint32_t>(static_cast<int8_t>(mx)),
            static_cast<uint32_t>(static_cast<int8_t>(my - 1u)));
    const uint8_t opts = *GbaPtr<const uint8_t>(kAiPlayStatusAddr + 0x41u);
    if ((opts & 2u) == 0u) {
      GbaCall(kAddrM4aSongNumStart, 0xABu);
    }
    GbaCall(kAddrStartPopupText, kAiPillageTextAddr, 0x60u, 0u, proc.addr);
  }
  return true;
}

// ---------------------------------------------------------------------------
// 0x08035420: CpPerformStaff
// ---------------------------------------------------------------------------
bool CpPerformStaff(GbaPtr<Proc> proc) {
  GbaPtr<Unit> active = ActiveUnit();
  active->x = static_cast<int8_t>(Decision()->x_move);
  active->y = static_cast<int8_t>(Decision()->y_move);
  *GbaPtr<uint8_t>(kAiActionDataAddr + 0x11u) = 3u;
  *GbaPtr<uint8_t>(kAiActionDataAddr + 0x0Du) = Decision()->unit_target_id;
  *GbaPtr<uint8_t>(kAiActionDataAddr + 0x12u) = Decision()->item_slot;
  GbaCall(kAddrDoItemAction, proc.addr);
  return true;
}

// ---------------------------------------------------------------------------
// 0x08035458: CpPerformUseItem
// ---------------------------------------------------------------------------
bool CpPerformUseItem(GbaPtr<Proc> proc) {
  GbaPtr<Unit> active = ActiveUnit();
  active->x = static_cast<int8_t>(Decision()->x_move);
  active->y = static_cast<int8_t>(Decision()->y_move);
  *GbaPtr<uint8_t>(kAiActionDataAddr + 0x11u) = 0x17u;
  *GbaPtr<uint8_t>(kAiActionDataAddr + 0x12u) = Decision()->item_slot;
  GbaCall(kAddrDoItemAction, proc.addr);
  return true;
}

// ---------------------------------------------------------------------------
// 0x0803548C: CpPerformActionNop
// ---------------------------------------------------------------------------
bool CpPerformActionNop(GbaPtr<Proc> /*proc*/) { return true; }

// ---------------------------------------------------------------------------
// 0x08035490: CpPerformTalk
// ---------------------------------------------------------------------------
bool CpPerformTalk(GbaPtr<Proc> /*proc*/) {
  GbaPtr<Unit> active = ActiveUnit();
  active->x = static_cast<int8_t>(Decision()->x_move);
  active->y = static_cast<int8_t>(Decision()->y_move);
  if (Decision()->unit_target_id == 0u) {
    const uint8_t pid0 = GetUnit(Decision()->item_slot)->pinfo->id;
    const uint8_t pid1 = GetUnit(Decision()->x_target)->pinfo->id;
    GbaCall(kAddrStartTalkEvent, pid0, pid1);
  }
  return true;
}

// ---------------------------------------------------------------------------
// 0x080354D4: CpPerformRideBallista
// ---------------------------------------------------------------------------
bool CpPerformRideBallista(GbaPtr<Proc> /*proc*/) {
  GbaPtr<Unit> active = ActiveUnit();
  active->x = static_cast<int8_t>(Decision()->x_move);
  active->y = static_cast<int8_t>(Decision()->y_move);
  GbaCall(kAddrRideBallista, active.addr);
  return true;
}

// ---------------------------------------------------------------------------
// 0x080354FC: CpPerformExitBallista
// ---------------------------------------------------------------------------
bool CpPerformExitBallista(GbaPtr<Proc> /*proc*/) {
  GbaPtr<Unit> active = ActiveUnit();
  active->x = static_cast<int8_t>(Decision()->x_move);
  active->y = static_cast<int8_t>(Decision()->y_move);
  GbaCall(kAddrRemoveUnitFromBallista, active.addr);
  return true;
}

// ---------------------------------------------------------------------------
// 0x08035524: CpPerformTargetCursor
// ---------------------------------------------------------------------------
void CpPerformTargetCursor(GbaPtr<Proc> proc) {
  if (*GbaPtr<const uint8_t>(kAiActionDataAddr + 0x11u) == 0x1Bu) {
    return;
  }

  int tx = 0;
  int ty = 0;
  const uint8_t act = Decision()->action_id;
  switch (act) {
    case 0:
    case 2:
    case 4:
    case 6:
    case 9:
    case 10:
      return;
    case 1:
      if (Decision()->unit_target_id == 0u) {
        tx = Decision()->x_target;
        ty = Decision()->y_target;
      } else {
        GbaPtr<Unit> target = GetUnit(Decision()->unit_target_id);
        tx = target->x;
        ty = target->y;
      }
      if (static_cast<int8_t>(Decision()->item_slot) == -1) {
        GbaPtr<Unit> active = ActiveUnit();
        if ((active->flags & kUnitFlagInBallista) == 0u) {
          GbaCall(kAddrEndAllMus);
          active->x = static_cast<int8_t>(Decision()->x_move);
          active->y = static_cast<int8_t>(Decision()->y_move);
          GbaCall(kAddrRideBallista, active.addr);
          GbaCall(kAddrStartMu, active.addr);
          GbaCall(kAddrSetAutoMuDefaultFacing);
        }
      }
      break;
    case 3:
    case 7: {
      GbaPtr<Unit> target = GetUnit(Decision()->unit_target_id);
      tx = target->x;
      ty = target->y;
      break;
    }
    case 8: {
      GbaPtr<Unit> target = GetUnit(Decision()->y_target);
      tx = target->x;
      ty = target->y;
      break;
    }
    case 5: {
      if (Decision()->unit_target_id == 0u) {
        return;
      }
      GbaPtr<Unit> target = GetUnit(Decision()->unit_target_id);
      tx = target->x;
      ty = target->y;
      break;
    }
    default:
      break;
  }

  GbaCall(kAddrCameraMoveWatchPosition, proc.addr, static_cast<uint32_t>(tx),
          static_cast<uint32_t>(ty));
  StartAiCursorFlash(tx << 4, ty << 4, 2, proc);
}

// ---------------------------------------------------------------------------
// 0x08035634: CpPerformDispatchAction
// ---------------------------------------------------------------------------
void CpPerformDispatchAction(GbaPtr<Proc> proc) {
  *GbaPtr<uint8_t>(proc.addr + 0x30u) = 0u;
  if (*GbaPtr<const uint8_t>(kAiActionDataAddr + 0x11u) == 0x1Bu) {
    *GbaPtr<uint32_t>(proc.addr + 0x2Cu) = kAddrCpPerformWaitDone | 1u;
    return;
  }

  switch (Decision()->action_id) {
    case 0:
      *GbaPtr<uint32_t>(proc.addr + 0x2Cu) = kAddrCpPerformWaitDone | 1u;
      break;
    case 1:
      *GbaPtr<uint32_t>(proc.addr + 0x2Cu) = kAddrCpPerformWaitDone | 1u;
      CpPerformAttack(proc);
      break;
    case 2:
      CpPerformEscapeMove(proc);
      *GbaPtr<uint32_t>(proc.addr + 0x2Cu) = kAddrCpPerformWaitEscape | 1u;
      break;
    case 3:
      CpPerformSteal(proc);
      *GbaPtr<uint32_t>(proc.addr + 0x2Cu) = kAddrCpPerformWaitSteal | 1u;
      break;
    case 4:
      *GbaPtr<uint32_t>(proc.addr + 0x2Cu) = kAddrCpPerformPillageOrChest | 1u;
      break;
    case 5:
      *GbaPtr<uint32_t>(proc.addr + 0x2Cu) = kAddrCpPerformStaff | 1u;
      break;
    case 6:
      *GbaPtr<uint32_t>(proc.addr + 0x2Cu) = kAddrCpPerformUseItem | 1u;
      break;
    case 7:
      *GbaPtr<uint32_t>(proc.addr + 0x2Cu) = kAddrCpPerformActionNop | 1u;
      break;
    case 8:
      *GbaPtr<uint32_t>(proc.addr + 0x2Cu) = kAddrCpPerformTalk | 1u;
      break;
    case 9:
      *GbaPtr<uint32_t>(proc.addr + 0x2Cu) = kAddrCpPerformRideBallista | 1u;
      break;
    case 10:
      *GbaPtr<uint32_t>(proc.addr + 0x2Cu) = kAddrCpPerformExitBallista | 1u;
      break;
    default:
      break;
  }
}

// ---------------------------------------------------------------------------
// 0x08035714: CpPerformActionLoop
// ---------------------------------------------------------------------------
void CpPerformActionLoop(GbaPtr<Proc> proc) {
  GbaPtr<uint8_t> cnt(proc.addr + 0x30u);
  *cnt = static_cast<uint8_t>(*cnt + 1u);

  const uint32_t fn = *GbaPtr<const uint32_t>(proc.addr + 0x2Cu);
  if (static_cast<int8_t>(GbaCall(fn & ~1u, proc.addr)) == 1) {
    ProcBreak(proc);
  }

  GbaPtr<Unit> active = ActiveUnit();
  active->x = static_cast<int8_t>(Decision()->x_move);
  active->y = static_cast<int8_t>(Decision()->y_move);
}

// ---------------------------------------------------------------------------
// 0x08035758: CpPerformCleanup
// ---------------------------------------------------------------------------
void CpPerformCleanup(GbaPtr<Proc> proc) {
  AiUpdateFactionRecovery();
  AiEndMuAndRefreshUnits();
  GbaPtr<Unit> active = ActiveUnit();
  if (!active->pinfo || (active->flags & 0x00010005u) != 0u) {
    ProcGoto(proc, 1);
  }
}

// ---------------------------------------------------------------------------
// 0x08035790: CpPerformWaitDone
// ---------------------------------------------------------------------------
bool CpPerformWaitDone(GbaPtr<Proc> /*proc*/) { return true; }

// ---------------------------------------------------------------------------
// 0x08035794: CpPerformWaitEscape
// ---------------------------------------------------------------------------
bool CpPerformWaitEscape(GbaPtr<Proc> /*proc*/) {
  if (static_cast<int8_t>(GbaCall(kAddrMuExistsActive)) != 0) {
    return false;
  }
  ActiveUnit()->pinfo = GbaPtr<const PInfo>(0);
  return true;
}

// ---------------------------------------------------------------------------
// 0x080357B8: CpPerformWaitSteal
// ---------------------------------------------------------------------------
bool CpPerformWaitSteal(GbaPtr<Proc> proc) {
  if (*GbaPtr<const uint8_t>(proc.addr + 0x30u) <= 4u) {
    return false;
  }
  GbaCall(kAddrTmFill, kAiBg0TmAddr, 0u);
  GbaCall(kAddrTmFill, kAiBg1TmAddr, 0u);
  GbaCall(kAddrEnableBgSync, 3u);
  return true;
}

// ---------------------------------------------------------------------------
// Native dispatch thunks
// ---------------------------------------------------------------------------
namespace {

uint32_t ThunkCpDecideMain(uint32_t, uint32_t, uint32_t, uint32_t,
                           const uint32_t* absl_nullable) {
  CpDecideMain();
  return 0u;
}
uint32_t ThunkCpDecideSuspendOrRecover(uint32_t, uint32_t, uint32_t, uint32_t,
                                       const uint32_t* absl_nullable) {
  CpDecideSuspendOrRecover();
  return 0u;
}
uint32_t ThunkCpDecideDoorOrChest(uint32_t, uint32_t, uint32_t, uint32_t,
                                  const uint32_t* absl_nullable) {
  CpDecideDoorOrChest();
  return 0u;
}
uint32_t ThunkCpDecideScriptA(uint32_t, uint32_t, uint32_t, uint32_t,
                              const uint32_t* absl_nullable) {
  CpDecideScriptA();
  return 0u;
}
uint32_t ThunkCpDecideScriptB(uint32_t, uint32_t, uint32_t, uint32_t,
                              const uint32_t* absl_nullable) {
  CpDecideScriptB();
  return 0u;
}
uint32_t ThunkAiCursorFlashLoop(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                const uint32_t* absl_nullable) {
  AiCursorFlashLoop(GbaPtr<Proc>(r0));
  return 0u;
}
uint32_t ThunkStartAiCursorFlash(uint32_t r0, uint32_t r1, uint32_t r2,
                                 uint32_t r3, const uint32_t* absl_nullable) {
  StartAiCursorFlash(static_cast<int>(r0), static_cast<int>(r1),
                     static_cast<int>(r2), GbaPtr<Proc>(r3));
  return 0u;
}
uint32_t ThunkCpPerformEnsureMapBgm(uint32_t, uint32_t, uint32_t, uint32_t,
                                    const uint32_t* absl_nullable) {
  CpPerformEnsureMapBgm();
  return 0u;
}
uint32_t ThunkCpPerformCameraWatch(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                   const uint32_t* absl_nullable) {
  CpPerformCameraWatch(GbaPtr<Proc>(r0));
  return 0u;
}
uint32_t ThunkCpPerformBeginMove(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                 const uint32_t* absl_nullable) {
  CpPerformBeginMove(GbaPtr<Proc>(r0));
  return 0u;
}
uint32_t ThunkAiEndMuAndRefreshUnits(uint32_t, uint32_t, uint32_t, uint32_t,
                                     const uint32_t* absl_nullable) {
  AiEndMuAndRefreshUnits();
  return 0u;
}
uint32_t ThunkCpPerformAttack(uint32_t r0, uint32_t, uint32_t, uint32_t,
                              const uint32_t* absl_nullable) {
  CpPerformAttack(GbaPtr<Proc>(r0));
  return 0u;
}
uint32_t ThunkCpPerformEscapeMove(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                  const uint32_t* absl_nullable) {
  CpPerformEscapeMove(GbaPtr<Proc>(r0));
  return 0u;
}
uint32_t ThunkCpPerformSteal(uint32_t r0, uint32_t, uint32_t, uint32_t,
                             const uint32_t* absl_nullable) {
  CpPerformSteal(GbaPtr<Proc>(r0));
  return 0u;
}
uint32_t ThunkCpPerformPillageOrChest(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                      const uint32_t* absl_nullable) {
  return CpPerformPillageOrChest(GbaPtr<Proc>(r0)) ? 1u : 0u;
}
uint32_t ThunkCpPerformStaff(uint32_t r0, uint32_t, uint32_t, uint32_t,
                             const uint32_t* absl_nullable) {
  return CpPerformStaff(GbaPtr<Proc>(r0)) ? 1u : 0u;
}
uint32_t ThunkCpPerformUseItem(uint32_t r0, uint32_t, uint32_t, uint32_t,
                               const uint32_t* absl_nullable) {
  return CpPerformUseItem(GbaPtr<Proc>(r0)) ? 1u : 0u;
}
uint32_t ThunkCpPerformActionNop(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                 const uint32_t* absl_nullable) {
  return CpPerformActionNop(GbaPtr<Proc>(r0)) ? 1u : 0u;
}
uint32_t ThunkCpPerformTalk(uint32_t r0, uint32_t, uint32_t, uint32_t,
                            const uint32_t* absl_nullable) {
  return CpPerformTalk(GbaPtr<Proc>(r0)) ? 1u : 0u;
}
uint32_t ThunkCpPerformRideBallista(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                    const uint32_t* absl_nullable) {
  return CpPerformRideBallista(GbaPtr<Proc>(r0)) ? 1u : 0u;
}
uint32_t ThunkCpPerformExitBallista(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                    const uint32_t* absl_nullable) {
  return CpPerformExitBallista(GbaPtr<Proc>(r0)) ? 1u : 0u;
}
uint32_t ThunkCpPerformTargetCursor(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                    const uint32_t* absl_nullable) {
  CpPerformTargetCursor(GbaPtr<Proc>(r0));
  return 0u;
}
uint32_t ThunkCpPerformDispatchAction(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                      const uint32_t* absl_nullable) {
  CpPerformDispatchAction(GbaPtr<Proc>(r0));
  return 0u;
}
uint32_t ThunkCpPerformActionLoop(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                  const uint32_t* absl_nullable) {
  CpPerformActionLoop(GbaPtr<Proc>(r0));
  return 0u;
}
uint32_t ThunkCpPerformCleanup(uint32_t r0, uint32_t, uint32_t, uint32_t,
                               const uint32_t* absl_nullable) {
  CpPerformCleanup(GbaPtr<Proc>(r0));
  return 0u;
}
uint32_t ThunkCpPerformWaitDone(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                const uint32_t* absl_nullable) {
  return CpPerformWaitDone(GbaPtr<Proc>(r0)) ? 1u : 0u;
}
uint32_t ThunkCpPerformWaitEscape(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                  const uint32_t* absl_nullable) {
  return CpPerformWaitEscape(GbaPtr<Proc>(r0)) ? 1u : 0u;
}
uint32_t ThunkCpPerformWaitSteal(uint32_t r0, uint32_t, uint32_t, uint32_t,
                                 const uint32_t* absl_nullable) {
  return CpPerformWaitSteal(GbaPtr<Proc>(r0)) ? 1u : 0u;
}

}  // namespace

void RegisterAiCpFunctions() {
  RegisterNative(kAddrCpDecideMain, &ThunkCpDecideMain);
  RegisterNative(kAddrCpDecideSuspendOrRecover, &ThunkCpDecideSuspendOrRecover);
  RegisterNative(kAddrCpDecideDoorOrChest, &ThunkCpDecideDoorOrChest);
  RegisterNative(kAddrCpDecideScriptA, &ThunkCpDecideScriptA);
  RegisterNative(kAddrCpDecideScriptB, &ThunkCpDecideScriptB);
  RegisterNative(kAddrAiCursorFlashLoop, &ThunkAiCursorFlashLoop);
  RegisterNative(kAddrStartAiCursorFlash, &ThunkStartAiCursorFlash);
  RegisterNative(kAddrCpPerformEnsureMapBgm, &ThunkCpPerformEnsureMapBgm);
  RegisterNative(kAddrCpPerformCameraWatch, &ThunkCpPerformCameraWatch);
  RegisterNative(kAddrCpPerformBeginMove, &ThunkCpPerformBeginMove);
  RegisterNative(kAddrAiEndMuAndRefreshUnits, &ThunkAiEndMuAndRefreshUnits);
  RegisterNative(kAddrCpPerformAttack, &ThunkCpPerformAttack);
  RegisterNative(kAddrCpPerformEscapeMove, &ThunkCpPerformEscapeMove);
  RegisterNative(kAddrCpPerformSteal, &ThunkCpPerformSteal);
  RegisterNative(kAddrCpPerformPillageOrChest, &ThunkCpPerformPillageOrChest);
  RegisterNative(kAddrCpPerformStaff, &ThunkCpPerformStaff);
  RegisterNative(kAddrCpPerformUseItem, &ThunkCpPerformUseItem);
  RegisterNative(kAddrCpPerformActionNop, &ThunkCpPerformActionNop);
  RegisterNative(kAddrCpPerformTalk, &ThunkCpPerformTalk);
  RegisterNative(kAddrCpPerformRideBallista, &ThunkCpPerformRideBallista);
  RegisterNative(kAddrCpPerformExitBallista, &ThunkCpPerformExitBallista);
  RegisterNative(kAddrCpPerformTargetCursor, &ThunkCpPerformTargetCursor);
  RegisterNative(kAddrCpPerformDispatchAction, &ThunkCpPerformDispatchAction);
  RegisterNative(kAddrCpPerformActionLoop, &ThunkCpPerformActionLoop);
  RegisterNative(kAddrCpPerformCleanup, &ThunkCpPerformCleanup);
  RegisterNative(kAddrCpPerformWaitDone, &ThunkCpPerformWaitDone);
  RegisterNative(kAddrCpPerformWaitEscape, &ThunkCpPerformWaitEscape);
  RegisterNative(kAddrCpPerformWaitSteal, &ThunkCpPerformWaitSteal);
}

}  // namespace fe7
