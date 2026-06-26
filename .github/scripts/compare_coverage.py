#!/usr/bin/env python3
# If not stated otherwise in this file or this component's LICENSE file the
# following copyright and licenses apply:
#
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
Coverage gate for firebolt-cpp-transport.

Reads unit-test line coverage from an lcov .info file produced by gcovr,
compares it against a stored baseline (build-metadata branch), and prints a
summary report.  For valid invocations it exits 0 (informational gate);
invalid CLI usage is reported by argparse with a non-zero exit status.
"""

import argparse
import datetime
import json
import os
import sys
from typing import Optional


THRESHOLD = 75.0

_GREEN = "\033[32m"
_RED   = "\033[31m"
_RESET = "\033[0m"
_SEP   = "\u2500" * 64


def _colored(token: str, ok: bool) -> str:
    if os.environ.get("NO_COLOR") is not None:
        return token
    return f"{_GREEN if ok else _RED}{token}{_RESET}"


def _fmt_timestamp(ts_raw: object) -> str:
    """Format an ISO 8601 UTC timestamp string for display, or return 'unknown'."""
    if not isinstance(ts_raw, str):
        return "unknown"
    try:
        dt = datetime.datetime.strptime(ts_raw, "%Y-%m-%dT%H:%M:%SZ")
        return dt.strftime("%Y-%m-%d %H:%M UTC")
    except ValueError:
        return ts_raw or "unknown"


def parse_lcov_coverage(path: Optional[str]) -> Optional[float]:
    """Return overall line coverage % from an lcov .info file, or None.

    Aggregates LF (lines found) and LH (lines hit) across all records in the
    file to produce a single project-wide percentage.
    Returns None when the file is absent, empty, or contains no line data.
    """
    if not path or not os.path.isfile(path):
        return None

    total_found = total_hit = 0
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            for raw in fh:
                line = raw.strip()
                if line.startswith("LF:"):
                    try:
                        total_found += int(line[3:])
                    except ValueError:
                        pass
                elif line.startswith("LH:"):
                    try:
                        total_hit += int(line[3:])
                    except ValueError:
                        pass
    except OSError as exc:
        print(f"WARNING: Could not read {path}: {exc}", file=sys.stderr)
        return None

    if total_found == 0:
        return None

    return round((total_hit / total_found) * 100.0, 2)


def load_baseline(path: str) -> dict:
    """Load baseline JSON; return empty dict on any error."""
    if not path or not os.path.isfile(path):
        return {}
    try:
        with open(path, "r", encoding="utf-8") as fh:
            data = json.load(fh)
        if isinstance(data, dict):
            return data
        print(
            f"WARNING: Baseline {path} is not a JSON object \u2014 ignoring",
            file=sys.stderr,
        )
    except (OSError, json.JSONDecodeError) as exc:
        print(f"WARNING: Could not parse baseline {path}: {exc}", file=sys.stderr)
    return {}


def _suite_analysis(
    current: Optional[float],
    baseline_cov: Optional[float],
) -> tuple:
    """Analyse coverage for the unit-test suite.

    Args:
        current:      Measured line coverage %, or None if the lcov file was
                      absent or unreadable.
        baseline_cov: Stored baseline coverage %, or None if no baseline exists.

    Returns:
        A 4-tuple (ok, result_str, delta_str, reason):
        - ok:         True when coverage meets both the threshold and regression checks.
        - result_str: Human-readable status — the coverage percentage, or a
                      "coverage data missing" message when current is None.
        - delta_str:  Change from baseline as "+X.XX%" / "-X.XX%" / "N/A".
        - reason:     None when ok, otherwise a description of what failed.
    """
    if current is None:
        msg = "coverage data missing \u2014 lcov artifact absent or unreadable"
        return False, msg, "N/A", msg

    delta_str = "N/A"
    threshold_ok = current >= THRESHOLD
    regression_ok = True

    if baseline_cov is not None and baseline_cov > 0.0:
        regression_ok = current >= baseline_cov
        delta = current - baseline_cov
        delta_str = f"{'+' if delta >= 0 else ''}{delta:.2f}%"

    ok = threshold_ok and regression_ok
    reasons = []
    if not threshold_ok:
        reasons.append(f"below threshold ({THRESHOLD}%)")
    if not regression_ok:
        reasons.append(f"dropped from baseline ({baseline_cov:.2f}%)")

    reason = " \u00b7 ".join(reasons) if reasons else None
    return ok, f"{current:.2f}%", delta_str, reason


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Coverage gate for firebolt-cpp-transport unit tests."
    )
    parser.add_argument(
        "--baseline", required=False, default="", metavar="PATH",
        help="Path to coverage-baseline.json (omit to run threshold-only).",
    )
    parser.add_argument(
        "--unit", required=False, metavar="PATH",
        help="Path to the unit-test lcov coverage.info file.",
    )
    parser.add_argument(
        "--output-json", required=False, metavar="PATH",
        help=(
            "Write a new coverage-baseline.json to PATH when coverage data is "
            "available.  --commit and --timestamp are optional metadata fields."
        ),
    )
    parser.add_argument(
        "--commit", required=False, default="unknown", metavar="SHA",
        help="Commit SHA to embed in --output-json (default: 'unknown').",
    )
    parser.add_argument(
        "--timestamp", required=False,
        default=datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        metavar="ISO8601",
        help="UTC timestamp to embed in --output-json (default: current time).",
    )
    args = parser.parse_args()

    baseline = load_baseline(args.baseline)
    baseline_cov: Optional[float] = None
    if baseline:
        v = baseline.get("coverage")
        try:
            baseline_cov = float(v) if v is not None else None
        except (TypeError, ValueError):
            baseline_cov = None

    current: Optional[float] = parse_lcov_coverage(args.unit) if args.unit else None

    ok, result_str, delta_str, reason = _suite_analysis(current, baseline_cov)

    # ── report ────────────────────────────────────────────────────────────────
    print()
    print(_SEP)
    if baseline:
        commit = baseline.get("commit", "unknown")
        ts = _fmt_timestamp(baseline.get("timestamp", ""))
        base_disp = f"{baseline_cov:.2f}%" if baseline_cov is not None else "N/A"
        print(f"  Baseline  {base_disp}  (commit {commit}, {ts})")
    else:
        print("  Baseline  N/A  (first run \u2014 threshold check only)")
    print(f"  Threshold {THRESHOLD}%")
    delta_disp = f"  \u0394 {delta_str}" if delta_str != "N/A" else ""
    cur_disp = f"{current:.2f}%" if current is not None else "N/A"
    print(f"  Current   {cur_disp}{delta_disp}")
    print(_SEP)
    if reason:
        print(f"  {reason}")
    token = _colored("[PASS]", True) if ok else _colored("[WARN]", False)
    print(f"  {token}  (informational \u2014 PRs are never blocked)")
    print(_SEP)
    print()

    # ── optional baseline output ───────────────────────────────────────────────
    if args.output_json:
        if current is not None:
            new_baseline = {
                "coverage": current,
                "commit": args.commit,
                "timestamp": args.timestamp,
            }
            with open(args.output_json, "w", encoding="utf-8") as fh:
                json.dump(new_baseline, fh, indent=2)
                fh.write("\n")
        else:
            print(
                f"WARNING: --output-json requested but coverage data is absent; "
                f"{args.output_json} not written",
                file=sys.stderr,
            )

    sys.exit(0)


if __name__ == "__main__":
    main()
