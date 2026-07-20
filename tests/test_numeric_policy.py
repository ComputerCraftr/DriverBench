#!/usr/bin/env python3
"""Regression fixtures for the source-level numeric boundary policy."""

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
        [sys.executable, str(checker), "--source-root", str(root)],
        check=False,
        capture_output=True,
        text=True,
    )


def write_fixture(root: Path, source: str) -> None:
    (root / "src/core").mkdir(parents=True)
    (root / "src/core/db_numeric.h").write_text(
        "static inline double numeric_cast(float value) { return (double)value; }\n",
        encoding="ascii",
    )
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
size_t bounded(size_t value, size_t limit) {
    return DB_MIN(value, limit);
}
unsigned saturating(unsigned lhs, unsigned rhs) {
    return db_u32_saturating_sub(lhs, rhs);
}
const char *select_text(const char *text) {
    return text != NULL ? text : "none";
}
double widen(float value) {
    return DB_TO_F64(value);
}
""",
        )
        clean = run_checker(checker, root)
        require(clean.returncode == 0, clean.stderr)

    cases = {
        "float_cast": "float bad(double value) { return (float)value; }\n",
        "double_cast": "double bad(float value) { return (double)value; }\n",
        "float_extrema": "float bad(float a, float b) { return fminf(a, b); }\n",
        "float_parse": "float bad(const char *s) { return strtof(s, 0); }\n",
        "integer_parse": (
            "unsigned long long bad(const char *s) { return strtoull(s, 0, 16); }\n"
        ),
        "inline_min": (
            "unsigned bad(unsigned a, unsigned b) {\n    return (a < b) ? a : b;\n}\n"
        ),
        "inline_f64_nonnegative": (
            "double bad(double value) {\n    return (value > 0.0) ? value : 0.0;\n}\n"
        ),
        "inline_f32_upper_bound": (
            "float bad(float value) {\n    return (value < 1.0F) ? value : 1.0F;\n}\n"
        ),
        "multiline_max": (
            "#define BAD_MAX(lhs, rhs) \\\n"
            "    ((lhs) > (rhs)) ? \\\n"
            "        (lhs) : (rhs)\n"
        ),
        "conditional_narrow": (
            "unsigned bad(int raw_age) {\n"
            "    return (raw_age > 0) ? (uint32_t)raw_age : 0U;\n"
            "}\n"
        ),
        "conditional_saturate": (
            "uint32_t bad(uint64_t value) {\n"
            "    return value <= UINT32_MAX ? (uint32_t)value : UINT32_MAX;\n"
            "}\n"
        ),
        "conditional_narrow_reverse": (
            "unsigned bad(int raw_age) {\n"
            "    return (raw_age <= 0) ? 0U : (uint32_t)raw_age;\n"
            "}\n"
        ),
        "conditional_size_narrow": (
            "size_t bad(int32_t height) {\n"
            "    return (height > 0) ? (size_t)height : 0U;\n"
            "}\n"
        ),
        "signed_bit_shift": (
            "uint32_t bad(uint32_t index) {\n    return 1 << index;\n}\n"
        ),
        "saturating_subtract": (
            "uint64_t bad(uint64_t lhs, uint64_t rhs) {\n"
            "    return lhs > rhs ? lhs - rhs : 0U;\n"
            "}\n"
        ),
        "absolute_difference": (
            "uint64_t bad(uint64_t lhs, uint64_t rhs) {\n"
            "    return lhs > rhs ? lhs - rhs : rhs - lhs;\n"
            "}\n"
        ),
        "conditional_reciprocal": (
            "double bad(double value) {\n"
            "    return value > 0.0 ? (1.0 / value) : 0.0;\n"
            "}\n"
        ),
        "saturating_add": (
            "uint64_t bad(uint64_t lhs, uint64_t rhs) {\n"
            "    return rhs > (UINT64_MAX - lhs) ? UINT64_MAX : lhs + rhs;\n"
            "}\n"
        ),
        "checked_add_or_zero": (
            "size_t bad(size_t lhs, size_t rhs) {\n"
            "    return lhs <= (SIZE_MAX - rhs) ? lhs + rhs : 0U;\n"
            "}\n"
        ),
        "casted_extrema": (
            "uint32_t bad(uint64_t lhs, uint64_t rhs) {\n"
            "    return (uint32_t)DB_MIN(lhs, rhs);\n"
            "}\n"
        ),
        "unchecked_generation_increment": (
            "void bad(struct state *state) { state->generation++; }\n"
        ),
        "unchecked_serial_increment": (
            "void bad(struct state *state) { state->present_serial++; }\n"
        ),
    }
    for name, source in cases.items():
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_fixture(root, source)
            result = run_checker(checker, root)
            require(result.returncode != 0, f"{name} bypass was accepted")
            require("src/fixture.c:" in result.stderr, result.stderr)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(error, file=sys.stderr)
        raise SystemExit(1) from error
