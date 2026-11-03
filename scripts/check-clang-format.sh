#!/usr/bin/env bash
set -euo pipefail

# Usage: bash scripts/check-clang-format.sh [clang-format-binary]
# Uses the repository's .clang-format. Historical academic exercises are excluded.
formatter=${1:-clang-format}
if ! command -v "$formatter" >/dev/null 2>&1; then
    printf 'ERROR: missing formatting tool: %s\n' "$formatter" >&2
    exit 127
fi

repo_root=$(git -C "$(dirname -- "${BASH_SOURCE[0]}")" rev-parse --show-toplevel)
cd "$repo_root"

git ls-files -z -- '*.c' '*.h' '*.cpp' '*.hpp' '*.tpp' ':(exclude)archive/**' |
    xargs -0 -r "$formatter" --dry-run --Werror
