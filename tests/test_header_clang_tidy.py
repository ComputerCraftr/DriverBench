#!/usr/bin/env python3
"""Regression tests for target-aware primary-header clang-tidy selection."""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path


def load_module(path: Path):  # type: ignore[no-untyped-def]
    spec = importlib.util.spec_from_file_location("run_header_clang_tidy", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    source_root = Path("/repo")
    module = load_module(Path(sys.argv[1]))
    source_directory = Path(module.SOURCE_DIRECTORY)
    test_directory = Path(module.TEST_DIRECTORY)
    core_header = source_directory / "core/db_core.h"
    gl3_header = source_directory / "renderers/opengl_gl3_3/gl3_internal.h"
    kms_header = source_directory / "displays/linux_kms_atomic/kms_internal.h"
    test_header = test_directory / "support/test_harness.h"

    def absolute(path: Path) -> Path:
        return source_root / path

    entries: list[dict[str, object]] = [
        {
            "file": str(absolute(core_header.with_suffix(".c"))),
            "arguments": ["clang", "-c"],
        },
        {
            "file": str(absolute(gl3_header.with_name("gl3_execute.c"))),
            "arguments": ["clang", "-DGL3", "-c"],
        },
    ]

    cases = (
        (core_header, None, 2),
        (test_header, None, 2),
        (gl3_header, absolute(gl3_header.parent), 1),
        (kms_header, absolute(kms_header.parent), 0),
    )
    for relative_header, expected_component, expected_entry_count in cases:
        header = absolute(relative_header)
        assert module.optional_component(source_root, header) == expected_component
        assert (
            len(module.entries_for_header(source_root, header, entries))
            == expected_entry_count
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
