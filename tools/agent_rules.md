# FE7 AI Agent Coding Rules (Google "C-with-types" C++20)

All code in `game/` (colocated `.h`, `.cc`, and `_test.cc` files) must follow the **Google C++ Style Guide** and the strict "C-with-types" subset enforced by `clang-tidy` and `tools/check_agent_rules.py`:

## Naming Conventions (Repo-Wide)
- Constants use `kCamelCase` (e.g., `kViewportWidth`, `kRomBaseAddr`).
- Globals and file-scope variables use `gCamelCase` (e.g., `gMem`, `gActiveUnit`, `gChapterData`) for consistency with `kBar` constants and FE7 decompilation symbols.

## Allowed in `game/`
- Free functions in `namespace fe7`
- POD structs with `static_assert(sizeof(...))` and `static_assert(offsetof(...))`
- `GbaPtr<T>`, `GbaSpan<T>`, `ProcAs<T>()`
- Scoped enums (`enum class`) and `constexpr` constants
- `kViewportWidth` / `kViewportHeight` for screen dimensions
- Explicit `hal::*` calls for MMIO, VRAM, Palette, OAM, DMA, and BIOS SWIs
- Zero-cost Abseil header annotations (`absl_nonnull`, `absl_nullable`)

## Forbidden in `game/`
- Exceptions (`try`, `catch`, `throw`) and RTTI (`dynamic_cast`, `typeid`)
- Class inheritance or `virtual` methods
- Heap-allocating STL or Abseil containers (`std::vector`, `std::string`, `std::map`, `std::unordered_map`, `std::set`, `std::unordered_set`, `std::list`, `std::deque`, `absl::flat_hash_map`, `absl::flat_hash_set`, `absl::node_hash_map`, `absl::node_hash_set`, `absl::btree_map`, `absl::btree_set`, `absl::InlinedVector`) during Phases 1–4
- Inline assembly (`asm`, `__asm__`)
- Raw `reinterpret_cast` from `uint32_t` or raw integer addresses (use `GbaPtr<T>` or `hal::*`)
- Hardcoded `240` or `160` screen dimensions in camera/UI layout logic (use `kViewportWidth` / `kViewportHeight`)
- Direct dependencies on `SDL3`, `ImGui`, `libmgba`, or host OS headers (enforced by Bazel `visibility`)
