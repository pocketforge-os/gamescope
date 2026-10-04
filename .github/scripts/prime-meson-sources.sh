#!/usr/bin/env bash
set -euo pipefail

offline="${PF_MESON_CACHE_OFFLINE:-0}"
if [[ "${1:-}" == "--offline" ]]; then
	offline=1
	shift
fi
if (( $# != 0 )); then
	echo "usage: $0 [--offline]" >&2
	exit 2
fi

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"
cache_root="${MESON_SOURCE_CACHE_ROOT:-${RUNNER_TEMP:-$repo_root/.ci-cache}/gamescope-source-closure}"

args=(
	--repo-root "$repo_root"
	--manifest "$repo_root/.github/pocketforge-source-closure.tsv"
	--vendored-registry "$repo_root/.github/vendored-sources.tsv"
	--cache-root "$cache_root"
)
if [[ "$offline" == 1 ]]; then
	args+=(--offline)
fi

exec python3 "$script_dir/admit-source-closure.py" "${args[@]}"
