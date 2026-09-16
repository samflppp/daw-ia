#!/usr/bin/env python3
"""Validates workspaces/*.json against the schema plus cross-field rules.

Requires `jsonschema`. In CI:
    uv run --no-project --with jsonschema python scripts/validate_workspaces.py
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

from jsonschema import Draft202012Validator

ROOT = Path(__file__).resolve().parent.parent
WORKSPACES = ROOT / "workspaces"
SCHEMA = WORKSPACES / "schema" / "workspace.schema.json"
EXPECTED = {"decouverte", "beatmaker", "ugc", "film"}


def layout_panels(node: dict) -> list[str]:
    if "panel" in node:
        return [node["panel"]]
    return [p for child in node["children"] for p in layout_panels(child)]


def check_manifest(path: Path, validator: Draft202012Validator) -> list[str]:
    errors: list[str] = []
    manifest = json.loads(path.read_text(encoding="utf-8"))

    for error in sorted(validator.iter_errors(manifest), key=lambda e: list(e.path)):
        location = "/".join(str(p) for p in error.path) or "<root>"
        errors.append(f"{path.name}: {location}: {error.message}")
    if errors:
        return errors

    if manifest["id"] != path.stem:
        errors.append(f"{path.name}: id '{manifest['id']}' must match file name '{path.stem}'")

    declared = manifest["panels"]
    placed = layout_panels(manifest["layout"])
    for panel in sorted(set(placed) - set(declared)):
        errors.append(f"{path.name}: layout uses undeclared panel '{panel}'")
    for panel in sorted(set(declared) - set(placed)):
        errors.append(f"{path.name}: panel '{panel}' is declared but not placed in layout")
    for panel in sorted({p for p in placed if placed.count(p) > 1}):
        errors.append(f"{path.name}: panel '{panel}' is placed more than once")

    return errors


def main() -> int:
    schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    validator = Draft202012Validator(schema)

    manifests = sorted(WORKSPACES.glob("*.json"))
    errors: list[str] = []

    missing = EXPECTED - {m.stem for m in manifests}
    errors += [f"missing workspace manifest: {name}.json" for name in sorted(missing)]

    for manifest in manifests:
        errors += check_manifest(manifest, validator)

    if errors:
        print("\n".join(errors))
        return 1

    print(f"{len(manifests)} workspace manifests valid.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
