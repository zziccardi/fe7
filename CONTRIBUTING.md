# Contributing to `fe7`

Thank you for your interest in contributing to `fe7`!

## Legal & Licensing Rules

Please read [`LICENSE`](LICENSE) before contributing:

- **Never commit copyrighted assets** — no `.gba` ROMs, extracted sprites/tiles/audio, dialogue scripts, `.sav`/`.srm` saves, `.fe7t` trace dumps, `corpus/`, or `.ghidra/` artifacts.
- **Split license:**
  - Contributions under `runtime/`, `tools/`, and `testing/` are licensed under **GPL-3.0** (`LICENSE-GPLv3`).
  - Code under `game/` is reverse-engineered behavioral reimplementation excluded from GPL-3.0 (provided strictly for non-commercial educational, research, and preservation purposes).

## Prerequisites

- **Bazelisk** (fetches Bazel `9.3.2` from `.bazelversion`)
- **Clang 18+** (`libclang-rt-18-dev` on Ubuntu/Debian for `--config=asan`) and `clang-format`
- **Python 3.10+**

## Local Verification Checklist

Before opening a pull request, run the full ROM-free verification suite:

```bash
# Build & run all ASan/UBSan unit tests and pure-native E2E tests
bazel test --config=asan --config=headless //...

# Verify 80-column Google C++ formatting, game/ guardrails, and Python tool tests
python3 tools/check_format.py
python3 tools/check_agent_rules.py
python3 -m unittest discover -s tools -p '*_test.py' -v
```

If you are decompiling or modifying functions in `game/` and have a legally obtained FE7 US (`AE7E`) ROM staged locally, also run differential verification:

```bash
bazel run --config=headless //runtime:fe7_diff -- --rom=/path/to/fe7_us.gba --fuzz_iters=256
```

## Coding & Commit Conventions

- **C++20 style:** Google C++ Style Guide (`.clang-format`, 80-column limit), `-fno-exceptions`, `-fno-rtti`, `-Werror`, Include-What-You-Use (IWYU).
- **`game/` & `testing/e2e/` guardrails:** Follow [`tools/agent_rules.md`](tools/agent_rules.md) (no heap-allocating STL/Abseil containers in `game/`, no hardcoded `240`/`160` screen dimensions, no direct `0x...` hex literals inside `testing/e2e/*_test.cc` `TEST_F` bodies).
- **Decompilation workflow:** See [`docs/decompilation_workflow.md`](docs/decompilation_workflow.md) for the step-by-step Thumb-1 → C++20 translation and verification guide.
- **Commits:** Use [Conventional Commits](https://www.conventionalcommits.org/) (`feat:`, `fix:`, `test:`, `docs:`, `refactor:`, `chore:`).
