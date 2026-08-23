#!/usr/bin/env bash
# Formats first-party C/C++ sources in place with clang-format, honouring
# .clang-format-ignore (generated files: fonts, bitmap icons).
#
# CI does not run this — it only checks formatting on the files a PR/push
# actually touches (see the "Formatação" job in .github/workflows/ci.yml), so
# the existing tree is not reformatted wholesale by this change. Run this
# script by hand if you want to bring a file (or the whole tree) up to spec.
set -euo pipefail
cd "$(dirname "$0")/.."

mapfile -t ignore_patterns < <(grep -v '^#' .clang-format-ignore | grep -v '^\s*$')

is_ignored() {
    local file="$1"
    for pattern in "${ignore_patterns[@]}"; do
        # shellcheck disable=SC2053 # intentional glob match, not literal
        [[ "$file" == $pattern ]] && return 0
    done
    return 1
}

files=()
while IFS= read -r -d '' f; do
    is_ignored "$f" || files+=("$f")
done < <(find lib src include -type f \( -name '*.cpp' -o -name '*.h' \) -print0)

echo "Formatting ${#files[@]} file(s)..."
clang-format -i "${files[@]}"
