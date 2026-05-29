#!/usr/bin/env python3
"""Generate packed C++ glyph tables from a text pixel-art source.

Expected block format:

    [A large 10x14]
    ..#####...
    ...

    [A small 6x10]
    .####.
    ...

Rows use '#' for enabled pixels and '.' for disabled pixels. Labels are
case-insensitive except for the glyph character itself. The generated table is
written into src/core/renderer.cpp by replacing constexpr Glyph kGlyphs[].
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path


BLOCK_RE = re.compile(r"^\[(\S+)\s+(large|small)\s+(\d+)x(\d+)\]$", re.IGNORECASE)
TABLE_RE = re.compile(r"constexpr Glyph kGlyphs\[\] = \{.*?\n\};", re.DOTALL)


def decode_label(label: str) -> str:
    names = {
        "space": " ",
        "apostrophe": "'",
        "quote": '"',
        "backslash": "\\",
        "lbracket": "[",
        "rbracket": "]",
    }
    return names.get(label.lower(), label)


def parse_glyphs(path: Path) -> dict[str, dict[str, list[str]]]:
    glyphs: dict[str, dict[str, list[str]]] = {}
    current: tuple[str, str, int, int] | None = None
    rows: list[str] = []

    def flush() -> None:
        nonlocal current, rows
        if current is None:
            return
        ch, size_name, width, height = current
        if len(rows) != height:
            raise SystemExit(f"{path}: [{ch} {size_name}] has {len(rows)} rows, expected {height}")
        for idx, row in enumerate(rows, start=1):
            if len(row) != width:
                raise SystemExit(f"{path}: [{ch} {size_name}] row {idx} has width {len(row)}, expected {width}")
            invalid = set(row) - {".", "#"}
            if invalid:
                raise SystemExit(f"{path}: [{ch} {size_name}] row {idx} has invalid characters: {sorted(invalid)}")
        glyphs.setdefault(ch, {})[size_name] = rows
        current = None
        rows = []

    for line_no, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()
        if not line:
            continue
        match = BLOCK_RE.match(line)
        if match:
            flush()
            ch = decode_label(match.group(1))
            if len(ch) != 1:
                raise SystemExit(f"{path}:{line_no}: glyph label must resolve to one character: {match.group(1)}")
            size_name = match.group(2).lower()
            width = int(match.group(3))
            height = int(match.group(4))
            if size_name == "large" and (width, height) != (10, 14):
                raise SystemExit(f"{path}:{line_no}: large glyph must be 10x14")
            if size_name == "small" and (width, height) != (6, 10):
                raise SystemExit(f"{path}:{line_no}: small glyph must be 6x10")
            current = (ch, size_name, width, height)
            continue
        if current is None:
            raise SystemExit(f"{path}:{line_no}: pixel row appears before any block header")
        rows.append(line)

    flush()

    for ch, sizes in glyphs.items():
        missing = {"large", "small"} - set(sizes)
        if missing:
            raise SystemExit(f"{path}: glyph {ch!r} missing {', '.join(sorted(missing))} block")
    return glyphs


def pack_rows(rows: list[str]) -> list[int]:
    width = len(rows[0])
    packed: list[int] = []
    for row in rows:
        value = 0
        for col, pixel in enumerate(row):
            if pixel == "#":
                value |= 1 << (width - 1 - col)
        packed.append(value)
    return packed


def char_literal(ch: str) -> str:
    if ch == "'":
        return "'\\''"
    if ch == "\\":
        return "'\\\\'"
    return repr(ch)


def render_table(glyphs: dict[str, dict[str, list[str]]]) -> str:
    lines = ["constexpr Glyph kGlyphs[] = {"]
    for ch in sorted(glyphs):
        large = ", ".join(f"0x{value:03x}" for value in pack_rows(glyphs[ch]["large"]))
        small = ", ".join(f"0x{value:02x}" for value in pack_rows(glyphs[ch]["small"]))
        lines.append(f"    {{{char_literal(ch)}, {{{large}}}, {{{small}}}}},")
    lines.append("};")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("glyphs", type=Path, help="Input glyph text file")
    parser.add_argument("--renderer", type=Path, default=Path("src/core/renderer.cpp"))
    args = parser.parse_args()

    glyphs = parse_glyphs(args.glyphs)
    table = render_table(glyphs)
    renderer = args.renderer.read_text(encoding="utf-8")
    updated, count = TABLE_RE.subn(table, renderer, count=1)
    if count != 1:
        raise SystemExit(f"{args.renderer}: could not find exactly one kGlyphs table")
    args.renderer.write_text(updated, encoding="utf-8")
    print(f"Generated {len(glyphs)} glyphs into {args.renderer}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
