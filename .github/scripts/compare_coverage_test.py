#!/usr/bin/env python3
# Copyright 2026 Comcast Cable Communications Management, LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# SPDX-License-Identifier: Apache-2.0
"""
Tests for compare_coverage.py

Covers:
  - Unit tests for parse_lcov_coverage(), load_baseline(), _suite_analysis(),
    and _fmt_timestamp()
  - Integration tests (subprocess) simulating all gate scenarios.

This repository has a single test suite (unit tests).  There are no component
or L1 tests, so --unit is the only coverage input.

Workflow-level scenarios (unit_tests job fails) are handled by GitHub Actions'
implicit success() dependency check on the coverage-gate job and cannot be
tested at the Python script level; they are documented inline.
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

# ---------------------------------------------------------------------------
# Import the module under test
# ---------------------------------------------------------------------------
SCRIPTS_DIR = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, SCRIPTS_DIR)

import compare_coverage  # noqa: E402  (after sys.path manipulation)

THRESHOLD = compare_coverage.THRESHOLD  # 75.0


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _make_lcov(lines_found: int, lines_hit: int) -> str:
    """Minimal valid lcov .info content with the given LF/LH counts."""
    return (
        "SF:src/fake.cpp\n"
        f"LF:{lines_found}\n"
        f"LH:{lines_hit}\n"
        "end_of_record\n"
    )


def _write_lcov(tmp_dir: str, name: str, lines_found: int, lines_hit: int) -> str:
    """Write an lcov file and return its absolute path."""
    path = os.path.join(tmp_dir, name)
    with open(path, "w") as fh:
        fh.write(_make_lcov(lines_found, lines_hit))
    return path


def _write_baseline(tmp_dir: str, data: dict, name: str = "baseline.json") -> str:
    """Serialise *data* to JSON and return the path."""
    path = os.path.join(tmp_dir, name)
    with open(path, "w") as fh:
        json.dump(data, fh)
    return path


def _run_script(*args: str) -> subprocess.CompletedProcess:
    """Invoke compare_coverage.py as a subprocess and return the result."""
    cmd = [sys.executable, os.path.join(SCRIPTS_DIR, "compare_coverage.py"), *args]
    return subprocess.run(cmd, capture_output=True, text=True)


# ===========================================================================
# Unit tests — parse_lcov_coverage()
# ===========================================================================

class TestParseLcovCoverage(unittest.TestCase):
    """Tests for the lcov .info parser."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmp, ignore_errors=True)

    def _write(self, name: str, content: str) -> str:
        path = os.path.join(self.tmp, name)
        with open(path, "w") as fh:
            fh.write(content)
        return path

    # --- Missing / empty inputs -------------------------------------------------

    def test_none_path_returns_none(self):
        self.assertIsNone(compare_coverage.parse_lcov_coverage(None))

    def test_empty_path_returns_none(self):
        self.assertIsNone(compare_coverage.parse_lcov_coverage(""))

    def test_nonexistent_file_returns_none(self):
        self.assertIsNone(compare_coverage.parse_lcov_coverage("/no/such/file.info"))

    def test_empty_file_returns_none(self):
        p = self._write("empty.info", "")
        self.assertIsNone(compare_coverage.parse_lcov_coverage(p))

    def test_no_lf_data_returns_none(self):
        p = self._write("no_lf.info", "SF:foo.cpp\nend_of_record\n")
        self.assertIsNone(compare_coverage.parse_lcov_coverage(p))

    def test_lf_zero_returns_none(self):
        p = self._write("zero_lf.info", "SF:foo.cpp\nLF:0\nLH:0\nend_of_record\n")
        self.assertIsNone(compare_coverage.parse_lcov_coverage(p))

    # --- Basic coverage values --------------------------------------------------

    def test_100_percent(self):
        p = _write_lcov(self.tmp, "full.info", 100, 100)
        self.assertEqual(compare_coverage.parse_lcov_coverage(p), 100.0)

    def test_75_percent_exact(self):
        p = _write_lcov(self.tmp, "seventy_five.info", 100, 75)
        self.assertEqual(compare_coverage.parse_lcov_coverage(p), 75.0)

    def test_zero_percent(self):
        p = _write_lcov(self.tmp, "zero_pct.info", 100, 0)
        self.assertEqual(compare_coverage.parse_lcov_coverage(p), 0.0)

    def test_partial_coverage(self):
        # 150 / 200 = 75.0 %
        p = _write_lcov(self.tmp, "partial.info", 200, 150)
        self.assertEqual(compare_coverage.parse_lcov_coverage(p), 75.0)

    # --- Multi-record aggregation -----------------------------------------------

    def test_aggregates_multiple_records(self):
        # 80 + 60 = 140 hit out of 200 → 70.0 %
        content = (
            "SF:a.cpp\nLF:100\nLH:80\nend_of_record\n"
            "SF:b.cpp\nLF:100\nLH:60\nend_of_record\n"
        )
        p = self._write("multi.info", content)
        self.assertEqual(compare_coverage.parse_lcov_coverage(p), 70.0)

    # --- Malformed data ---------------------------------------------------------

    def test_malformed_lf_ignored_gracefully(self):
        # LF with a non-numeric value; total_found stays 0 → None
        p = self._write("bad_lf.info", "SF:a.cpp\nLF:abc\nLH:50\nend_of_record\n")
        self.assertIsNone(compare_coverage.parse_lcov_coverage(p))

    def test_malformed_lh_ignored_gracefully(self):
        # LH with garbage value; LF is valid, so LF=100, LH=0 → 0.0 %
        p = self._write("bad_lh.info", "SF:a.cpp\nLF:100\nLH:xyz\nend_of_record\n")
        self.assertEqual(compare_coverage.parse_lcov_coverage(p), 0.0)

    def test_entirely_non_lcov_content(self):
        p = self._write("corrupt.info", "THIS IS NOT A VALID LCOV FILE\n")
        self.assertIsNone(compare_coverage.parse_lcov_coverage(p))

    # --- Rounding ---------------------------------------------------------------

    def test_rounds_to_two_decimal_places(self):
        # 1/3 ≈ 33.33 %
        p = _write_lcov(self.tmp, "third.info", 3, 1)
        self.assertEqual(compare_coverage.parse_lcov_coverage(p), 33.33)


# ===========================================================================
# Unit tests — load_baseline()
# ===========================================================================

class TestLoadBaseline(unittest.TestCase):
    """Tests for the JSON baseline loader."""

    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmp, ignore_errors=True)

    def _write_json(self, name: str, content: str) -> str:
        path = os.path.join(self.tmp, name)
        with open(path, "w") as fh:
            fh.write(content)
        return path

    # --- Missing / empty inputs -------------------------------------------------

    def test_none_returns_empty_dict(self):
        self.assertEqual(compare_coverage.load_baseline(None), {})

    def test_empty_path_returns_empty_dict(self):
        self.assertEqual(compare_coverage.load_baseline(""), {})

    def test_nonexistent_file_returns_empty_dict(self):
        self.assertEqual(compare_coverage.load_baseline("/no/such/file.json"), {})

    # --- Valid JSON -------------------------------------------------------------

    def test_valid_baseline_returns_dict(self):
        data = {"coverage": 80.0, "commit": "abc123", "timestamp": "2026-01-01T00:00:00Z"}
        p = _write_baseline(self.tmp, data)
        self.assertEqual(compare_coverage.load_baseline(p), data)

    def test_valid_empty_json_object(self):
        p = _write_baseline(self.tmp, {})
        self.assertEqual(compare_coverage.load_baseline(p), {})

    def test_valid_baseline_extra_keys_preserved(self):
        # load_baseline returns the dict as-is — keys not read by main() must survive
        data = {"coverage": 80.0, "commit": "abc123", "timestamp": "2026-01-01T00:00:00Z",
                "extra_field": "ignored_by_gate", "another": 42}
        p = _write_baseline(self.tmp, data)
        self.assertEqual(compare_coverage.load_baseline(p), data)

    # --- Invalid JSON -----------------------------------------------------------

    def test_malformed_json_returns_empty_dict(self):
        p = self._write_json("bad.json", "{not valid json}")
        result = compare_coverage.load_baseline(p)
        self.assertEqual(result, {})

    def test_truncated_json_returns_empty_dict(self):
        p = self._write_json("truncated.json", '{"coverage": 80')
        self.assertEqual(compare_coverage.load_baseline(p), {})

    def test_empty_file_returns_empty_dict(self):
        p = self._write_json("empty.json", "")
        self.assertEqual(compare_coverage.load_baseline(p), {})

    # --- Non-dict JSON ----------------------------------------------------------

    def test_json_array_returns_empty_dict(self):
        import io, contextlib
        p = self._write_json("list.json", "[1, 2, 3]")
        buf = io.StringIO()
        with contextlib.redirect_stderr(buf):
            result = compare_coverage.load_baseline(p)
        self.assertEqual(result, {})
        self.assertIn("WARNING", buf.getvalue())

    def test_json_string_returns_empty_dict(self):
        import io, contextlib
        p = self._write_json("str.json", '"just a string"')
        buf = io.StringIO()
        with contextlib.redirect_stderr(buf):
            result = compare_coverage.load_baseline(p)
        self.assertEqual(result, {})
        self.assertIn("WARNING", buf.getvalue())

    def test_json_number_returns_empty_dict(self):
        import io, contextlib
        p = self._write_json("num.json", "42")
        buf = io.StringIO()
        with contextlib.redirect_stderr(buf):
            result = compare_coverage.load_baseline(p)
        self.assertEqual(result, {})
        self.assertIn("WARNING", buf.getvalue())

    def test_json_null_returns_empty_dict(self):
        p = self._write_json("null.json", "null")
        # null is parsed as None, which is not a dict — Warning emitted, empty dict returned
        self.assertEqual(compare_coverage.load_baseline(p), {})


# ===========================================================================
# Unit tests — _suite_analysis()
# ===========================================================================

class TestSuiteAnalysis(unittest.TestCase):
    """
    Tests for the core gate analysis function.

    Gate passes (ok=True) when BOTH:
      1. current >= THRESHOLD (75.0)
      2. current >= baseline (regression check)

    SKIP when current is None.
    Regression check disabled when baseline is None or 0.0.
    """

    # --- Scenario 1: exceeds both threshold AND baseline → PASS ----------------

    def test_s1_exceeds_threshold_and_baseline(self):
        ok, _, _, reason = compare_coverage._suite_analysis(80.0, 77.0)
        self.assertTrue(ok)
        self.assertIsNone(reason)

    # --- Scenario 2: meets threshold exactly (75%) AND beats baseline → PASS ---

    def test_s2_meets_threshold_exactly_beats_baseline(self):
        ok, _, _, reason = compare_coverage._suite_analysis(75.0, 70.0)
        self.assertTrue(ok)
        self.assertIsNone(reason)

    # --- Scenario 3: meets baseline exactly, exceeds threshold → PASS ----------

    def test_s3_meets_baseline_exactly_exceeds_threshold(self):
        ok, _, _, reason = compare_coverage._suite_analysis(80.0, 80.0)
        self.assertTrue(ok)
        self.assertIsNone(reason)

    # --- Scenario 4: meets BOTH exactly (75.0 == threshold == baseline) → PASS -

    def test_s4_meets_both_exactly_at_threshold(self):
        ok, _, _, reason = compare_coverage._suite_analysis(75.0, 75.0)
        self.assertTrue(ok)
        self.assertIsNone(reason)

    # --- Scenario 5: exceeds threshold but BELOW baseline → FAIL (regression) --

    def test_s5_above_threshold_below_baseline(self):
        ok, result, _, reason = compare_coverage._suite_analysis(76.0, 80.0)
        self.assertFalse(ok)
        self.assertIsNotNone(reason)
        self.assertIn("dropped from baseline", reason)

    # --- Scenario 6: below threshold but meets/exceeds baseline → FAIL ----------

    def test_s6_below_threshold_meets_baseline(self):
        ok, _, _, reason = compare_coverage._suite_analysis(74.0, 70.0)
        self.assertFalse(ok)
        self.assertIsNotNone(reason)
        self.assertIn("below threshold", reason)
        self.assertNotIn("dropped from baseline", reason)  # regression check passed

    def test_s6b_below_threshold_equals_baseline(self):
        ok, _, _, reason = compare_coverage._suite_analysis(74.0, 74.0)
        self.assertFalse(ok)
        self.assertIsNotNone(reason)
        self.assertIn("below threshold", reason)
        self.assertNotIn("dropped from baseline", reason)  # regression check passed

    # --- Scenario 7: fails BOTH conditions → FAIL --------------------------------

    def test_s7_fails_both_conditions(self):
        ok, _, _, reason = compare_coverage._suite_analysis(70.0, 80.0)
        self.assertFalse(ok)
        self.assertIsNotNone(reason)
        self.assertIn("below threshold", reason)
        self.assertIn("dropped from baseline", reason)

    # --- Scenario 8: no baseline → threshold-only check -------------------------

    def test_no_baseline_above_threshold_passes(self):
        ok, _, delta, reason = compare_coverage._suite_analysis(80.0, None)
        self.assertTrue(ok)
        self.assertIsNone(reason)
        self.assertEqual(delta, "N/A")

    def test_no_baseline_below_threshold_fails(self):
        ok, _, _, reason = compare_coverage._suite_analysis(70.0, None)
        self.assertFalse(ok)
        self.assertIn("below threshold", reason)

    def test_no_baseline_at_threshold_exactly_passes(self):
        ok, _, _, reason = compare_coverage._suite_analysis(75.0, None)
        self.assertTrue(ok)
        self.assertIsNone(reason)

    # --- SKIP case: no current coverage -----------------------------------------

    def test_skip_when_current_is_none(self):
        ok, result, delta, reason = compare_coverage._suite_analysis(None, 80.0)
        self.assertFalse(ok, "Missing coverage data should be treated as WARN")
        self.assertIn("coverage data missing", result)
        self.assertEqual(delta, "N/A")
        self.assertIsNotNone(reason)
        self.assertIn("coverage data missing", reason)

    def test_skip_when_both_none(self):
        ok, result, delta, reason = compare_coverage._suite_analysis(None, None)
        self.assertFalse(ok)
        self.assertIn("coverage data missing", result)

    # --- Zero baseline: regression check disabled --------------------------------

    def test_zero_baseline_above_threshold_passes(self):
        ok, _, delta, reason = compare_coverage._suite_analysis(80.0, 0.0)
        self.assertTrue(ok)
        self.assertIsNone(reason)
        self.assertEqual(delta, "N/A", "Delta must be N/A for zero baseline")

    def test_zero_baseline_below_threshold_fails(self):
        ok, _, _, reason = compare_coverage._suite_analysis(70.0, 0.0)
        self.assertFalse(ok)
        self.assertIsNotNone(reason)
        self.assertIn("below threshold", reason)

    # --- Delta string correctness ------------------------------------------------

    def test_delta_positive(self):
        _, _, delta, _ = compare_coverage._suite_analysis(80.0, 77.0)
        self.assertEqual(delta, "+3.00%")

    def test_delta_negative(self):
        _, _, delta, _ = compare_coverage._suite_analysis(76.0, 80.0)
        self.assertEqual(delta, "-4.00%")

    def test_delta_zero(self):
        _, _, delta, _ = compare_coverage._suite_analysis(80.0, 80.0)
        self.assertEqual(delta, "+0.00%")

    def test_delta_na_when_no_baseline(self):
        _, _, delta, _ = compare_coverage._suite_analysis(80.0, None)
        self.assertEqual(delta, "N/A")

    # --- Boundary: one tick below threshold (74.99 is impossible from lcov,
    #     but 74.0 covers the just-below case) ----------------------------------

    def test_just_below_threshold_fails(self):
        # 74 / 100 = 74.0 %
        ok, _, _, reason = compare_coverage._suite_analysis(74.0, 70.0)
        self.assertFalse(ok)

    def test_just_at_threshold_passes(self):
        ok, _, _, reason = compare_coverage._suite_analysis(75.0, 70.0)
        self.assertTrue(ok)


# ===========================================================================
# Unit tests — _fmt_timestamp()
# ===========================================================================

class TestFmtTimestamp(unittest.TestCase):
    """Tests for the timestamp formatting helper."""

    def test_valid_iso8601_formats_correctly(self):
        result = compare_coverage._fmt_timestamp("2026-01-15T10:30:00Z")
        self.assertEqual(result, "2026-01-15 10:30 UTC")

    def test_invalid_string_returned_as_is(self):
        result = compare_coverage._fmt_timestamp("not-a-timestamp")
        self.assertEqual(result, "not-a-timestamp")

    def test_empty_string_returns_unknown(self):
        result = compare_coverage._fmt_timestamp("")
        self.assertEqual(result, "unknown")

    def test_none_returns_unknown(self):
        result = compare_coverage._fmt_timestamp(None)
        self.assertEqual(result, "unknown")

    def test_integer_returns_unknown(self):
        result = compare_coverage._fmt_timestamp(12345)
        self.assertEqual(result, "unknown")


# ===========================================================================
# Integration tests — main() via subprocess
# ===========================================================================

class TestMainIntegration(unittest.TestCase):
    """
    End-to-end simulation of every gate scenario.

    Each test invokes the script as a subprocess (exactly as GitHub Actions
    would) and asserts on exit code and stdout/stderr content.

    This repository has unit tests only.  --unit is the single coverage input.
    The baseline JSON uses the key "coverage" for the single coverage value.
    """

    def setUp(self):
        self.tmp = tempfile.mkdtemp()

    def tearDown(self):
        import shutil
        shutil.rmtree(self.tmp, ignore_errors=True)

    # --- Helpers ----------------------------------------------------------------

    def _lcov(self, name: str, lf: int, lh: int) -> str:
        return _write_lcov(self.tmp, name, lf, lh)

    def _baseline(self, data: dict, name: str = "baseline.json") -> str:
        return _write_baseline(self.tmp, data, name)

    def _run(self, *args: str) -> subprocess.CompletedProcess:
        return _run_script(*args)

    # ===========================================================================
    # SCENARIO 1 — Coverage exceeds both threshold AND baseline
    # Expected: Gate PASSES (exit 0)
    # ===========================================================================

    def test_s1_exceeds_threshold_and_baseline(self):
        bl = self._baseline({"coverage": 77.0})
        unit_cov = self._lcov("unit.info", 100, 80)  # 80 %
        r = self._run("--baseline", bl, "--unit", unit_cov)
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        self.assertIn("[PASS]", r.stdout)

    # ===========================================================================
    # SCENARIO 2 — Coverage meets threshold exactly (75%) and meets baseline
    # Expected: Gate PASSES (exit 0)
    # ===========================================================================

    def test_s2_meets_threshold_exactly_meets_baseline(self):
        bl = self._baseline({"coverage": 70.0})
        unit_cov = self._lcov("unit.info", 100, 75)  # 75.0 %
        r = self._run("--baseline", bl, "--unit", unit_cov)
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        self.assertIn("[PASS]", r.stdout)

    # ===========================================================================
    # SCENARIO 3 — Meets baseline exactly but exceeds threshold
    # Expected: Gate PASSES (exit 0)
    # ===========================================================================

    def test_s3_meets_baseline_exactly_exceeds_threshold(self):
        bl = self._baseline({"coverage": 80.0})
        unit_cov = self._lcov("unit.info", 100, 80)  # 80 % == baseline
        r = self._run("--baseline", bl, "--unit", unit_cov)
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        self.assertIn("[PASS]", r.stdout)

    # ===========================================================================
    # SCENARIO 4 — Meets BOTH exactly (current == threshold == baseline == 75 %)
    # Expected: Gate PASSES (exit 0)
    # ===========================================================================

    def test_s4_meets_both_exactly(self):
        bl = self._baseline({"coverage": 75.0})
        unit_cov = self._lcov("unit.info", 100, 75)
        r = self._run("--baseline", bl, "--unit", unit_cov)
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        self.assertIn("[PASS]", r.stdout)

    # ===========================================================================
    # SCENARIO 5 — Exceeds threshold but falls BELOW baseline (regression)
    # Expected: Gate WARNS (exit 0 — informational only), [WARN] shown
    # ===========================================================================

    def test_s5_above_threshold_below_baseline(self):
        bl = self._baseline({"coverage": 85.0})
        unit_cov = self._lcov("unit.info", 100, 80)  # 80 % < 85 % baseline
        r = self._run("--baseline", bl, "--unit", unit_cov)
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        self.assertIn("[WARN]", r.stdout)
        self.assertIn("dropped from baseline", r.stdout)

    # ===========================================================================
    # SCENARIO 6 — Falls BELOW threshold but meets/exceeds baseline
    # Expected: Gate WARNS (exit 0 — informational only), [WARN] shown
    # ===========================================================================

    def test_s6_below_threshold_meets_baseline(self):
        bl = self._baseline({"coverage": 70.0})
        unit_cov = self._lcov("unit.info", 100, 74)  # 74 % < 75 % threshold
        r = self._run("--baseline", bl, "--unit", unit_cov)
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        self.assertIn("[WARN]", r.stdout)
        self.assertIn("below threshold", r.stdout)

    # ===========================================================================
    # SCENARIO 7 — Fails BOTH conditions (below threshold AND below baseline)
    # Expected: Gate WARNS (exit 0 — informational only), [WARN] shown
    # ===========================================================================

    def test_s7_fails_both_threshold_and_baseline(self):
        bl = self._baseline({"coverage": 85.0})
        unit_cov = self._lcov("unit.info", 100, 70)  # 70 % < threshold AND < baseline
        r = self._run("--baseline", bl, "--unit", unit_cov)
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        self.assertIn("below threshold", r.stdout)
        self.assertIn("dropped from baseline", r.stdout)

    # ===========================================================================
    # SCENARIO 8a — Baseline file is MISSING
    # Expected: Graceful fallback; threshold-only check; no crash
    # ===========================================================================

    def test_s8_baseline_missing_coverage_above_threshold(self):
        unit_cov = self._lcov("unit.info", 100, 80)
        r = self._run("--baseline", "/nonexistent/coverage-baseline.json", "--unit", unit_cov)
        # No baseline → regression skipped → threshold pass → exit 0
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        self.assertIn("[PASS]", r.stdout)

    def test_s8_baseline_missing_coverage_below_threshold(self):
        unit_cov = self._lcov("unit.info", 100, 70)  # 70 % < 75 %
        r = self._run("--baseline", "/nonexistent/coverage-baseline.json", "--unit", unit_cov)
        # Informational only — exit 0 even below threshold; [WARN] shown
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        self.assertIn("[WARN]", r.stdout)

    # ===========================================================================
    # SCENARIO 9 — Baseline file contains invalid / malformed JSON
    # Expected: Warning emitted, treated as empty baseline, gate continues
    # ===========================================================================

    def test_s9_malformed_json_above_threshold(self):
        path = os.path.join(self.tmp, "malformed.json")
        with open(path, "w") as fh:
            fh.write("{this is not json}")
        unit_cov = self._lcov("unit.info", 100, 80)
        r = self._run("--baseline", path, "--unit", unit_cov)
        self.assertIn("WARNING", r.stderr)
        # Fallback to empty baseline → threshold-only → pass
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        self.assertIn("[PASS]", r.stdout)

    def test_s9_malformed_json_below_threshold(self):
        path = os.path.join(self.tmp, "malformed2.json")
        with open(path, "w") as fh:
            fh.write("{bad json")
        unit_cov = self._lcov("unit.info", 100, 70)
        r = self._run("--baseline", path, "--unit", unit_cov)
        self.assertIn("WARNING", r.stderr)
        # Informational only — exit 0 even below threshold; [WARN] shown
        self.assertEqual(r.returncode, 0)
        self.assertIn("[WARN]", r.stdout)

    def test_s9_empty_json_file(self):
        path = os.path.join(self.tmp, "empty.json")
        with open(path, "w") as fh:
            fh.write("")
        unit_cov = self._lcov("unit.info", 100, 80)
        r = self._run("--baseline", path, "--unit", unit_cov)
        # Empty file → empty dict baseline → threshold-only → pass
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        self.assertIn("[PASS]", r.stdout)

    def test_s9_non_dict_json(self):
        path = os.path.join(self.tmp, "list_json.json")
        with open(path, "w") as fh:
            json.dump([1, 2, 3], fh)
        unit_cov = self._lcov("unit.info", 100, 80)
        r = self._run("--baseline", path, "--unit", unit_cov)
        # Non-dict JSON → WARNING emitted, empty dict → threshold-only → pass
        self.assertIn("WARNING", r.stderr)
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        self.assertIn("[PASS]", r.stdout)

    # ===========================================================================
    # SCENARIO 10 — unit_tests job fails
    # Coverage Gate does NOT trigger (workflow-level behaviour).
    #
    # GitHub Actions: coverage-gate has `needs: [unit_tests]` with no custom
    # `if:`.  The implicit success() check means coverage-gate is SKIPPED
    # whenever unit_tests fails.  This cannot be unit-tested here; it is
    # enforced by the workflow graph.
    #
    # Artifact-level: if the download step (continue-on-error:true) produces no
    # file, --unit is absent and the script warns gracefully.
    # ===========================================================================

    def test_s10_unit_lcov_artifact_absent(self):
        """
        Simulates the unit-test lcov artifact being absent after the download
        step (which has continue-on-error:true in the coverage-gate job).
        Gate warns informally; exit 0.
        """
        bl = self._baseline({"coverage": 75.0})
        r = self._run("--baseline", bl)  # --unit not provided
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        self.assertIn("coverage data missing", r.stdout)
        self.assertIn("[WARN]", r.stdout)

    # ===========================================================================
    # SCENARIO 11 — Coverage Gate step itself throws an unexpected error
    # Expected: non-zero exit; error visible on stderr
    # ===========================================================================

    def test_s11_missing_required_baseline_arg(self):
        """Invoking the script without --baseline must fail (argparse error)."""
        unit_cov = self._lcov("unit.info", 100, 80)
        r = self._run("--unit", unit_cov)
        # argparse exits with code 2 on missing required argument
        self.assertNotEqual(r.returncode, 0)
        self.assertTrue(len(r.stderr) > 0, "Error must appear on stderr")

    # ===========================================================================
    # First-time setup: empty baseline {} → threshold-only
    # ===========================================================================

    def test_first_time_setup_empty_baseline_passes(self):
        bl = self._baseline({})
        unit_cov = self._lcov("unit.info", 100, 80)
        r = self._run("--baseline", bl, "--unit", unit_cov)
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        self.assertIn("[PASS]", r.stdout)

    def test_first_time_setup_empty_baseline_below_threshold_warns(self):
        bl = self._baseline({})
        unit_cov = self._lcov("unit.info", 100, 70)
        r = self._run("--baseline", bl, "--unit", unit_cov)
        # Informational only — exit 0 even below threshold; [WARN] shown
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        self.assertIn("[WARN]", r.stdout)

    # ===========================================================================
    # Output format validation
    # ===========================================================================

    def test_pass_token_in_output(self):
        bl = self._baseline({"coverage": 75.0})
        unit_cov = self._lcov("unit.info", 100, 80)
        r = self._run("--baseline", bl, "--unit", unit_cov)
        self.assertEqual(r.returncode, 0)
        self.assertIn("[PASS]", r.stdout)

    def test_warn_token_in_output(self):
        bl = self._baseline({"coverage": 75.0})
        unit_cov = self._lcov("unit.info", 100, 70)
        r = self._run("--baseline", bl, "--unit", unit_cov)
        self.assertIn("[WARN]", r.stdout)
        # Informational only — always exit 0
        self.assertEqual(r.returncode, 0)

    # ===========================================================================
    # --output-json baseline extraction
    # ===========================================================================

    def test_output_json_written_when_pass(self):
        bl = self._baseline({"coverage": 75.0})
        unit_cov = self._lcov("unit.info", 100, 80)
        out = os.path.join(self.tmp, "new-baseline.json")
        r = self._run(
            "--baseline", bl,
            "--unit", unit_cov,
            "--output-json", out,
            "--commit", "abc123",
            "--timestamp", "2026-01-01T00:00:00Z",
        )
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        self.assertTrue(os.path.isfile(out), "output-json must be written")
        with open(out) as fh:
            data = json.load(fh)
        self.assertEqual(data["coverage"], 80.0)
        self.assertEqual(data["commit"], "abc123")
        self.assertEqual(data["timestamp"], "2026-01-01T00:00:00Z")

    def test_output_json_written_even_when_gate_warns(self):
        """
        --output-json is written as long as coverage data is available,
        regardless of gate outcome.
        """
        bl = self._baseline({"coverage": 90.0})
        unit_cov = self._lcov("unit.info", 100, 80)  # 80 % < 90 % baseline → WARN
        out = os.path.join(self.tmp, "new-baseline.json")
        r = self._run(
            "--baseline", bl,
            "--unit", unit_cov,
            "--output-json", out,
        )
        # Informational only — always exit 0 regardless of gate outcome
        self.assertEqual(r.returncode, 0)
        self.assertIn("[WARN]", r.stdout)
        self.assertTrue(os.path.isfile(out), "output-json written even on gate warning")
        with open(out) as fh:
            data = json.load(fh)
        self.assertEqual(data["coverage"], 80.0)

    def test_output_json_not_written_when_unit_missing(self):
        """When unit .info is absent, --output-json must NOT be written."""
        bl = self._baseline({"coverage": 75.0})
        out = os.path.join(self.tmp, "new-baseline.json")
        r = self._run("--baseline", bl, "--output-json", out)  # --unit not provided
        self.assertFalse(os.path.isfile(out), "output-json must NOT be written when unit absent")
        self.assertIn("WARNING", r.stderr)

    # ===========================================================================
    # Baseline coercion: non-float coverage values must not crash the script
    # ===========================================================================

    def test_baseline_string_coverage_treated_as_missing(self):
        """String value for 'coverage' in baseline JSON → coerced to None → threshold-only."""
        bl = self._baseline({"coverage": "not-a-number"})
        unit_cov = self._lcov("unit.info", 100, 80)
        r = self._run("--baseline", bl, "--unit", unit_cov)
        # Must not crash; baseline treated as absent → threshold-only → pass
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        self.assertIn("[PASS]", r.stdout)

    def test_baseline_null_coverage_treated_as_missing(self):
        """null value for 'coverage' in baseline JSON → coerced to None → threshold-only."""
        bl = self._baseline({"coverage": None})
        unit_cov = self._lcov("unit.info", 100, 80)
        r = self._run("--baseline", bl, "--unit", unit_cov)
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        self.assertIn("[PASS]", r.stdout)

    def test_baseline_non_float_below_threshold_warns(self):
        """Non-float coverage in baseline → threshold-only → [WARN] exit 0 if below 75%."""
        bl = self._baseline({"coverage": "bad"})
        unit_cov = self._lcov("unit.info", 100, 70)
        r = self._run("--baseline", bl, "--unit", unit_cov)
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        self.assertIn("[WARN]", r.stdout)

    # ===========================================================================
    # _fmt_timestamp: null/non-string timestamp must not crash the report
    # ===========================================================================

    def test_null_timestamp_in_baseline_does_not_crash(self):
        """null timestamp value in baseline JSON → handled gracefully."""
        bl = self._baseline({"coverage": 80.0, "commit": "abc", "timestamp": None})
        unit_cov = self._lcov("unit.info", 100, 80)
        r = self._run("--baseline", bl, "--unit", unit_cov)
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        self.assertIn("[PASS]", r.stdout)

    def test_integer_timestamp_in_baseline_does_not_crash(self):
        """Integer timestamp → handled gracefully."""
        bl = self._baseline({"coverage": 80.0, "commit": "abc", "timestamp": 12345})
        unit_cov = self._lcov("unit.info", 100, 80)
        r = self._run("--baseline", bl, "--unit", unit_cov)
        self.assertEqual(r.returncode, 0, msg=r.stdout + r.stderr)
        self.assertIn("[PASS]", r.stdout)


if __name__ == "__main__":
    unittest.main(verbosity=2)
