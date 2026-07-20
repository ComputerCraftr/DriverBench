#!/usr/bin/env python3
"""Verify every C unit suite is dispatched once and failures reach process exit."""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path

RUNNER_DEFINITION = re.compile(
    r"\bunsigned\s+(db_[A-Za-z0-9_]+_test_run_all)\s*\(\s*void\s*\)\s*\{"
)
RUNNER_CALL = re.compile(r"\b(db_[A-Za-z0-9_]+_test_run_all)\s*\(\s*\)")
RUN_CASE_RETURN = re.compile(r"\breturn\s+db_test_run_cases\s*\(")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    if len(sys.argv) != 3:
        return 2
    source_root = Path(sys.argv[1]).resolve()
    unit_binary = Path(sys.argv[2]).resolve()
    test_sources = sorted((source_root / "tests").glob("test_*.c"))
    definitions: dict[str, Path] = {}
    for source in test_sources:
        text = source.read_text(encoding="utf-8")
        source_definitions = RUNNER_DEFINITION.findall(text)
        for name in source_definitions:
            require(
                name not in definitions, f"duplicate test runner definition: {name}"
            )
            definitions[name] = source
        if source_definitions:
            require(
                len(source_definitions) == 1,
                f"{source.relative_to(source_root)} must define one test runner",
            )
            require(
                len(RUN_CASE_RETURN.findall(text)) == 1,
                f"{source.relative_to(source_root)} must return db_test_run_cases",
            )

    main_source = source_root / "tests/test_main.c"
    main_text = main_source.read_text(encoding="utf-8")
    calls = RUNNER_CALL.findall(main_text)
    for name, source in definitions.items():
        require(
            calls.count(name) == 1,
            f"{source.relative_to(source_root)}: {name} must be dispatched once",
        )
    extras = sorted(set(calls) - definitions.keys())
    require(not extras, f"dispatcher calls undefined test runners: {extras}")

    for source in test_sources:
        if source == main_source:
            continue
        nested_calls = RUNNER_CALL.findall(source.read_text(encoding="utf-8"))
        require(
            not nested_calls,
            f"{source.relative_to(source_root)} nests test runners: {nested_calls}",
        )

    failure = subprocess.run(
        [str(unit_binary), "--verify-failure-propagation"],
        check=False,
        capture_output=True,
        text=True,
    )
    require(failure.returncode != 0, "deliberate assertion failure returned success")
    require("expected true: 0" in failure.stderr, "failure diagnostic was lost")
    nan_failure = subprocess.run(
        [str(unit_binary), "--verify-nan-failure-propagation"],
        check=False,
        capture_output=True,
        text=True,
    )
    require(nan_failure.returncode != 0, "NaN assertion failure returned success")
    require("nan != 0.000000" in nan_failure.stderr, "NaN diagnostic was lost")
    counted_failure = subprocess.run(
        [str(unit_binary), "--verify-failure-count-propagation"],
        check=False,
        capture_output=True,
        text=True,
    )
    require(
        counted_failure.returncode == 2,
        "two deliberate assertion failures did not propagate as exit status 2",
    )
    require(
        counted_failure.stderr.count("expected true: 0") == 2,
        "one of the deliberate assertion diagnostics was lost",
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(error, file=sys.stderr)
        raise SystemExit(1) from error
