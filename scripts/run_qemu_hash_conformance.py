#!/usr/bin/env python3

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

SKIP_RETURN_CODE = 77


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--arch", choices=("aarch64", "i686"), required=True)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--build-root", type=Path, required=True)
    return parser.parse_args()


def tool_or_skip(name: str) -> str:
    path = shutil.which(name)
    if path is None:
        print(f"SKIP: required tool is unavailable: {name}")
        raise SystemExit(SKIP_RETURN_CODE)
    return path


def compiler_probe(compiler: str, flags: list[str], build_root: Path) -> None:
    probe_source = build_root / "probe.c"
    probe_binary = build_root / "probe"
    probe_source.write_text("int main(void) { return 0; }\n", encoding="ascii")
    result = subprocess.run(
        [compiler, *flags, str(probe_source), "-o", str(probe_binary)],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print("SKIP: cross runtime is unavailable")
        print(result.stderr.strip())
        raise SystemExit(SKIP_RETURN_CODE)


def main() -> int:
    args = parse_args()
    args.build_root.mkdir(parents=True, exist_ok=True)
    source_root = args.source_root.resolve()
    output = args.build_root / f"hash-conformance-{args.arch}"
    if args.arch == "aarch64":
        compiler = tool_or_skip("aarch64-linux-gnu-gcc")
        emulator = tool_or_skip("qemu-aarch64")
        compiler_flags: list[str] = []
        emulator_args = ["-L", "/usr/aarch64-linux-gnu", "-cpu", "cortex-a53"]
        expected_kernel = "neon"
        architecture_sources: list[Path] = []
    else:
        compiler = tool_or_skip("clang")
        emulator = tool_or_skip("qemu-i386")
        sysroot = Path("/usr/i686-pc-linux-gnu")
        if not sysroot.is_dir():
            print(f"SKIP: i686 sysroot is unavailable: {sysroot}")
            return SKIP_RETURN_CODE
        compiler_flags = [
            "--target=i686-pc-linux-gnu",
            f"--sysroot={sysroot}",
            "--gcc-toolchain=/usr",
            "-msse2",
            "-mno-sse3",
            "-mno-avx",
        ]
        emulator_args = [
            "-L",
            str(sysroot),
            "-cpu",
            "qemu32,-sse3,-ssse3,-sse4.1,-sse4.2,-avx,-avx2",
        ]
        expected_kernel = "sse2"
        architecture_sources = [
            source_root / "src/core/db_hash_simd_x86.c",
        ]
    compiler_probe(compiler, compiler_flags, args.build_root)
    sources = [
        source_root / "tests/hash_conformance.c",
        source_root / "tests/hash_conformance_support.c",
        source_root / "src/core/db_hash_simd.c",
        *architecture_sources,
    ]
    compile_command = [
        compiler,
        *compiler_flags,
        "-std=c23",
        "-O2",
        "-Wall",
        "-Wextra",
        "-Werror",
        f"-I{source_root / 'src'}",
        *(str(source) for source in sources),
        "-o",
        str(output),
    ]
    subprocess.run(compile_command, check=True)
    subprocess.run([emulator, *emulator_args, str(output), expected_kernel], check=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
