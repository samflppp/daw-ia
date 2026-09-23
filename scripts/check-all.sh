#!/usr/bin/env bash
# Runs every check the CI lint and Python jobs run, in the same order, and
# stops at the first one that fails.
#
# It exists because the checks live in three places — clang-format, the
# hygiene rules, the Python toolchain — and running two of them out of three
# is how a push goes red. It does not build or run the C++ tests: those are
# the presets, and they take minutes where this takes seconds.
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

step() { printf '\n== %s\n' "$1"; }

step "clang-format"
./scripts/check-format.sh

step "hygiene"
python scripts/check_hygiene.py

step "workspace manifests"
uv run --no-project --with jsonschema python scripts/validate_workspaces.py

step "ruff"
(cd services && uv run ruff check . && uv run ruff format --check .)

step "pytest"
(cd services && uv run pytest -q)

printf '\nAll checks passed.\n'
