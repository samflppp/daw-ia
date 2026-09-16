#!/usr/bin/env python3
"""Static checks for the three hygiene rules on core/ C++ sources.

Rule 1: no hard-coded visual values. Colours, sizes and font sizes come from
        core/ui/tokens/tokens.json through daw::ui::Tokens.
Rule 2: panels never know their position or size. Only layout hosts
        (files named *View.cpp, *Layout*.cpp, or under core/ui/layout/) call
        setBounds and friends. Windows are allowed to size themselves from tokens.
Rule 3: core/domain includes nothing from JUCE, Tracktion or ui.

Standard library only. Exit code 1 on any violation.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CORE = ROOT / "core"
CPP_SUFFIXES = {".h", ".hpp", ".cpp"}

# Rule 1: literal visual values.
VISUAL_PATTERNS = [
    (re.compile(r"\bColours::\w+"), "named JUCE colour"),
    (re.compile(r"\bColour\s*\(\s*0x[0-9a-fA-F]+"), "hex colour literal"),
    (re.compile(r"\bColour\s*\(\s*\d+\s*,"), "RGB colour literal"),
    (re.compile(r"\bColour::from(RGB|RGBA|HSV|FloatRGBA)\s*\(\s*[\d.]"), "colour literal"),
    (re.compile(r"\bColour::fromString\s*\(\s*\""), "colour string literal"),
    (re.compile(r"\b(FontOptions|Font|withHeight|setFont)\s*\(\s*\d"), "font size literal"),
    (re.compile(r"\b(setSize|centreWithSize|setResizeLimits)\s*\(\s*\d"), "size literal"),
    (re.compile(r"\b(reduced|expanded|withTrimmed\w+|removeFrom\w+)\s*\(\s*\d"), "spacing literal"),
    (re.compile(r"\b(fillRoundedRectangle|drawRoundedRectangle)\s*\([^;]*,\s*\d+(\.\d+)?f?\s*\)"),
     "radius literal"),
]

# Rule 2: geometry calls reserved to layout hosts.
GEOMETRY_CALL = re.compile(r"\b(setBounds|setTopLeftPosition|setCentrePosition|setBoundsRelative)\s*\(")

# Rule 3: forbidden includes in the domain.
DOMAIN_FORBIDDEN_INCLUDE = re.compile(r'#\s*include\s*[<"](juce_|tracktion_|daw/ui/|clap/|pluginterfaces/)')

# The token loader is the one place allowed to build colours from strings.
TOKEN_LOADER = CORE / "ui" / "src" / "Tokens.cpp"

LINE_COMMENT = re.compile(r"//.*$")


def cpp_files(base: Path) -> list[Path]:
    if not base.exists():
        return []
    return sorted(p for p in base.rglob("*") if p.suffix in CPP_SUFFIXES)


def is_layout_host(path: Path) -> bool:
    rel = path.relative_to(CORE).as_posix()
    return (
        rel.startswith("ui/layout/")
        or path.stem.endswith("View")
        or "Layout" in path.stem
        or path.stem.endswith("Window")
    )


def code_lines(path: Path):
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        yield number, LINE_COMMENT.sub("", raw)


def main() -> int:
    violations: list[str] = []

    for path in cpp_files(CORE / "ui") + cpp_files(CORE / "app"):
        rel = path.relative_to(ROOT).as_posix()
        for number, line in code_lines(path):
            for pattern, label in VISUAL_PATTERNS:
                if path != TOKEN_LOADER and pattern.search(line):
                    violations.append(f"{rel}:{number}: rule 1 ({label}): {line.strip()}")
            if GEOMETRY_CALL.search(line) and not is_layout_host(path):
                violations.append(f"{rel}:{number}: rule 2 (geometry outside layout host): {line.strip()}")

    for path in cpp_files(CORE / "domain"):
        rel = path.relative_to(ROOT).as_posix()
        for number, line in code_lines(path):
            if DOMAIN_FORBIDDEN_INCLUDE.search(line):
                violations.append(f"{rel}:{number}: rule 3 (domain includes UI/framework): {line.strip()}")

    if violations:
        print("Hygiene violations:")
        print("\n".join(violations))
        return 1

    print("Hygiene checks passed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
