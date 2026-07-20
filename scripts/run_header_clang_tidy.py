#!/usr/bin/env python3
"""Run clang-tidy with every configured first-party header as a primary include."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


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


def optional_component(source_root: Path, path: Path) -> Path | None:
    """Return the independently enabled presenter/renderer component for path."""
    try:
        relative = path.resolve().relative_to(source_root.resolve())
    except ValueError:
        return None
    parts = relative.parts
    if (
        len(parts) >= 3
        and parts[0] == "src"
        and parts[1]
        in {
            "displays",
            "renderers",
        }
    ):
        return source_root / Path(*parts[:3])
    return None


def entries_for_header(
    source_root: Path,
    header: Path,
    entries: list[dict[str, object]],
) -> list[dict[str, object]]:
    """Select the compile context that actually owns an optional header."""
    component = optional_component(source_root, header)
    if component is not None:
        return [
            entry
            for entry in entries
            if Path(str(entry["file"])).resolve().is_relative_to(component.resolve())
        ]
    return entries


def run_one(
    clang_tidy: str,
    database_dir: Path,
    source: Path,
    reported_header: Path,
    checks: str | None = None,
) -> tuple[Path, int, str]:
    command = [
        clang_tidy,
        "--quiet",
        f"-p={database_dir}",
        "-header-filter=.*",
        str(source),
    ]
    if checks is not None:
        command.insert(2, f"-checks={checks}")
    result = subprocess.run(
        command,
        check=False,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    return reported_header, result.returncode, result.stdout


def include_name(source_root: Path, header: Path) -> Path:
    for include_root in (source_root / "src", source_root / "tests"):
        try:
            return header.relative_to(include_root)
        except ValueError:
            continue
    raise ValueError(f"header is outside first-party include roots: {header}")


def write_header_tu(source_root: Path, output_dir: Path, header: Path) -> Path:
    relative = include_name(source_root, header)
    generated_name = "__".join(relative.parts).replace(".", "_") + ".c"
    generated = output_dir / generated_name
    generated.write_text(
        f'#include "{relative.as_posix()}" // NOLINT(misc-include-cleaner)\n'
        "typedef int db_header_lint_translation_unit_t;\n",
        encoding="ascii",
    )
    return generated


def contains_diagnostic(output: str) -> bool:
    """Distinguish real clang diagnostics from platform-specific summaries."""
    return re.search(r"\b(?:warning|error):", output) is not None


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

    output_dir = Path(tempfile.mkdtemp(prefix="header-clang-tidy-", dir=build_dir))

    headers = sorted(
        [
            *(source_root / "src").rglob("*.h"),
            *(source_root / "tests").rglob("*.h"),
        ]
    )
    if sys.platform != "linux":
        headers = [
            header
            for header in headers
            if "displays/linux_kms_atomic" not in header.as_posix()
        ]
    header_entries = {
        header: entries_for_header(source_root, header, entries) for header in headers
    }
    headers = [header for header in headers if header_entries[header]]
    generated_by_header = {
        header: write_header_tu(source_root, output_dir, header) for header in headers
    }
    synthetic_database = []
    for header, generated in generated_by_header.items():
        entry = representative_entry(header_entries[header])
        compiler = command_arguments(entry)[0]
        flags = [*analysis_flags(entry), "-I", str(source_root / "tests")]
        synthetic_database.append(
            {
                "directory": str(source_root),
                "file": str(generated),
                "arguments": [compiler, *flags, "-c", str(generated)],
            }
        )
        synthetic_database.append(
            {
                "directory": str(source_root),
                "file": str(header),
                "arguments": [
                    compiler,
                    *flags,
                    "-Wno-unused-macros",
                    "-x",
                    "c-header",
                    "-c",
                    str(header),
                ],
            }
        )
    (output_dir / "compile_commands.json").write_text(
        json.dumps(synthetic_database, indent=2) + "\n", encoding="utf-8"
    )

    failures: list[tuple[Path, int, str]] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        futures = [
            pool.submit(run_one, clang_tidy, output_dir, generated, header)
            for header, generated in generated_by_header.items()
        ]
        futures.extend(
            pool.submit(
                run_one,
                clang_tidy,
                output_dir,
                header,
                header,
                "-*,misc-include-cleaner",
            )
            for header in headers
        )
        for future in concurrent.futures.as_completed(futures):
            header, returncode, output = future.result()
            if returncode != 0 or contains_diagnostic(output):
                failures.append((header, returncode, output))

    if failures:
        for header, returncode, output in sorted(failures):
            relative = header.relative_to(source_root)
            print(f"== {relative} (exit {returncode}) ==", file=sys.stderr)
            print(output.rstrip(), file=sys.stderr)
        shutil.rmtree(output_dir)
        return 1

    shutil.rmtree(output_dir)
    print(f"Header clang-tidy passed for {len(headers)} headers.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
