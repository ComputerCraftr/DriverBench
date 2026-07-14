#!/usr/bin/env python3
"""Run the LLVM 22 formatting and tidy CI contracts."""

from __future__ import annotations

import argparse
import os
import re
import subprocess
from pathlib import Path


def run(command: list[str], root: Path) -> None:
    subprocess.run(command, cwd=root, check=True)


def verify_llvm22(compiler: str, root: Path) -> None:
    result = subprocess.run(
        [compiler, "--version"],
        cwd=root,
        check=True,
        capture_output=True,
        text=True,
    )
    if re.search(r"clang version 22(?:[. ]|$)", result.stdout) is None:
        raise RuntimeError("LLVM 22 compiler is required")


def run_format(root: Path) -> None:
    runner = root / "scripts/run_developer_tools.py"
    common = ["python3", str(runner), "--source-root", str(root)]
    run([*common, "c-format", "--tool", "clang-format-22", "--check"], root)
    run([*common, "cmake-format", "--tool", "cmake-format", "--check"], root)
    run(
        [*common, "python-check", "--ruff", "ruff", "--mypy", "mypy"],
        root,
    )


def run_tidy(root: Path, compiler: str, build_dir: Path) -> None:
    run(
        [
            "cmake",
            "--fresh",
            "-S",
            str(root),
            "-B",
            str(build_dir),
            "-G",
            "Ninja",
            "-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_C_COMPILER={compiler}",
            "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
            "-DDB_ENABLE_LTO=OFF",
            "-DDB_GLFW_PROVIDER=vendored",
            "-DDB_GLFW_REQUIRED=ON",
            "-DDB_TEST_HEADLESS_ONLY=ON",
        ],
        root,
    )
    run(["cmake", "--build", str(build_dir), "--parallel"], root)
    run(
        [
            "cmake",
            "-DTIDY_COMMAND=run-clang-tidy-22",
            f"-DBUILD_DIR={build_dir}",
            f"-DSOURCE_ROOT={root}",
            f"-DLOG_PATH={build_dir / 'clang-tidy.log'}",
            "-DJOBS=32",
            "-P",
            str(root / "cmake/RunClangTidy.cmake"),
        ],
        root,
    )
    run(
        [
            "python3",
            str(root / "scripts/run_header_clang_tidy.py"),
            "--source-root",
            str(root),
            "--build-dir",
            str(build_dir),
            "--clang-tidy",
            "clang-tidy-22",
            "--jobs",
            "32",
        ],
        root,
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "mode", choices=("format", "tidy", "all"), nargs="?", default="all"
    )
    parser.add_argument(
        "--build-dir",
        type=Path,
        default=Path(os.environ.get("DB_QUALITY_BUILD_DIR", "build/ci-quality")),
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    compiler = os.environ.get("CC", "clang-22")
    build_dir = args.build_dir
    if not build_dir.is_absolute():
        build_dir = root / build_dir
    verify_llvm22(compiler, root)
    if args.mode in {"format", "all"}:
        run_format(root)
    if args.mode in {"tidy", "all"}:
        run_tidy(root, compiler, build_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
