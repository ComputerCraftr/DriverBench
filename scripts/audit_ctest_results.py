#!/usr/bin/env python3
"""Audit CTest JUnit results against a job capability manifest."""

from __future__ import annotations

import argparse
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Any, cast

SKIP_PATTERN = re.compile(
    r"DB_CTEST_OUTCOME=SKIP reason=([a-z0-9_]+) capability=([a-z0-9_]+)"
)


def load_manifest(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if value.get("schema") != 1 or not isinstance(value.get("requirements"), list):
        raise ValueError("unsupported capability manifest")
    return cast(dict[str, Any], value)


def testcase_output(case: ET.Element) -> str:
    values = [case.get("name", "")]
    for tag in ("system-out", "system-err", "skipped", "failure", "error"):
        for child in case.findall(tag):
            values.append(child.get("message", ""))
            values.append(child.text or "")
    return "\n".join(values)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--junit", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()

    manifest = load_manifest(args.manifest)
    root = ET.parse(args.junit).getroot()
    cases = root.findall(".//testcase")
    failures: list[str] = []
    summaries: list[str] = []

    for requirement in manifest["requirements"]:
        capability = str(requirement["capability"])
        pattern = re.compile(str(requirement["test_regex"]))
        required = bool(requirement.get("required", False))
        minimum = int(requirement.get("minimum_executed", 1))
        allowed_reasons = set(requirement.get("allowed_skip_reasons", []))
        matched = [case for case in cases if pattern.search(case.get("name", ""))]
        executed = 0
        skipped = 0
        failed = 0
        for case in matched:
            output = testcase_output(case)
            skip_match = SKIP_PATTERN.search(output)
            is_skipped = case.find("skipped") is not None
            is_failed = (
                case.find("failure") is not None or case.find("error") is not None
            )
            if is_failed:
                failed += 1
            elif is_skipped:
                skipped += 1
                if skip_match is None:
                    failures.append(
                        f"{capability}: skipped test lacks canonical marker: "
                        f"{case.get('name', '')}"
                    )
                elif skip_match.group(1) not in allowed_reasons and not required:
                    failures.append(
                        f"{capability}: unapproved skip reason {skip_match.group(1)}"
                    )
            else:
                executed += 1
        summaries.append(
            f"{capability}: executed={executed} skipped={skipped} failed={failed}"
        )
        if failed != 0:
            failures.append(f"{capability}: {failed} test(s) failed")
        if required and skipped != 0:
            failures.append(f"{capability}: required tests skipped")
        if executed < minimum:
            failures.append(
                f"{capability}: executed {executed}, required at least {minimum}"
            )

    print("\n".join(summaries))
    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
