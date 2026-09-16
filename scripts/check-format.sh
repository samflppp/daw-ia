#!/usr/bin/env bash
# Checks (default) or applies (--fix) clang-format on core/ sources.
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"
mapfile -t files < <(git ls-files 'core/*.h' 'core/*.hpp' 'core/*.cpp')

if [[ ${#files[@]} -eq 0 ]]; then
    echo "No C++ files to check."
    exit 0
fi

"${CLANG_FORMAT}" --version
if [[ "${1:-}" == "--fix" ]]; then
    "${CLANG_FORMAT}" -i "${files[@]}"
else
    "${CLANG_FORMAT}" --dry-run --Werror "${files[@]}"
fi
