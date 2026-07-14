#!/usr/bin/env python3
"""Install the pinned Python developer tools through pipx."""

from __future__ import annotations

import subprocess
from pathlib import Path


def load_versions(path: Path) -> dict[str, str]:
    versions: dict[str, str] = {}
    for line in path.read_text(encoding="ascii").splitlines():
        key, separator, value = line.partition("=")
        if separator != "=" or not key or not value:
            raise ValueError(f"invalid tool version record: {line}")
        versions[key] = value
    return versions


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    versions = load_versions(root / "ci/python-tool-versions.env")
    packages = {
        "cmakelang": versions["DB_CMAKELANG_VERSION"],
        "mypy": versions["DB_MYPY_VERSION"],
        "ruff": versions["DB_RUFF_VERSION"],
    }
    for package, version in packages.items():
        subprocess.run(
            ["pipx", "install", "--force", f"{package}=={version}"], check=True
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
