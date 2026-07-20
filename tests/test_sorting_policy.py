#!/usr/bin/env python3
"""Regression fixtures for the canonical sorting source policy."""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def run_checker(checker: Path, root: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            "cmake",
            f"-DSOURCE_ROOT={root}",
            "-DRULE_SET=sorting_policy",
            "-P",
            str(checker),
        ],
        check=False,
        capture_output=True,
        text=True,
    )


def write_fixture(root: Path, source: str) -> None:
    (root / "src").mkdir(parents=True)
    (root / "src/fixture.c").write_text(source, encoding="ascii")


def main() -> int:
    if len(sys.argv) != 2:
        return 2
    checker = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        write_fixture(
            root,
            """
void bad(unsigned *values, unsigned count) {
    for (unsigned index = 1U; index < count; index++) {
        unsigned position = index;
        while ((position > 0U) &&
               (values[position - 1U] > values[position])) {
            values[position] = values[position - 1U];
            position--;
        }
    }
}
""",
        )
        result = run_checker(checker, root)
        require(result.returncode != 0, "inline insertion sort was accepted")
        require("src/fixture.c:" in result.stderr, result.stderr)
    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        write_fixture(
            root,
            """
#include <stdlib.h>
void bad(unsigned *values, unsigned count,
         int (*compare)(const void *, const void *)) {
    qsort(values, count, sizeof(*values), compare);
}
""",
        )
        result = run_checker(checker, root)
        require(result.returncode != 0, "direct libc sorting was accepted")
        require("src/fixture.c:" in result.stderr, result.stderr)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(error, file=sys.stderr)
        raise SystemExit(1) from error
