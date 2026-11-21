#!/usr/bin/env python3
"""Enforces Google 'C-with-types' C++20 guardrails on src/game/ and include/game/."""

import pathlib
import re
import sys

FORBIDDEN_GAME_PATTERNS = [
    (re.compile(r"\b(try|catch|throw)\b"), "Exceptions (try/catch/throw) are forbidden in game code"),
    (re.compile(r"\b(dynamic_cast|typeid)\b"), "RTTI (dynamic_cast/typeid) is forbidden in game code"),
    (re.compile(r"\bvirtual\b"), "virtual methods are forbidden in game code"),
    (re.compile(r"\bstd::(vector|string|map|unordered_map|set|unordered_set|list|deque)\b"),
     "Heap-allocating STL containers are forbidden in game code during Phases 1-4"),
    (re.compile(r"\b(__asm__|asm)\s*\("), "Inline assembly is forbidden in game code"),
    (re.compile(r"\breinterpret_cast\s*<"), "Raw reinterpret_cast is forbidden in game code; use GbaPtr<T> or ProcAs<T>()"),
]

HARDCODED_DIM_RE = re.compile(r"(?<![A-Za-z0-9_])(240|160)(?![A-Za-z0-9_])")


def strip_comments(line: str) -> str:
  idx = line.find("//")
  if idx != -1:
    return line[:idx]
  return line


def is_game_path(path: pathlib.Path) -> bool:
  parts = path.parts
  for i in range(len(parts) - 1):
    if parts[i] in ("src", "include") and parts[i + 1] == "game":
      return True
  return False


def check_file(path: pathlib.Path) -> list[str]:
  errors: list[str] = []
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
  roots = [pathlib.Path(a) for a in argv[1:]] or [pathlib.Path("src"), pathlib.Path("include")]
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
