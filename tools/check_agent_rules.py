#!/usr/bin/env python3
"""Enforces Google 'C-with-types' C++20 guardrails on game/."""

import pathlib
import re
import sys

FORBIDDEN_GAME_PATTERNS = [
    (re.compile(r"\b(try|catch|throw)\b"), "Exceptions (try/catch/throw) are forbidden in game code"),
    (re.compile(r"\b(dynamic_cast|typeid)\b"), "RTTI (dynamic_cast/typeid) is forbidden in game code"),
    (re.compile(r"\bvirtual\b"), "virtual methods are forbidden in game code"),
    (re.compile(r"\bstd::(vector|string|map|unordered_map|set|unordered_set|list|deque)\b"),
     "Heap-allocating STL containers are forbidden in game code during Phases 1-4"),
    (re.compile(r"\babsl::(flat_hash_map|flat_hash_set|node_hash_map|node_hash_set|btree_map|btree_set|InlinedVector)\b"),
     "Heap-allocating Abseil containers are forbidden in game code during Phases 1-4"),
    (re.compile(r"\b(__asm__|asm)\s*\("), "Inline assembly is forbidden in game code"),
    (re.compile(r"\breinterpret_cast\s*<"), "Raw reinterpret_cast is forbidden in game code; use GbaPtr<T> or ProcAs<T>()"),
]

HARDCODED_DIM_RE = re.compile(r"(?<![A-Za-z0-9_])(240|160)(?![A-Za-z0-9_])")
TEST_MACRO_RE = re.compile(r"^\s*TEST(?:_F|_P)?\s*\(")
HEX_LITERAL_RE = re.compile(r"\b0[xX][0-9a-fA-F]+\b")


def strip_comments(line: str) -> str:
  idx = line.find("//")
  if idx != -1:
    return line[:idx]
  return line


def is_game_path(path: pathlib.Path) -> bool:
  return "game" in path.parts


def is_e2e_test_path(path: pathlib.Path) -> bool:
  parts = path.parts
  for i in range(len(parts) - 1):
    if parts[i] == "testing" and parts[i + 1] == "e2e":
      return path.name.endswith("_test.cc")
  return False


def check_e2e_test_file(path: pathlib.Path) -> list[str]:
  errors: list[str] = []
  text = path.read_text(encoding="utf-8")
  pending_test = False
  in_test = False
  brace_depth = 0
  for lineno, raw_line in enumerate(text.splitlines(), start=1):
    line = strip_comments(raw_line)
    if not line.strip():
      continue
    if not in_test and TEST_MACRO_RE.search(line):
      pending_test = True
    if pending_test:
      if "{" in line:
        in_test = True
        pending_test = False
        brace_depth = line.count("{") - line.count("}")
        if HEX_LITERAL_RE.search(line):
          errors.append(
              f"{path}:{lineno}: Direct hexadecimal literal in E2E test body; "
              "introduce a named constant or test-only helper"
          )
        if brace_depth <= 0:
          in_test = False
      continue
    if in_test:
      if HEX_LITERAL_RE.search(line):
        errors.append(
            f"{path}:{lineno}: Direct hexadecimal literal in E2E test body; "
            "introduce a named constant or test-only helper"
        )
      brace_depth += line.count("{") - line.count("}")
      if brace_depth <= 0:
        in_test = False
  return errors


def check_file(path: pathlib.Path) -> list[str]:
  errors: list[str] = []
  if is_e2e_test_path(path):
    return check_e2e_test_file(path)
  if not is_game_path(path):
    return errors
  text = path.read_text(encoding="utf-8")
  for lineno, raw_line in enumerate(text.splitlines(), start=1):
    line = strip_comments(raw_line)
    if not line.strip():
      continue
    for pattern, msg in FORBIDDEN_GAME_PATTERNS:
      if pattern.search(line):
        errors.append(f"{path}:{lineno}: {msg}")
    if HARDCODED_DIM_RE.search(line) and "kViewport" not in line:
      errors.append(
          f"{path}:{lineno}: Hardcoded 240/160 screen dimension; use kViewportWidth/kViewportHeight"
      )
  return errors


def main(argv: list[str]) -> int:
  roots = [pathlib.Path(a) for a in argv[1:]] or [
      pathlib.Path("game"),
      pathlib.Path("testing/e2e"),
  ]
  all_errors: list[str] = []
  for root in roots:
    if not root.exists():
      continue
    if root.is_file():
      all_errors.extend(check_file(root))
      continue
    for ext in ("*.h", "*.cc"):
      for path in sorted(root.rglob(ext)):
        all_errors.extend(check_file(path))
  for err in all_errors:
    print(err, file=sys.stderr)
  if all_errors:
    print(f"Found {len(all_errors)} agent rule violation(s).", file=sys.stderr)
    return 1
  print("check_agent_rules: OK")
  return 0


if __name__ == "__main__":
  sys.exit(main(sys.argv))
