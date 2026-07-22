#!/usr/bin/env python3
"""Build and execute the local Void i686 sanitizer container."""

from __future__ import annotations

import os
import subprocess
from pathlib import Path


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    image = os.environ.get("DB_VOID_I686_IMAGE", "driverbench-void-i686")
    platform = "linux/386"
    network = os.environ.get("DB_DOCKER_BUILD_NETWORK", "default")
    build = ["docker", "build"]
    if network != "default":
        build.extend(["--network", network])
    build.extend(
        [
            "--platform",
            platform,
            "--file",
            "ci/void-i686/Dockerfile",
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
            "--volume",
            f"{root}:/workspace",
            "--workdir",
            "/workspace",
            image,
            "python3",
            "scripts/run_void_i686_inner.py",
        ],
        cwd=root,
        check=True,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
