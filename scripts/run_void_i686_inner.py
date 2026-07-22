#!/usr/bin/env python3
"""Execute the native i686 sanitizer contract inside the Void container."""

from __future__ import annotations

import re
import subprocess
from pathlib import Path


def run(command: list[str], root: Path, *, capture: bool = False) -> str:
    result = subprocess.run(
        command,
        cwd=root,
        check=True,
        capture_output=capture,
        text=capture,
    )
    return result.stdout if capture else ""


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    if run(["getconf", "LONG_BIT"], root, capture=True).strip() != "32":
        raise RuntimeError("Void container userland is not 32-bit")
    version = run(["clang", "--version"], root, capture=True)
    if re.search(r"clang version 22(?:[. ]|$)", version) is None:
        raise RuntimeError("Void container does not provide Clang 22")

    build_dir = root / "build/linux32-sanitize"
    junit_path = build_dir / "ctest-results.xml"
    result_auditor = root / "scripts/audit_ctest_results.py"
    capability_manifest = root / "ci/capabilities/i686-sanitizer.json"
    run(
        [
            "cmake",
            "--fresh",
            "--preset",
            "ninja-linux-gnu-i686-clang-headless-sanitize",
        ],
        root,
    )
    run(["cmake", "--build", "--preset", "i686-headless-sanitize"], root)
    run(
        [
            "ctest",
            "--preset",
            "i686-headless-sanitize",
            "--output-junit",
            str(junit_path),
        ],
        root,
    )
    run(
        [
            "python3",
            str(result_auditor),
            "--junit",
            str(junit_path),
            "--manifest",
            str(capability_manifest),
        ],
        root,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
