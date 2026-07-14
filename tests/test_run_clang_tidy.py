#!/usr/bin/env python3
"""Regression coverage for atomic clang-tidy log publication."""

from __future__ import annotations

import concurrent.futures
import subprocess
import sys
import tempfile
from pathlib import Path


def write_runner(path: Path, output: str, status: int) -> None:
    path.write_text(
        f"#!/bin/sh\nprintf '%s\\n' {output!r}\nexit {status}\n",
        encoding="utf-8",
    )
    path.chmod(0o755)


def run_contract(
    cmake: Path, contract: Path, root: Path, runner: Path, log: Path
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            str(cmake),
            f"-DTIDY_COMMAND={runner}",
            f"-DBUILD_DIR={root}",
            f"-DSOURCE_ROOT={root}",
            f"-DLOG_PATH={log}",
            "-DJOBS=2",
            "-P",
            str(contract),
        ],
        check=False,
        capture_output=True,
        text=True,
    )


def require(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


def main() -> int:
    if len(sys.argv) != 3:
        return 2
    cmake = Path(sys.argv[1]).resolve()
    contract = Path(sys.argv[2]).resolve()

    with tempfile.TemporaryDirectory() as directory:
        root = Path(directory)
        clean = root / "clean"
        failing = root / "failing"
        warning = root / "warning"
        write_runner(clean, "clean", 0)
        write_runner(failing, "runner failed without a diagnostic", 3)
        write_runner(warning, "sample.c:1:1: warning: diagnostic", 0)

        log = root / "clang-tidy.log"
        log.write_text("stale.c:1:1: error: stale\n", encoding="utf-8")
        clean_result = run_contract(cmake, contract, root, clean, log)
        require(clean_result.returncode == 0, clean_result.stderr)
        require(log.read_text(encoding="utf-8") == "clean\n", "stale log survived")
        require(
            run_contract(cmake, contract, root, failing, log).returncode != 0,
            "nonzero runner status was accepted",
        )
        require(
            run_contract(cmake, contract, root, warning, log).returncode != 0,
            "warning diagnostic was accepted",
        )

        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
            futures = [
                executor.submit(run_contract, cmake, contract, root, clean, log)
                for _ in range(2)
            ]
            results = [future.result() for future in futures]
        require(
            all(result.returncode == 0 for result in results),
            "concurrent clean runners failed",
        )
        require(log.read_text(encoding="utf-8") == "clean\n", "final log is invalid")
        require(not list(root.glob("clang-tidy.log.*.tmp")), "temporary log leaked")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(error, file=sys.stderr)
        raise SystemExit(1) from error
