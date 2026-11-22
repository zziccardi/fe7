#include "game/register_all.h"

#include "game/engine/rng.h"

namespace fe7 {

void RegisterAllNativeFunctions() { RegisterRngFunctions(); }

}  // namespace fe7
