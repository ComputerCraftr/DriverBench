#!/usr/bin/env python3
"""Regression fixtures for the source-level C memory policy."""

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
void clean(void *destination, const void *source, size_t checked_bytes) {
    void *resized = realloc(destination, checked_bytes);
    if (resized != NULL) {
        memcpy(resized, source, checked_bytes);
    }
    free(resized != NULL ? resized : destination);
}
""",
        )
        clean = run_checker(checker, root)
        require(clean.returncode == 0, clean.stderr)

    cases = {
        "cast_free": "void bad(void *p) { free((void *)p); }\n",
        "direct_realloc": (
            "void bad(struct state *s, size_t n) {\n"
            "    s->pixels = realloc(s->pixels, n);\n"
            "}\n"
        ),
        "dynamic_memcpy": (
            "void bad(void *d, const void *s, size_t n) {\n"
            "    memcpy(d, s, n * sizeof(unsigned));\n"
            "}\n"
        ),
        "dynamic_memmove": (
            "void bad(void *d, const void *s, size_t n) {\n"
            "    memmove(d, s, n * sizeof(unsigned));\n"
            "}\n"
        ),
        "dynamic_memcmp": (
            "int bad(const void *a, const void *b, size_t n) {\n"
            "    return memcmp(a, b, n * sizeof(unsigned));\n"
            "}\n"
        ),
        "dynamic_memset": (
            "void bad(void *d, size_t n) {\n"
            "    memset(d, 0, n * sizeof(unsigned));\n"
            "}\n"
        ),
        "unchecked_malloc": (
            "void *bad(size_t n) { return malloc(n * sizeof(unsigned)); }\n"
        ),
        "unchecked_calloc_count": (
            "void *bad(size_t n) { return calloc(n + 1U, sizeof(unsigned)); }\n"
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
