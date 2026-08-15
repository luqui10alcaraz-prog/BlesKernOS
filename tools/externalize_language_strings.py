#!/usr/bin/env python3
"""Replace user-visible C literals with stable @Hxxxxxxxx language keys."""
from __future__ import annotations

import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("catalog", ROOT / "tools" / "build_language_catalogs.py")
catalog = importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(catalog)


def tokens(text: str):
    result = []
    i, n, state = 0, len(text), "code"
    while i < n:
        c = text[i]
        if state == "code":
            if c == "/" and i + 1 < n and text[i + 1] == "/": state, i = "line", i + 2; continue
            if c == "/" and i + 1 < n and text[i + 1] == "*": state, i = "block", i + 2; continue
            if c == "'": state, i = "char", i + 1; continue
            if c == '"':
                start = i; i += 1; out = []
                while i < n:
                    c = text[i]
                    if c == '"': i += 1; break
                    if c == "\\" and i + 1 < n:
                        esc = text[i + 1]
                        out.append({"n":"\n", "r":"\r", "t":"\t", "0":"\0", '"':'"', "\\":"\\"}.get(esc, esc))
                        i += 2; continue
                    out.append(c); i += 1
                result.append((start, i, "".join(out))); continue
            i += 1
        elif state == "line":
            if c == "\n": state = "code"
            i += 1
        elif state == "block":
            if c == "*" and i + 1 < n and text[i + 1] == "/": state, i = "code", i + 2
            else: i += 1
        else:
            if c == "\\" and i + 1 < n: i += 2
            elif c == "'": state, i = "code", i + 1
            else: i += 1
    return result


def only_spacing(segment: str) -> bool:
    i, n = 0, len(segment)
    while i < n:
        if segment[i].isspace(): i += 1; continue
        if segment.startswith("//", i):
            end = segment.find("\n", i + 2)
            if end < 0: return True
            i = end + 1; continue
        if segment.startswith("/*", i):
            end = segment.find("*/", i + 2)
            if end < 0: return False
            i = end + 2; continue
        return False
    return True



def assembly_literal(text: str, start: int) -> bool:
    """Return True when a string is the template of inline assembly."""
    line_start = text.rfind("\n", 0, start) + 1
    prefix = text[line_start:start]
    compact = "".join(prefix.split())
    return ("__asm__(" in compact or "__asm__volatile(" in compact or
            "__asmvolatile(" in compact or "asm(" in compact or
            "asmvolatile(" in compact)

def rewrite(path: Path) -> int:
    text = path.read_text(encoding="utf-8", errors="ignore")
    raw_tokens = tokens(text)
    groups = []
    index = 0
    while index < len(raw_tokens):
        start, end, value = raw_tokens[index]
        last = index
        while last + 1 < len(raw_tokens) and only_spacing(text[end:raw_tokens[last + 1][0]]):
            last += 1
            end = raw_tokens[last][1]
            value += raw_tokens[last][2]
        groups.append((start, end, value))
        index = last + 1

    replacements = []
    for start, end, value in groups:
        if assembly_literal(text, start): continue
        if value.startswith("@") or value[:1].isspace() or value[-1:].isspace(): continue
        if not catalog.visible_candidate(value): continue
        replacements.append((start, end, f'"@H{catalog.fnv1a(value):08X}"'))
    if not replacements: return 0
    for start, end, replacement in reversed(replacements):
        text = text[:start] + replacement + text[end:]
    path.write_text(text, encoding="utf-8")
    return len(replacements)


def main() -> None:
    files = []
    for pattern in catalog.SOURCE_GLOBS: files.extend(ROOT.glob(pattern))
    files.extend(ROOT / item for item in catalog.SOURCE_FILES)
    total = 0
    for path in sorted(set(files)):
        if not path.exists(): continue
        count = rewrite(path)
        if count:
            print(f"{path.relative_to(ROOT)}: {count}")
            total += count
    print(f"Externalized {total} source literals")

if __name__ == "__main__": main()
