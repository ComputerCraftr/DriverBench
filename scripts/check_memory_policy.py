#!/usr/bin/env python3
"""Reject C memory operations that bypass repository ownership policy."""

from __future__ import annotations

import argparse
import re
import sys
from collections.abc import Iterator
from pathlib import Path

from check_numeric_policy import (
    first_party_sources,
    line_at,
    strip_comments_and_literals,
)

FREE_CAST_PATTERN = re.compile(r"\bfree\s*\(\s*\(\s*void\s*\*\s*\)")
DIRECT_REALLOC_PATTERN = re.compile(
    r"(?P<target>[A-Za-z_][A-Za-z0-9_]*(?:(?:->|\.)[A-Za-z_][A-Za-z0-9_]*)*)"
    r"\s*=\s*(?:\([^()]+\)\s*)?realloc\s*\(\s*(?P=target)\b"
)
FIXED_SIZE_MULTIPLICATION = re.compile(
    r"^\s*(?:[0-9]+[uUlL]*\s*\*\s*)?sizeof\s*\([^()]+\)\s*$"
)
ARITHMETIC_OPERATOR = re.compile(r"(?<![<>=!])(?:\+|-|\*)(?![=>])")
MALLOC_MULTIPLICATION_ALLOWLIST = {
    Path("src/core/db_core.h"),
    Path("src/core/db_render_ir_snapshot.c"),
}
RANGE_OVERLAP_HELPER_PATTERN = re.compile(
    r"\b(?:static\s+)?int\s+[A-Za-z_][A-Za-z0-9_]*"
    r"(?:memory|pointer|address)[A-Za-z0-9_]*range[A-Za-z0-9_]*overlap"
    r"\s*\("
)
RANGE_OVERLAP_HELPER_ALLOWLIST = {
    Path("src/core/db_core.c"),
    Path("src/core/db_core.h"),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, required=True)
    return parser.parse_args()


def function_call_arguments(
    source: str, function_name: str
) -> Iterator[tuple[int, list[str]]]:
    pattern = re.compile(rf"\b{re.escape(function_name)}\s*\(")
    for match in pattern.finditer(source):
        open_index = source.find("(", match.start())
        depth = 1
        cursor = open_index + 1
        argument_start = cursor
        arguments: list[str] = []
        while cursor < len(source) and depth > 0:
            character = source[cursor]
            if character == "(":
                depth += 1
            elif character == ")":
                depth -= 1
                if depth == 0:
                    arguments.append(source[argument_start:cursor])
                    yield match.start(), arguments
                    break
            elif character == "," and depth == 1:
                arguments.append(source[argument_start:cursor])
                argument_start = cursor + 1
            cursor += 1


def scan_file(source_root: Path, path: Path) -> list[tuple[int, str]]:
    relative = path.relative_to(source_root)
    source = strip_comments_and_literals(path.read_text(encoding="utf-8"))
    violations: list[tuple[int, str]] = []
    if relative not in RANGE_OVERLAP_HELPER_ALLOWLIST:
        for match in RANGE_OVERLAP_HELPER_PATTERN.finditer(source):
            violations.append(
                (
                    line_at(source, match.start()),
                    "memory-range overlap classification must use "
                    "db_memory_ranges_overlap",
                )
            )
    for match in FREE_CAST_PATTERN.finditer(source):
        violations.append(
            (
                line_at(source, match.start()),
                "free must receive the owned pointer without a cast",
            )
        )
    for match in DIRECT_REALLOC_PATTERN.finditer(source):
        violations.append(
            (
                line_at(source, match.start()),
                "realloc must use a temporary so failure preserves ownership",
            )
        )
    for function_name in ("memcpy", "memmove", "memcmp", "memset"):
        for offset, arguments in function_call_arguments(source, function_name):
            if len(arguments) != 3:
                continue
            if function_name == "memcmp" and any(
                argument.strip().startswith("&") for argument in arguments[:2]
            ):
                violations.append(
                    (
                        line_at(source, offset),
                        "memcmp must compare explicit byte arrays, not object "
                        "representations",
                    )
                )
            byte_count = arguments[2].strip()
            if "*" in byte_count and not FIXED_SIZE_MULTIPLICATION.fullmatch(
                byte_count
            ):
                violations.append(
                    (
                        line_at(source, offset),
                        f"dynamic {function_name} byte counts must be checked "
                        "before the call",
                    )
                )
    if relative not in MALLOC_MULTIPLICATION_ALLOWLIST:
        for offset, arguments in function_call_arguments(source, "malloc"):
            if len(arguments) == 1 and "*" in arguments[0]:
                violations.append(
                    (
                        line_at(source, offset),
                        "malloc byte multiplication must use a checked helper",
                    )
                )
    for offset, arguments in function_call_arguments(source, "calloc"):
        if len(arguments) == 2 and ARITHMETIC_OPERATOR.search(arguments[0]) is not None:
            violations.append(
                (
                    line_at(source, offset),
                    "calloc element-count arithmetic must use a checked helper",
                )
            )
    return violations


def main() -> int:
    source_root = parse_args().source_root.resolve()
    violations = [
        (path.relative_to(source_root), line, message)
        for path in first_party_sources(source_root)
        for line, message in scan_file(source_root, path)
    ]
    for path, line, message in violations:
        print(f"{path}:{line}: {message}", file=sys.stderr)
    return 1 if violations else 0


if __name__ == "__main__":
    raise SystemExit(main())
