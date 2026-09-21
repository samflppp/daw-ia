#!/usr/bin/env bash
# Checks (default) or applies (--fix) clang-format on core/ sources.
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"
# --others --exclude-standard as well as the tracked files: a source file that
# has just been written is exactly the one most likely to need formatting, and
# leaving it out made the check pass locally and fail in CI.
mapfile -t files < <(git ls-files --cached --others --exclude-standard     'core/*.h' 'core/*.hpp' 'core/*.cpp' | sort -u)

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
