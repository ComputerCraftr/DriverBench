#!/usr/bin/env python3
"""Run first-party formatting and Python validation with tracked-file scope."""

from __future__ import annotations

import argparse
import shutil
import subprocess
from pathlib import Path
from typing import NoReturn


def fail(message: str) -> NoReturn:
    raise SystemExit(message)


def resolve_tool(value: str | None, label: str) -> str:
    if value is None or value.endswith("-NOTFOUND"):
        fail(f"{label} is unavailable")
    resolved = shutil.which(value)
    if resolved is None:
        candidate = Path(value)
        if candidate.is_file():
            return str(candidate)
        fail(f"{label} is unavailable: {value}")
    return resolved


def tracked_files(source_root: Path) -> list[Path]:
    result = subprocess.run(
        [
            "git",
            "-C",
            str(source_root),
            "ls-files",
            "-z",
            "--cached",
            "--others",
            "--exclude-standard",
        ],
        check=True,
        capture_output=True,
    )
    return [
        source_root / item.decode("utf-8")
        for item in result.stdout.split(b"\0")
        if item and (source_root / item.decode("utf-8")).is_file()
    ]


def selected_files(source_root: Path, kind: str) -> list[Path]:
    files = tracked_files(source_root)
    if kind == "c":
        return [path for path in files if path.suffix in {".c", ".h"}]
    if kind == "cmake":
        return [
            path
            for path in files
            if path.name == "CMakeLists.txt" or path.suffix == ".cmake"
        ]
    if kind == "python":
        return [
            path
            for path in files
            if path.suffix == ".py"
            and path.parts[len(source_root.parts)] in {"scripts", "tests"}
        ]
    fail(f"unsupported file kind: {kind}")


def run(command: list[str], source_root: Path) -> None:
    subprocess.run(command, cwd=source_root, check=True)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, required=True)
    subparsers = parser.add_subparsers(dest="operation", required=True)

    for operation in ("c-format", "cmake-format"):
        formatter = subparsers.add_parser(operation)
        formatter.add_argument("--tool", required=True)
        action = formatter.add_mutually_exclusive_group(required=True)
        action.add_argument("--check", action="store_true")
        action.add_argument("--fix", action="store_true")

    python_check = subparsers.add_parser("python-check")
    python_check.add_argument("--ruff", required=True)
    python_check.add_argument("--mypy", required=True)
    python_format = subparsers.add_parser("python-format")
    python_format.add_argument("--ruff", required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    source_root = args.source_root.resolve()
    operation = str(args.operation)

    if operation == "c-format":
        tool = resolve_tool(args.tool, "clang-format")
        files = selected_files(source_root, "c")
        action = ["--dry-run", "--Werror"] if args.check else ["-i"]
        run([tool, *action, *(str(path) for path in files)], source_root)
    elif operation == "cmake-format":
        tool = resolve_tool(args.tool, "cmake-format")
        files = selected_files(source_root, "cmake")
        action = ["--check"] if args.check else ["-i"]
        run(
            [
                tool,
                "--config-file",
                str(source_root / ".cmake-format.json"),
                *action,
                *(str(path) for path in files),
            ],
            source_root,
        )
    elif operation == "python-check":
        ruff = resolve_tool(args.ruff, "ruff")
        mypy = resolve_tool(args.mypy, "mypy")
        files = selected_files(source_root, "python")
        run([ruff, "check", *(str(path) for path in files)], source_root)
        run([ruff, "format", "--check", *(str(path) for path in files)], source_root)
        run([mypy, "--strict", *(str(path) for path in files)], source_root)
    elif operation == "python-format":
        ruff = resolve_tool(args.ruff, "ruff")
        files = selected_files(source_root, "python")
        run([ruff, "check", "--fix", *(str(path) for path in files)], source_root)
        run([ruff, "format", *(str(path) for path in files)], source_root)
    else:
        fail(f"unsupported operation: {operation}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
