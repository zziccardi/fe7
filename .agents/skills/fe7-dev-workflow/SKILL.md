---
name: fe7-dev-workflow
description: Build, test, and documentation conventions for the zziccardi/fe7 Bazel C++20 project on the Linux cloud VM (Bazelisk setup, JVM trustStore flags, libclang-rt-18-dev for ASan, and commenting standards). Use whenever building, testing, or modifying code in zziccardi/fe7.
---

# fe7 Development, Build & Documentation Workflow

## 1. Linux Cloud VM Environment Setup

Before running Bazel on a fresh VM for `zziccardi/fe7`:

1. **Install Bazelisk** (fetches Bazel `9.3.2` from `.bazelversion`):
   ```bash
   mkdir -p "$HOME/.local/bin"
   curl -fsSL -o "$HOME/.local/bin/bazel" \
     https://github.com/bazelbuild/bazelisk/releases/download/v1.25.0/bazelisk-linux-amd64
   chmod +x "$HOME/.local/bin/bazel"
   ```
2. **Install Clang 18 sanitizer runtime** (required for `--config=asan` linking `libclang_rt.asan_static-x86_64.a`):
   ```bash
   sudo apt-get update -qq && sudo apt-get install -y -qq libclang-rt-18-dev
   ```

## 2. Running Bazel Builds & Tests

Bazel's embedded JDK does not include the VM's system CA bundle (`PKIX path building failed` when fetching Bzlmod/http_archive deps). Always pass the system Java trustStore startup flags:

```bash
BAZEL="$HOME/.local/bin/bazel"
BAZEL_JVM_FLAGS=(
  --host_jvm_args=-Djavax.net.ssl.trustStore=/etc/ssl/certs/java/cacerts
  --host_jvm_args=-Djavax.net.ssl.trustStorePassword=changeit
)

# Default unit test run (ASan + UBSan + LSan + FE7_DEBUG_BOUNDS=1)
"$BAZEL" "${BAZEL_JVM_FLAGS[@]}" test --config=asan //...

# Also verify default and release (-O2 -DNDEBUG) builds when changing build files or asserts
"$BAZEL" "${BAZEL_JVM_FLAGS[@]}" test //...
"$BAZEL" "${BAZEL_JVM_FLAGS[@]}" test --config=release //...

# Enforce game/ C-with-types guardrails
python3 tools/check_agent_rules.py
```

## 3. Coding, Documentation & Commit Conventions

- **Package layout**: Headers, implementations, and unit tests are colocated in top-level Bazel packages (`runtime/` and `game/`). Include headers relative to workspace root (`#include "runtime/gba_ptr.h"`, `#include "game/..."`).
- **Naming**: Constants use `kCamelCase` (`kRomBaseAddr`); globals and file-scope variables use `gCamelCase` (`gMem`, `gNativeTable`, `gActiveUnit`).
- **Abseil**: `abseil-cpp` is available in `runtime/` (`absl::flat_hash_map`, `absl::Status`/`absl::StatusOr`, `absl::flags`, `absl_nonnull`/`absl_nullable`). In `game/`, only zero-cost header annotations (`absl_nonnull`/`absl_nullable`) are allowed—heap-allocating Abseil containers are forbidden during Phases 1–4.
- **Header files (`.h`)**: Always include a module-level comment block at the top explaining the module's purpose, invariants, and architectural role.
- **Public interfaces**: Document public constants, structs/classes, typedefs, and functions (parameters, return values, ownership/lifetime requirements).
- **Non-trivial implementations**: Include inline comments explaining *why* each non-trivial statement is performed (e.g., hardware bus rules, ARM7TDMI pipeline offsets, memory ownership order, ABI constraints), not merely restating what the code does.
- **C++20 style**: Google C++ Style Guide (`.clang-format`, 80-column limit), `-fno-exceptions`, `-fno-rtti`, `-Werror`. Follow `tools/agent_rules.md` for `game/`.
- **Commits**: Use Conventional Commits (`feat:`, `fix:`, `docs:`, `chore:`).

## 4. Runtime, Bazel & libmgba Gotchas

- `VFileFromConstMemory` in libmgba borrows the ROM byte pointer without copying; HAL (`runtime/mgba_hal.cc`) must keep an owned copy (`gRomBytes`) alive until after `fe7_mgba_core_destroy`.
- `mCoreInitConfig(core, NULL)` allocates config hash tables that `core->deinit(core)` does not free; always call `mCoreConfigDeinit(&core->config)` before `core->deinit(core)` (including error paths) to keep LSan clean.
- Because `fe7` targets compile with `-fno-rtti` while GoogleTest compiles with RTTI, `.bazelrc` keeps `--copt=-fno-sanitize=vptr` under `--config=asan` to prevent false-positive UBSan vptr errors.
- Under Bazel 9 + Clang + `--config=asan`, `absl/debugging:leak_check` includes `<sanitizer/lsan_interface.h>`, which fails `layering_check` unless `.bazelrc` sets `build:asan --features=-layering_check`.
- Keep `//runtime:gba_hal` as a header-only `cc_library` separate from `//runtime:mgba_hal`, and keep `runtime/BUILD.bazel` `default_visibility` restricted to `//runtime:__subpackages__` (exposing only `:gba_ptr`, `:gba_hal`, `:dispatch` to `//game:__subpackages__`) so `game/` targets never transitively depend on `@mgba`.
