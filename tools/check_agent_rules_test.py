#!/usr/bin/env python3
"""Unit tests for tools/check_agent_rules.py."""

import pathlib
import tempfile
import unittest

from tools import check_agent_rules


class CheckAgentRulesTest(unittest.TestCase):

  def setUp(self):
    self._tmpdir = tempfile.TemporaryDirectory()
    self.tmpdir = pathlib.Path(self._tmpdir.name)

  def tearDown(self):
    self._tmpdir.cleanup()

  def _write_game_file(self, name: str, content: str) -> pathlib.Path:
    game_dir = self.tmpdir / "game"
    game_dir.mkdir(exist_ok=True)
    path = game_dir / name
    path.write_text(content, encoding="utf-8")
    return path

  def test_strip_comments_removes_line_comments(self):
    self.assertEqual(
        check_agent_rules.strip_comments("int x = 1;  // throw std::vector"),
        "int x = 1;  ",
    )

  def test_is_game_path(self):
    self.assertTrue(check_agent_rules.is_game_path(pathlib.Path("game/unit.h")))
    self.assertTrue(
        check_agent_rules.is_game_path(pathlib.Path("game/core/bmunit.cc"))
    )
    self.assertFalse(
        check_agent_rules.is_game_path(pathlib.Path("runtime/dispatch.cc"))
    )

  def test_allows_clean_game_code_and_annotations(self):
    path = self._write_game_file(
        "unit.cc",
        "// Comments mentioning std::vector, 240, or throw are ignored.\n"
        "int ClampX(int x, const int* absl_nonnull out) {\n"
        "  return x < kViewportWidth ? x : kViewportWidth;\n"
        "}\n",
    )
    self.assertEqual(check_agent_rules.check_file(path), [])

  def test_ignores_non_game_paths(self):
    runtime_dir = self.tmpdir / "runtime"
    runtime_dir.mkdir(exist_ok=True)
    path = runtime_dir / "foo.cc"
    path.write_text("std::vector<int> v;\nabsl::flat_hash_map<int, int> m;\n",
                    encoding="utf-8")
    self.assertEqual(check_agent_rules.check_file(path), [])

  def test_rejects_forbidden_patterns_in_game_code(self):
    cases = [
        ("throw 1;\n", "Exceptions"),
        ("auto* p = dynamic_cast<int*>(x);\n", "RTTI"),
        ("virtual void Tick();\n", "virtual"),
        ("std::vector<int> xs;\n", "Heap-allocating STL"),
        ("absl::flat_hash_map<int, int> m;\n", "Heap-allocating Abseil"),
        ("__asm__(\"nop\");\n", "Inline assembly"),
        ("auto* p = reinterpret_cast<int*>(0x02000000);\n", "reinterpret_cast"),
        ("int w = 240;\n", "Hardcoded 240/160"),
        ("int h = 160;\n", "Hardcoded 240/160"),
    ]
    for idx, (snippet, expected_substr) in enumerate(cases):
      with self.subTest(snippet=snippet.strip()):
        path = self._write_game_file(f"case_{idx}.cc", snippet)
        errors = check_agent_rules.check_file(path)
        self.assertTrue(
            any(expected_substr in err for err in errors),
            f"Expected {expected_substr!r} in {errors}",
        )


  def _write_e2e_file(self, name: str, content: str) -> pathlib.Path:
    e2e_dir = self.tmpdir / "testing" / "e2e"
    e2e_dir.mkdir(parents=True, exist_ok=True)
    path = e2e_dir / name
    path.write_text(content, encoding="utf-8")
    return path

  def test_allows_hex_outside_e2e_test_bodies_and_in_fixtures(self):
    fixture = self._write_e2e_file(
        "skirmish_fixture.cc",
        "constexpr uint32_t kAddr = 0x02010000u;\n",
    )
    self.assertEqual(check_agent_rules.check_file(fixture), [])

    clean_test = self._write_e2e_file(
        "combat_test.cc",
        "constexpr int kSeed = 0x1234;\n"
        "TEST_F(SkirmishIntegrationTest, CleanScenario) {\n"
        "  // Comment mentioning 0xFF is fine.\n"
        "  RandInit(kSeed);\n"
        "}\n",
    )
    self.assertEqual(check_agent_rules.check_file(clean_test), [])

  def test_rejects_direct_hex_inside_e2e_test_body(self):
    bad_test = self._write_e2e_file(
        "ai_turn_test.cc",
        "TEST_F(SkirmishIntegrationTest,\n"
        "       MultilineHeaderScenario) {\n"
        "  auto u = GetUnit(0x81);\n"
        "}\n",
    )
    errors = check_agent_rules.check_file(bad_test)
    self.assertEqual(len(errors), 1)
    self.assertIn("Direct hexadecimal literal in E2E test body", errors[0])


if __name__ == "__main__":
  unittest.main()
