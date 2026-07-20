#!/usr/bin/env python3
"""Reject numeric conversions and extrema that bypass the numeric policy."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Violation:
    path: Path
    line: int
    message: str


CAST_PATTERN = re.compile(r"\((?:float|double)\)\s*(?=[A-Za-z_(])")
FLOAT_FUNCTION_PATTERN = re.compile(
    r"\b(?:atof|fmax|fmaxf|fmaxl|fmin|fminf|fminl|strtof|strtold)\s*\("
)
STRTOD_PATTERN = re.compile(r"\bstrtod\s*\(")
INTEGER_PARSE_PATTERN = re.compile(
    r"\b(?:strtol|strtoul|strtoll|strtoull|strtoimax|strtoumax)\s*\("
)
INTEGER_LITERAL = r"(?:0[xX][0-9A-Fa-f]+|[0-9]+)(?:[uUlL]+)?"
FLOAT_LITERAL = (
    r"(?:(?:[0-9]+\.[0-9]*|\.[0-9]+)"
    r"(?:[eE][+-]?[0-9]+)?|[0-9]+[eE][+-]?[0-9]+)[fFlL]?"
)
ATOM = (
    r"(?:"
    r"[A-Za-z_][A-Za-z0-9_]*"
    r"(?:(?:->|\.)[A-Za-z_][A-Za-z0-9_]*|\[[^\[\]?:;]+\])*"
    rf"|{FLOAT_LITERAL}|{INTEGER_LITERAL}"
    r")"
)
WRAPPED_ATOM = rf"\(*\s*{ATOM}\s*\)*"
EXTREMA_TERNARY_PATTERN = re.compile(
    rf"(?P<lhs>{WRAPPED_ATOM})\s*"
    rf"(?P<operator><=|>=|<|>)\s*"
    rf"(?P<rhs>{WRAPPED_ATOM})\s*\?\s*"
    rf"(?P<true>{WRAPPED_ATOM})\s*:\s*"
    rf"(?P<false>{WRAPPED_ATOM})"
)
NARROWING_TERNARY_PATTERN = re.compile(
    r"\?[\s]*(?:\((?:u?int(?:8|16|32|64)_t|size_t|unsigned|int|long)\))"
    r"[^:;{}]+:[\s]*(?:0[UuLl]*|UINT(?:8|16|32|64)_MAX)"
)
REVERSE_NARROWING_TERNARY_PATTERN = re.compile(
    r"\?[\s]*(?:0[UuLl]*|UINT(?:8|16|32|64)_MAX)[\s]*:"
    r"[\s]*(?:\((?:u?int(?:8|16|32|64)_t|size_t|unsigned|int|long)\))"
)
SATURATING_SUB_TERNARY_PATTERN = re.compile(
    rf"(?P<lhs>{WRAPPED_ATOM})\s*>\s*(?P<rhs>{WRAPPED_ATOM})\s*\?\s*"
    rf"(?P<sub_lhs>{WRAPPED_ATOM})\s*-\s*(?P<sub_rhs>{WRAPPED_ATOM})\s*"
    r":\s*0[UuLl]*"
)
ABS_DIFF_TERNARY_PATTERN = re.compile(
    rf"(?P<lhs>{WRAPPED_ATOM})\s*>\s*(?P<rhs>{WRAPPED_ATOM})\s*\?\s*"
    rf"(?P<true_lhs>{WRAPPED_ATOM})\s*-\s*(?P<true_rhs>{WRAPPED_ATOM})\s*"
    rf":\s*(?P<false_lhs>{WRAPPED_ATOM})\s*-\s*"
    rf"(?P<false_rhs>{WRAPPED_ATOM})"
)
POSITIVE_RECIPROCAL_TERNARY_PATTERN = re.compile(
    rf"(?P<value>{WRAPPED_ATOM})\s*>\s*0(?:\.0*)?[fFlL]?\s*\?\s*"
    rf"\(*\s*1(?:\.0*)?[fFlL]?\s*/\s*(?P<divisor>{WRAPPED_ATOM})\s*\)*\s*:"
)
SATURATING_ADD_TERNARY_PATTERN = re.compile(
    rf"(?:"
    rf"(?P<rhs_first>{WRAPPED_ATOM})\s*>\s*\(*\s*"
    rf"(?P<max_first>UINT(?:32|64)_MAX)\s*-\s*"
    rf"(?P<lhs_first>{WRAPPED_ATOM})\s*\)*"
    rf"|(?P<max_second>UINT(?:32|64)_MAX)\s*-\s*"
    rf"(?P<lhs_second>{WRAPPED_ATOM})\s*<\s*"
    rf"(?P<rhs_second>{WRAPPED_ATOM})"
    rf")\s*\?\s*UINT(?:32|64)_MAX\s*:\s*"
    rf"(?P<add_lhs>{WRAPPED_ATOM})\s*\+\s*(?P<add_rhs>{WRAPPED_ATOM})"
)
ADD_OR_ZERO_TERNARY_PATTERN = re.compile(
    rf"(?P<lhs>{WRAPPED_ATOM})\s*<=\s*\(*\s*SIZE_MAX\s*-\s*"
    rf"(?P<rhs>{WRAPPED_ATOM})\s*\)*\s*\?\s*"
    rf"(?P<add_lhs>{WRAPPED_ATOM})\s*\+\s*(?P<add_rhs>{WRAPPED_ATOM})\s*"
    r":\s*0[UuLl]*"
)
CASTED_EXTREMA_PATTERN = re.compile(
    r"\((?:u?int(?:8|16|32)_t)\)\s*"
    r"DB_(?:MIN|MAX|CLAMP)\s*\("
)
UNCHECKED_ID_INCREMENT_PATTERN = re.compile(
    r"(?:\b(?:[A-Za-z_][A-Za-z0-9_]*_)?"
    r"(?:generation|epoch|revision|sequence|serial)\s*\+\+|"
    r"\+\+\s*\b(?:[A-Za-z_][A-Za-z0-9_]*_)?"
    r"(?:generation|epoch|revision|sequence|serial)\b)"
)
SIGNED_ONE_SHIFT_PATTERN = re.compile(r"(?<![A-Za-z0-9_])1\s*<<")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source-root", type=Path, required=True)
    return parser.parse_args()


def strip_comments_and_literals(source: str) -> str:
    output = list(source)
    index = 0
    state = "code"
    while index < len(source):
        current = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""
        if state == "code":
            if current == "\\" and following == "\n":
                output[index] = " "
                index += 2
                continue
            if current == "/" and following == "/":
                output[index] = " "
                output[index + 1] = " "
                index += 2
                state = "line_comment"
                continue
            if current == "/" and following == "*":
                output[index] = " "
                output[index + 1] = " "
                index += 2
                state = "block_comment"
                continue
            if current == '"':
                output[index] = " "
                index += 1
                state = "string"
                continue
            if current == "'":
                output[index] = " "
                index += 1
                state = "character"
                continue
        elif state == "line_comment":
            if current == "\n":
                state = "code"
            else:
                output[index] = " "
            index += 1
            continue
        elif state == "block_comment":
            if current == "*" and following == "/":
                output[index] = " "
                output[index + 1] = " "
                index += 2
                state = "code"
                continue
            if current != "\n":
                output[index] = " "
            index += 1
            continue
        else:
            if current == "\\" and following:
                output[index] = " "
                if following != "\n":
                    output[index + 1] = " "
                index += 2
                continue
            delimiter = '"' if state == "string" else "'"
            if current == delimiter:
                output[index] = " "
                state = "code"
            elif current != "\n":
                output[index] = " "
            index += 1
            continue
        index += 1
    return "".join(output)


def line_at(source: str, offset: int) -> int:
    return source.count("\n", 0, offset) + 1


def normalized_atom(value: str) -> str:
    normalized = re.sub(r"\s+", "", value)
    return normalized.strip("()")


def is_extrema_ternary(match: re.Match[str]) -> bool:
    lhs = normalized_atom(match.group("lhs"))
    rhs = normalized_atom(match.group("rhs"))
    true_value = normalized_atom(match.group("true"))
    false_value = normalized_atom(match.group("false"))
    return (true_value == lhs and false_value == rhs) or (
        true_value == rhs and false_value == lhs
    )


def scan_file(source_root: Path, path: Path) -> list[Violation]:
    relative = path.relative_to(source_root)
    policy_implementation = relative in {
        Path("src/core/db_core.h"),
        Path("src/core/db_numeric.h"),
    }
    if relative == Path("src/core/db_numeric.h"):
        return []

    source = strip_comments_and_literals(path.read_text(encoding="utf-8"))
    violations: list[Violation] = []
    for match in CAST_PATTERN.finditer(source):
        violations.append(
            Violation(
                relative,
                line_at(source, match.start()),
                "scalar-to-float conversion must use DB_TO_F64 or a canonical "
                "narrowing helper",
            )
        )
    for match in FLOAT_FUNCTION_PATTERN.finditer(source):
        violations.append(
            Violation(
                relative,
                line_at(source, match.start()),
                "floating conversion/extrema must use the numeric policy helpers",
            )
        )
    if relative != Path("src/core/db_core.c"):
        for match in STRTOD_PATTERN.finditer(source):
            violations.append(
                Violation(
                    relative,
                    line_at(source, match.start()),
                    "text-to-f64 conversion must use db_parse_double_prefix",
                )
            )
        for match in INTEGER_PARSE_PATTERN.finditer(source):
            violations.append(
                Violation(
                    relative,
                    line_at(source, match.start()),
                    "text-to-integer conversion must use a db_parse_*_prefix helper",
                )
            )
    for match in EXTREMA_TERNARY_PATTERN.finditer(source):
        if is_extrema_ternary(match):
            violations.append(
                Violation(
                    relative,
                    line_at(source, match.start()),
                    "inline min/max ternary must use DB_MIN, DB_MAX, "
                    "db_min_f64, or db_max_f64",
                )
            )
    if not policy_implementation:
        for match in NARROWING_TERNARY_PATTERN.finditer(source):
            violations.append(
                Violation(
                    relative,
                    line_at(source, match.start()),
                    "conditional integral narrowing must use a named checked, "
                    "saturating, or fallback conversion helper",
                )
            )
        for match in REVERSE_NARROWING_TERNARY_PATTERN.finditer(source):
            violations.append(
                Violation(
                    relative,
                    line_at(source, match.start()),
                    "conditional integral narrowing must use a named checked, "
                    "saturating, or fallback conversion helper",
                )
            )
        for match in SATURATING_SUB_TERNARY_PATTERN.finditer(source):
            if normalized_atom(match.group("lhs")) == normalized_atom(
                match.group("sub_lhs")
            ) and normalized_atom(match.group("rhs")) == normalized_atom(
                match.group("sub_rhs")
            ):
                violations.append(
                    Violation(
                        relative,
                        line_at(source, match.start()),
                        "inline saturating subtraction must use a named "
                        "numeric policy helper",
                    )
                )
        for match in ABS_DIFF_TERNARY_PATTERN.finditer(source):
            if normalized_atom(match.group("lhs")) == normalized_atom(
                match.group("true_lhs")
            ) == normalized_atom(match.group("false_rhs")) and normalized_atom(
                match.group("rhs")
            ) == normalized_atom(match.group("true_rhs")) == normalized_atom(
                match.group("false_lhs")
            ):
                violations.append(
                    Violation(
                        relative,
                        line_at(source, match.start()),
                        "inline absolute difference must use a named numeric "
                        "policy helper",
                    )
                )
        for match in POSITIVE_RECIPROCAL_TERNARY_PATTERN.finditer(source):
            if normalized_atom(match.group("value")) == normalized_atom(
                match.group("divisor")
            ):
                violations.append(
                    Violation(
                        relative,
                        line_at(source, match.start()),
                        "conditional reciprocal must use a named finite "
                        "numeric policy helper",
                    )
                )
        for match in SATURATING_ADD_TERNARY_PATTERN.finditer(source):
            lhs = match.group("lhs_first") or match.group("lhs_second")
            rhs = match.group("rhs_first") or match.group("rhs_second")
            add_terms = {
                normalized_atom(match.group("add_lhs")),
                normalized_atom(match.group("add_rhs")),
            }
            if add_terms == {normalized_atom(lhs), normalized_atom(rhs)}:
                violations.append(
                    Violation(
                        relative,
                        line_at(source, match.start()),
                        "inline saturating addition must use a named numeric "
                        "policy helper",
                    )
                )
        for match in ADD_OR_ZERO_TERNARY_PATTERN.finditer(source):
            add_terms = {
                normalized_atom(match.group("add_lhs")),
                normalized_atom(match.group("add_rhs")),
            }
            if add_terms == {
                normalized_atom(match.group("lhs")),
                normalized_atom(match.group("rhs")),
            }:
                violations.append(
                    Violation(
                        relative,
                        line_at(source, match.start()),
                        "checked addition fallback must use a named numeric "
                        "policy helper",
                    )
                )
        for match in CASTED_EXTREMA_PATTERN.finditer(source):
            violations.append(
                Violation(
                    relative,
                    line_at(source, match.start()),
                    "narrowing an extrema macro result must use a named "
                    "checked conversion helper",
                )
            )
    if relative.parts[0] == "src":
        for match in SIGNED_ONE_SHIFT_PATTERN.finditer(source):
            violations.append(
                Violation(
                    relative,
                    line_at(source, match.start()),
                    "bit masks must shift an explicitly unsigned value",
                )
            )
        for match in UNCHECKED_ID_INCREMENT_PATTERN.finditer(source):
            violations.append(
                Violation(
                    relative,
                    line_at(source, match.start()),
                    "identity increments must use checked arithmetic or an "
                    "explicit wrapping helper",
                )
            )
    return violations


def first_party_sources(source_root: Path) -> list[Path]:
    paths: list[Path] = []
    for directory_name in ("src", "tests"):
        directory = source_root / directory_name
        if directory.is_dir():
            paths.extend(directory.rglob("*.c"))
            paths.extend(directory.rglob("*.h"))
    return sorted(paths)


def main() -> int:
    source_root = parse_args().source_root.resolve()
    violations = [
        violation
        for path in first_party_sources(source_root)
        for violation in scan_file(source_root, path)
    ]
    for violation in violations:
        print(
            f"{violation.path}:{violation.line}: {violation.message}",
            file=sys.stderr,
        )
    return 1 if violations else 0


if __name__ == "__main__":
    raise SystemExit(main())
