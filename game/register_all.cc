#include "game/register_all.h"

#include "game/action/action.h"
#include "game/ai/ai.h"
#include "game/ai/ai_actions.h"
#include "game/ai/ai_combat.h"
#include "game/ai/ai_cp.h"
#include "game/ai/ai_decide.h"
#include "game/ai/ai_opcodes.h"
#include "game/ai/ai_script.h"
#include "game/battle/battle.h"
#include "game/battle/battle_exp.h"
#include "game/battle/battle_sim.h"
#include "game/engine/clock.h"
#include "game/engine/proc.h"
#include "game/engine/rng.h"
#include "game/item/item.h"
#include "game/map/ballista.h"
#include "game/map/map.h"
#include "game/map/trap.h"
#include "game/phase/phase.h"
#include "game/support/support.h"
#include "game/unit/unit.h"

namespace fe7 {

void RegisterAllNativeFunctions() {
  RegisterRngFunctions();
  RegisterClockFunctions();
  RegisterProcFunctions();
  RegisterUnitFunctions();
  RegisterItemFunctions();
  RegisterBattleFunctions();
  RegisterBattleSimFunctions();
  RegisterBattleExpFunctions();
  RegisterMapFunctions();
  RegisterTrapFunctions();
  RegisterBallistaFunctions();
  RegisterPhaseFunctions();
  RegisterSupportFunctions();
  RegisterAiFunctions();
  RegisterAiDecideFunctions();
  RegisterAiScriptFunctions();
  RegisterAiOpcodeFunctions();
  RegisterAiActionFunctions();
  RegisterAiCombatFunctions();
  RegisterAiCpFunctions();
  RegisterActionFunctions();
}

}  // namespace fe7
