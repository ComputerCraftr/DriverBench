#!/usr/bin/env python3
"""Update canonical hashes only after the complete scenario matrix agrees."""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys


BENCHMARKS = (
    "bands",
    "gradient_fill",
    "gradient_sweep",
    "snake_grid",
    "snake_rect",
    "snake_shapes",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True)
    parser.add_argument("--manifest", required=True)
    parser.add_argument("--runner", required=True)
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--glfw-enabled", default="ON")
    parser.add_argument("--vulkan-enabled", default="ON")
    return parser.parse_args()


def capture(args: argparse.Namespace, benchmark: str) -> dict[str, str]:
    command = [
        args.cmake,
        f"-DTEST_BIN={args.binary}",
        f"-DTEST_BENCHMARK={benchmark}",
        f"-DTEST_GLFW_ENABLED={args.glfw_enabled}",
        f"-DTEST_VULKAN_ENABLED={args.vulkan_enabled}",
        "-DTEST_CAPTURE_GOLDEN=ON",
        "-P",
        args.runner,
    ]
    result = subprocess.run(command, text=True, capture_output=True, check=False)
    output = result.stdout + result.stderr
    if result.returncode != 0:
        raise RuntimeError(
            f"matrix disagreement for {benchmark} (status={result.returncode})\n{output}"
        )
    match = re.search(
        rf"canonical_golden benchmark={re.escape(benchmark)} "
        r"low_state=(0x[0-9a-f]+) low_framebuffer=(0x[0-9a-f]+) "
        r"long_state=(0x[0-9a-f]+) long_framebuffer=(0x[0-9a-f]+)",
        output,
    )
    if match is None:
        raise RuntimeError(f"missing canonical capture for {benchmark}\n{output}")
    return dict(
        zip(
            ("LOW_STATE", "LOW_FRAMEBUFFER", "LONG_STATE", "LONG_FRAMEBUFFER"),
            match.groups(),
            strict=True,
        )
    )


def replace_hash(text: str, benchmark: str, key: str, value: str) -> str:
    pattern = re.compile(
        rf"^(set\(DB_CANONICAL_GOLDEN_{re.escape(benchmark)}_{key} )"
        r"0x[0-9a-fA-F]+(\))$",
        re.MULTILINE,
    )
    updated, count = pattern.subn(rf"\g<1>{value}\g<2>", text)
    if count != 1:
        raise RuntimeError(f"expected one manifest field for {benchmark}.{key}")
    return updated


def main() -> int:
    args = parse_args()
    manifest = pathlib.Path(args.manifest)
    text = manifest.read_text(encoding="utf-8")
    captures = {benchmark: capture(args, benchmark) for benchmark in BENCHMARKS}
    for benchmark, values in captures.items():
        for key, value in values.items():
            text = replace_hash(text, benchmark, key, value)
    manifest.write_text(text, encoding="utf-8")
    print(f"updated verified canonical goldens: {manifest}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(error, file=sys.stderr)
        raise SystemExit(1) from error
