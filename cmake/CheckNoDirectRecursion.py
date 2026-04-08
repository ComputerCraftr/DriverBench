#!/usr/bin/env python3

from __future__ import annotations

import pathlib
import re
import sys


CONTROL_KEYWORDS = {"if", "for", "while", "switch"}
FUNCTION_DEF_RE = re.compile(
    r"(?ms)(^|[;}\n])\s*"
    r"(?:[A-Za-z_][A-Za-z0-9_\s\*\(\)]*?\s+)?"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)\s*"
    r"\((?P<params>[^;{}]*)\)\s*\{"
)


def sanitize_c_source(text: str) -> str:
    out: list[str] = []
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if ch == "/" and nxt == "/":
            out.append("  ")
            i += 2
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
            continue
        if ch == "/" and nxt == "*":
            out.append("  ")
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            if i + 1 < n:
                out.append("  ")
                i += 2
            continue
        if ch == '"':
            out.append(" ")
            i += 1
            while i < n:
                if text[i] == "\\" and i + 1 < n:
                    out.append("  ")
                    i += 2
                    continue
                if text[i] == '"':
                    out.append(" ")
                    i += 1
                    break
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            continue
        if ch == "'":
            out.append(" ")
            i += 1
            while i < n:
                if text[i] == "\\" and i + 1 < n:
                    out.append("  ")
                    i += 2
                    continue
                if text[i] == "'":
                    out.append(" ")
                    i += 1
                    break
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            continue
        out.append(ch)
        i += 1
    return "".join(out)


def find_matching_brace(text: str, open_index: int) -> int:
    depth = 0
    for index in range(open_index, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return -1


def detect_direct_recursion(path: pathlib.Path) -> list[tuple[int, str]]:
    original = path.read_text(encoding="utf-8")
    text = sanitize_c_source(original)
    violations: list[tuple[int, str]] = []
    for match in FUNCTION_DEF_RE.finditer(text):
        name = match.group("name")
        if name in CONTROL_KEYWORDS:
            continue
        body_open = match.end() - 1
        body_close = find_matching_brace(text, body_open)
        if body_close < 0:
            continue
        body = text[body_open + 1 : body_close]
        if re.search(rf"\b{name}\s*\(", body) is None:
            continue
        line = text.count("\n", 0, match.start("name")) + 1
        violations.append((line, name))
    return violations


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: CheckNoDirectRecursion.py <source-root>", file=sys.stderr)
        return 2
    source_root = pathlib.Path(sys.argv[1]).resolve()
    source_dir = source_root / "src"
    violations: list[str] = []
    for path in sorted(source_dir.rglob("*")):
        if path.suffix not in {".c", ".h"}:
            continue
        for line, name in detect_direct_recursion(path):
            rel = path.relative_to(source_root)
            violations.append(f"{rel}:{line}: direct recursion in {name}()")
    if violations:
        print(
            "Direct recursion is forbidden:\n" + "\n".join(violations),
            file=sys.stderr,
        )
        return 1
    print("No direct recursion found in scoped source files.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
