#!/usr/bin/env python3
"""Regression tests for capability-aware CTest result auditing."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
from pathlib import Path


def run_auditor(script: Path, root: Path, required: bool) -> int:
    junit = root / "results.xml"
    manifest = root / "manifest.json"
    skip_marker = "DB_CTEST_OUTCOME=SKIP reason=capability_absent capability=gpu"
    junit.write_text(
        f"""<?xml version="1.0"?>
<testsuite tests="1" failures="0" skipped="1">
  <testcase name="hardware_path" status="notrun">
    <skipped message="SKIP_REGULAR_EXPRESSION_MATCHED"/>
    <system-out>{skip_marker}</system-out>
  </testcase>
</testsuite>
""",
        encoding="utf-8",
    )
    manifest.write_text(
        json.dumps(
            {
                "schema": 1,
                "requirements": [
                    {
                        "capability": "gpu",
                        "test_regex": "^hardware_path$",
                        "required": required,
                        "minimum_executed": 1 if required else 0,
                        "allowed_skip_reasons": ["capability_absent"],
                    }
                ],
            }
        ),
        encoding="utf-8",
    )
    return subprocess.run(
        [
            sys.executable,
            str(script),
            "--junit",
            str(junit),
            "--manifest",
            str(manifest),
        ],
        check=False,
        capture_output=True,
        text=True,
    ).returncode


def main() -> int:
    if len(sys.argv) != 2:
        return 2
    script = Path(sys.argv[1])
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        if run_auditor(script, root, required=False) != 0:
            return 1
        if run_auditor(script, root, required=True) == 0:
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
