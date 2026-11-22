#!/usr/bin/env python3
"""Unit tests for tools/match_fe8.py."""

import pathlib
import tempfile
import unittest

from tools import match_fe8


class MatchFe8Test(unittest.TestCase):

  def setUp(self):
    self._tmpdir = tempfile.TemporaryDirectory()
    self.corpus = pathlib.Path(self._tmpdir.name)

  def tearDown(self):
    self._tmpdir.cleanup()

  def test_build_work_queue_matches_and_orders_bottom_up(self):
    fe7_asm = self.corpus / "fe7_us" / "asm"
    fe7_asm.mkdir(parents=True)
    (fe7_asm / "code.s").write_text(
        "NextRN: @ 0x08000BE0\n"
        "    bx lr\n"
        "\n"
        "Roll1RN: @ 0x08000C10\n"
        "    bl NextRN\n"
        "    bx lr\n",
        encoding="utf-8",
    )

    fe8_src = self.corpus / "fireemblem8u" / "src"
    fe8_src.mkdir(parents=True)
    (fe8_src / "rng.c").write_text(
        "int NextRN(void) {\n"
        "    return 0;\n"
        "}\n"
        "bool Roll1RN(int threshold) {\n"
        "    return NextRN() < threshold;\n"
        "}\n",
        encoding="utf-8",
    )

    queue = match_fe8.build_work_queue(self.corpus)
    self.assertEqual(len(queue), 2)
    # Leaf function NextRN (depth 0) comes before caller Roll1RN (depth 1).
    self.assertEqual(queue[0]["name"], "NextRN")
    self.assertEqual(queue[0]["depth"], 0)
    self.assertEqual(queue[0]["wave"], 1)
    self.assertEqual(queue[0]["fe8u_match"], "rng.c")
    self.assertEqual(queue[1]["name"], "Roll1RN")
    self.assertEqual(queue[1]["depth"], 1)

  def test_compute_dependency_depths_handles_cycles(self):
    funcs = {"A": {}, "B": {}}
    callees = {"A": {"B"}, "B": {"A"}}
    depths = match_fe8.compute_dependency_depths(funcs, callees)
    self.assertIn("A", depths)
    self.assertIn("B", depths)


if __name__ == "__main__":
  unittest.main()
