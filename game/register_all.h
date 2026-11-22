// Central registration entry point for all decompiled native C++20 functions.
//
// Runtime frontends (fe7_native, fe7_headless, fe7_diff) call
// RegisterAllNativeFunctions() after hal::Init() to populate the ROM-address
// dispatch table with every decompiled subsystem.

#ifndef FE7_GAME_REGISTER_ALL_H_
#define FE7_GAME_REGISTER_ALL_H_

namespace fe7 {

// Registers every decompiled game subsystem in the global dispatch table.
void RegisterAllNativeFunctions();

}  // namespace fe7

#endif  // FE7_GAME_REGISTER_ALL_H_
