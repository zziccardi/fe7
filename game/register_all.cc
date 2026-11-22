#include "game/register_all.h"

#include "game/battle/battle.h"
#include "game/battle/battle_sim.h"
#include "game/engine/clock.h"
#include "game/engine/proc.h"
#include "game/engine/rng.h"
#include "game/item/item.h"
#include "game/map/map.h"
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
  RegisterMapFunctions();
}

}  // namespace fe7
