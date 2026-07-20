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
    entries: list[dict[str, object]] = [
        {"file": "/repo/src/core/db_core.c", "arguments": ["clang", "-c"]},
        {
            "file": "/repo/src/renderers/opengl_gl3_3/gl3_execute.c",
            "arguments": ["clang", "-DGL3", "-c"],
        },
    ]

    core_header = source_root / "src/core/db_core.h"
    gl3_header = source_root / "src/renderers/opengl_gl3_3/gl3_internal.h"
    kms_header = source_root / "src/displays/linux_kms_atomic/kms_internal.h"
    test_header = source_root / "tests/support/test_harness.h"

    assert module.optional_component(source_root, core_header) is None
    assert module.optional_component(source_root, test_header) is None
    assert module.optional_component(source_root, gl3_header) == (
        source_root / "src/renderers/opengl_gl3_3"
    )
    assert len(module.entries_for_header(source_root, core_header, entries)) == 2
    assert len(module.entries_for_header(source_root, test_header, entries)) == 2
    assert len(module.entries_for_header(source_root, gl3_header, entries)) == 1
    assert not module.entries_for_header(source_root, kms_header, entries)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
