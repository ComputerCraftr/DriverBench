#!/usr/bin/env python3
"""Run clang-tidy with every configured first-party header as a primary include."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--clang-tidy", default="clang-tidy")
    parser.add_argument("--jobs", type=int, default=max(1, os.cpu_count() or 1))
    return parser.parse_args()


def command_arguments(entry: dict[str, object]) -> list[str]:
    arguments = entry.get("arguments")
    if isinstance(arguments, list) and all(isinstance(item, str) for item in arguments):
        return list(arguments)
    command = entry.get("command")
    if isinstance(command, str):
        return shlex.split(command)
    raise ValueError("compile command has neither string arguments nor command")


def analysis_flags(entry: dict[str, object]) -> list[str]:
    arguments = command_arguments(entry)
    source_file = str(entry["file"])
    flags: list[str] = []
    skip_next = False
    options_with_values = {"-o", "-MF", "-MT", "-MQ"}
    for index, argument in enumerate(arguments[1:], start=1):
        if skip_next:
            skip_next = False
            continue
        if argument in options_with_values:
            skip_next = True
            continue
        if argument in {"-c", "-MD", "-MMD", "-MP"}:
            continue
        if (
            argument == source_file
            or Path(argument).resolve() == Path(source_file).resolve()
        ):
            continue
        if index > 0 and any(
            argument.startswith(option) for option in ("-MF", "-MT", "-MQ")
        ):
            continue
        flags.append(argument)
    return flags


def representative_entry(entries: list[dict[str, object]]) -> dict[str, object]:
    if not entries:
        raise ValueError("compile_commands.json contains no entries")
    return max(entries, key=lambda entry: len(command_arguments(entry)))


def write_header_tu(source_root: Path, output_dir: Path, header: Path) -> Path:
    relative = header.relative_to(source_root / "src")
    generated_name = "__".join(relative.parts).replace(".", "_") + ".c"
    generated = output_dir / generated_name
    generated.write_text(
        f'#include "{relative.as_posix()}" // NOLINT(misc-include-cleaner)\n'
        "typedef int db_header_lint_translation_unit_t;\n",
        encoding="ascii",
    )
    return generated


def run_one(
    clang_tidy: str, database_dir: Path, generated: Path, header: Path
) -> tuple[Path, int, str]:
    command = [
        clang_tidy,
        "--quiet",
        f"-p={database_dir}",
        "-header-filter=.*",
        str(generated),
    ]
    result = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    return header, result.returncode, result.stdout


def main() -> int:
    args = parse_args()
    source_root = args.source_root.resolve()
    build_dir = args.build_dir.resolve()
    clang_tidy = shutil.which(args.clang_tidy)
    if clang_tidy is None:
        print(
            f"header clang-tidy executable not found: {args.clang_tidy}",
            file=sys.stderr,
        )
        return 2

    database_path = build_dir / "compile_commands.json"
    if not database_path.is_file():
        print(f"compile database not found: {database_path}", file=sys.stderr)
        return 2
    entries = json.loads(database_path.read_text(encoding="utf-8"))
    if not isinstance(entries, list):
        print(f"invalid compile database: {database_path}", file=sys.stderr)
        return 2

    output_dir = build_dir / "header-clang-tidy"
    if output_dir.exists():
        shutil.rmtree(output_dir)
    output_dir.mkdir(parents=True)

    entry = representative_entry(entries)
    compiler = command_arguments(entry)[0]
    flags = analysis_flags(entry)
    headers = sorted((source_root / "src").rglob("*.h"))
    if sys.platform != "linux":
        headers = [
            header
            for header in headers
            if "displays/linux_kms_atomic" not in header.as_posix()
        ]
    generated_by_header = {
        header: write_header_tu(source_root, output_dir, header) for header in headers
    }
    synthetic_database = [
        {
            "directory": str(source_root),
            "file": str(generated),
            "arguments": [compiler, *flags, "-c", str(generated)],
        }
        for generated in generated_by_header.values()
    ]
    (output_dir / "compile_commands.json").write_text(
        json.dumps(synthetic_database, indent=2) + "\n", encoding="utf-8"
    )

    failures: list[tuple[Path, int, str]] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [
            pool.submit(run_one, clang_tidy, output_dir, generated, header)
            for header, generated in generated_by_header.items()
        ]
        for future in concurrent.futures.as_completed(futures):
            header, returncode, output = future.result()
            if returncode != 0 or output.strip():
                failures.append((header, returncode, output))

    if failures:
        for header, returncode, output in sorted(failures):
            relative = header.relative_to(source_root)
            print(f"== {relative} (exit {returncode}) ==", file=sys.stderr)
            print(output.rstrip(), file=sys.stderr)
        return 1

    print(f"Header clang-tidy passed for {len(headers)} headers.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
