#!/usr/bin/env python3
"""Build and execute the local Ubuntu 26.04 quality container."""

from __future__ import annotations

import argparse
import os
import subprocess
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "mode", choices=("format", "tidy", "all"), nargs="?", default="all"
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parents[1]
    image = os.environ.get(
        "DB_UBUNTU2604_QUALITY_IMAGE", "driverbench-ubuntu2604-quality"
    )
    platform = "linux/amd64"
    network = os.environ.get("DB_DOCKER_BUILD_NETWORK", "default")
    build = ["docker", "build"]
    if network != "default":
        build.extend(["--network", network])
    build.extend(
        [
            "--platform",
            platform,
            "--file",
            "ci/ubuntu2604/Dockerfile",
            "--tag",
            image,
            ".",
        ]
    )
    subprocess.run(build, cwd=root, check=True)
    subprocess.run(
        [
            "docker",
            "run",
            "--rm",
            "--platform",
            platform,
            "--user",
            f"{os.getuid()}:{os.getgid()}",
            "--env",
            "HOME=/tmp",
            "--env",
            "CC=clang-22",
            "--volume",
            f"{root}:/workspace",
            "--workdir",
            "/workspace",
            image,
            "python3",
            "scripts/run_quality_ci.py",
            args.mode,
        ],
        cwd=root,
        check=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
