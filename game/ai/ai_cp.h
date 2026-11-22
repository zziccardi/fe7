// FE7 US (AE7E) Top-level Enemy AI decision/execution state-machine procs
// (`CpDecide*`, `CpPerform*`, `AiEndMuAndRefreshUnits`, cursor flash).
//
// Implements 28 Enemy AI functions (ROM 0x08034EDC..0x080357B8), completing
// the entire Core Enemy AI engine (`0x08034E00..0x0803B000`).

#ifndef FE7_GAME_AI_AI_CP_H_
#define FE7_GAME_AI_AI_CP_H_

#include <cstdint>

#include "game/engine/proc.h"
#include "game/map/map_range.h"
#include "runtime/gba_ptr.h"

namespace fe7 {

// Fixed FE7 US RAM & ROM table addresses used by CpDecide/CpPerform.
inline constexpr uint32_t kAiActionDataAddr = 0x0203A85Cu;
inline constexpr uint32_t kAiWorkingMoveScriptAddr = 0x02033E00u;
inline constexpr uint32_t kAiBg0TmAddr = 0x02022C60u;
inline constexpr uint32_t kAiBg1TmAddr = 0x02023460u;
inline constexpr uint32_t kAiKeysPtrAddr = 0x08B857F8u;
inline constexpr uint32_t kAiDecideFuncsAddr = 0x08B96F14u;
inline constexpr uint32_t kAiCursorFlashProcScrAddr = 0x08B96F7Cu;
inline constexpr uint32_t kAiMapBgmProcScrAddr = 0x08B85854u;
inline constexpr uint32_t kAiEscapeMoveScriptsAddr = 0x081D3664u;
inline constexpr uint32_t kAiPillageTextAddr = 0x08B9701Cu;

// External engine/UI helper ROM addresses called via GbaCall().
inline constexpr uint32_t kAddrEnableBgSync = 0x08000FFCu;
inline constexpr uint32_t kAddrTmFill = 0x08001810u;
inline constexpr uint32_t kAddrStartPopupText = 0x0800AD40u;
inline constexpr uint32_t kAddrStartStealPopup = 0x0800EEF4u;
inline constexpr uint32_t kAddrPutMapCursor = 0x0801596Cu;
inline constexpr uint32_t kAddrSetMapCursorPosition = 0x08015A90u;
inline constexpr uint32_t kAddrCameraMoveWatchPosition = 0x08015CE0u;
inline constexpr uint32_t kAddrStartMapSongBgm = 0x08015F84u;
inline constexpr uint32_t kAddrUnitAddItem = 0x08017654u;
inline constexpr uint32_t kAddrApplyUnitMovePos = 0x080181D0u;
inline constexpr uint32_t kAddrUnitRemoveItem = 0x08018D50u;
inline constexpr uint32_t kAddrRenderMap = 0x08019504u;
inline constexpr uint32_t kAddrRenderMapForFade = 0x08019584u;
inline constexpr uint32_t kAddrStartMapFade = 0x0801D608u;
inline constexpr uint32_t kAddrHideUnitSprite = 0x08026574u;
inline constexpr uint32_t kAddrShowUnitSprite = 0x08026598u;
inline constexpr uint32_t kAddrDoItemAction = 0x0802D00Cu;
inline constexpr uint32_t kAddrDoAction = 0x0802F218u;
inline constexpr uint32_t kAddrRideBallista = 0x08034770u;
inline constexpr uint32_t kAddrStartMu = 0x0806BAF0u;
inline constexpr uint32_t kAddrSetAutoMuDefaultFacing = 0x0806BFE0u;
inline constexpr uint32_t kAddrSetMuMoveScript = 0x0806C00Cu;
inline constexpr uint32_t kAddrMuExistsActive = 0x0806C064u;
inline constexpr uint32_t kAddrEndAllMus = 0x0806CCB8u;
inline constexpr uint32_t kAddrStartTalkEvent = 0x08078A40u;
inline constexpr uint32_t kAddrPillageVillageAt = 0x08078C14u;
inline constexpr uint32_t kAddrM4aSongNumStart = 0x080BE594u;

// FE7 US ROM addresses for the 28 decompiled CpDecide/CpPerform functions.
inline constexpr uint32_t kAddrCpDecideMain = 0x08034EDCu;
inline constexpr uint32_t kAddrCpDecideSuspendOrRecover = 0x08034F40u;
inline constexpr uint32_t kAddrCpDecideDoorOrChest = 0x08034FF0u;
inline constexpr uint32_t kAddrCpDecideScriptA = 0x0803500Cu;
inline constexpr uint32_t kAddrCpDecideScriptB = 0x08035044u;
inline constexpr uint32_t kAddrAiCursorFlashLoop = 0x080350A0u;
inline constexpr uint32_t kAddrStartAiCursorFlash = 0x080350E4u;
inline constexpr uint32_t kAddrCpPerformEnsureMapBgm = 0x0803510Cu;
inline constexpr uint32_t kAddrCpPerformCameraWatch = 0x08035124u;
inline constexpr uint32_t kAddrCpPerformBeginMove = 0x080351C4u;
inline constexpr uint32_t kAddrAiEndMuAndRefreshUnits = 0x0803523Cu;
inline constexpr uint32_t kAddrCpPerformAttack = 0x08035294u;
inline constexpr uint32_t kAddrCpPerformEscapeMove = 0x0803530Cu;
inline constexpr uint32_t kAddrCpPerformSteal = 0x0803534Cu;
inline constexpr uint32_t kAddrCpPerformPillageOrChest = 0x08035398u;
inline constexpr uint32_t kAddrCpPerformStaff = 0x08035420u;
inline constexpr uint32_t kAddrCpPerformUseItem = 0x08035458u;
inline constexpr uint32_t kAddrCpPerformActionNop = 0x0803548Cu;
inline constexpr uint32_t kAddrCpPerformTalk = 0x08035490u;
inline constexpr uint32_t kAddrCpPerformRideBallista = 0x080354D4u;
inline constexpr uint32_t kAddrCpPerformExitBallista = 0x080354FCu;
inline constexpr uint32_t kAddrCpPerformTargetCursor = 0x08035524u;
inline constexpr uint32_t kAddrCpPerformDispatchAction = 0x08035634u;
inline constexpr uint32_t kAddrCpPerformActionLoop = 0x08035714u;
inline constexpr uint32_t kAddrCpPerformCleanup = 0x08035758u;
inline constexpr uint32_t kAddrCpPerformWaitDone = 0x08035790u;
inline constexpr uint32_t kAddrCpPerformWaitEscape = 0x08035794u;
inline constexpr uint32_t kAddrCpPerformWaitSteal = 0x080357B8u;

// CpDecide decision-pipeline functions (`0x08034EDC..0x08035044`).
void CpDecideMain();
void CpDecideSuspendOrRecover();
void CpDecideDoorOrChest();
void CpDecideScriptA();
void CpDecideScriptB();

// Cursor flash proc (`0x080350A0..0x080350E4`).
void AiCursorFlashLoop(GbaPtr<Proc> proc);
void StartAiCursorFlash(int x, int y, int kind, GbaPtr<Proc> parent);

// CpPerform action-execution proc callbacks (`0x0803510C..0x080357B8`).
void CpPerformEnsureMapBgm();
void CpPerformCameraWatch(GbaPtr<Proc> proc);
void CpPerformBeginMove(GbaPtr<Proc> proc);
void AiEndMuAndRefreshUnits();
void CpPerformAttack(GbaPtr<Proc> proc);
void CpPerformEscapeMove(GbaPtr<Proc> proc);
void CpPerformSteal(GbaPtr<Proc> proc);
bool CpPerformPillageOrChest(GbaPtr<Proc> proc);
bool CpPerformStaff(GbaPtr<Proc> proc);
bool CpPerformUseItem(GbaPtr<Proc> proc);
bool CpPerformActionNop(GbaPtr<Proc> proc);
bool CpPerformTalk(GbaPtr<Proc> proc);
bool CpPerformRideBallista(GbaPtr<Proc> proc);
bool CpPerformExitBallista(GbaPtr<Proc> proc);
void CpPerformTargetCursor(GbaPtr<Proc> proc);
void CpPerformDispatchAction(GbaPtr<Proc> proc);
void CpPerformActionLoop(GbaPtr<Proc> proc);
void CpPerformCleanup(GbaPtr<Proc> proc);
bool CpPerformWaitDone(GbaPtr<Proc> proc);
bool CpPerformWaitEscape(GbaPtr<Proc> proc);
bool CpPerformWaitSteal(GbaPtr<Proc> proc);

// Registers all 28 CpDecide/CpPerform functions in the FE7 native dispatch
// table.
void RegisterAiCpFunctions();

}  // namespace fe7

#endif  // FE7_GAME_AI_AI_CP_H_
