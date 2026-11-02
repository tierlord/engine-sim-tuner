#!/usr/bin/env bash
# Applies the Linux port to the bundled submodules. Running it again is harmless.
# The submodules must already be checked out at their pinned commits.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

apply_patch() {
    local submodule="$1"
    local dir="$root/dependencies/submodules/$submodule"
    local patch="$root/patches/linux/$submodule.patch"

    if [ ! -e "$dir/.git" ]; then
        echo "submodule not initialized: $submodule (run: git submodule update --init --recursive)" >&2
        exit 1
    fi

    if git -C "$dir" apply --reverse --check "$patch" 2>/dev/null; then
        echo "already applied: $submodule"
    else
        git -C "$dir" apply "$patch"
        echo "applied: $submodule"
    fi
}

apply_patch delta-studio
apply_patch piranha
apply_patch simple-2d-constraint-solver
