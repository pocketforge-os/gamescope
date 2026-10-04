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
manifest="$repo_root/.github/meson-sources.lock"
manifest_key="$(sha256sum "$manifest" | awk '{print $1}')"
cache_root="${MESON_SOURCE_CACHE_ROOT:-${RUNNER_TEMP:-$repo_root/.ci-cache}/gamescope-meson-sources}"
cache_dir="$cache_root/$manifest_key"
packagecache="$repo_root/subprojects/packagecache"
install -d "$cache_dir" "$packagecache"

cache_result=warm
entries=0
while IFS=$'\t' read -r name kind revision archive_filename archive_root expected_hash archive_url meson_path license_path expected_license_hash extra; do
	if [[ -z "$name" || "$name" == \#* ]]; then
		continue
	fi
	if [[ -n "${extra:-}" || -z "$expected_license_hash" ]]; then
		echo "invalid manifest row for $name" >&2
		exit 1
	fi
	if [[ "$kind" != gitlink && "$kind" != wrap-file ]]; then
		echo "invalid source kind for $name: $kind" >&2
		exit 1
	fi
	if [[ ! "$revision" =~ ^[0-9a-f]{40}$ || ! "$expected_hash" =~ ^[0-9a-f]{64}$ || ! "$expected_license_hash" =~ ^[0-9a-f]{64}$ ]]; then
		echo "invalid immutable digest for $name" >&2
		exit 1
	fi
	if [[ "$archive_filename" == */* || "$archive_root" == */* || "$license_path" == /* || "$license_path" == *..* ]]; then
		echo "unsafe archive metadata for $name" >&2
		exit 1
	fi
	if [[ "$archive_url" != https://* || "$meson_path" != subprojects/* ]]; then
		echo "invalid source location for $name" >&2
		exit 1
	fi

	archive="$cache_dir/$archive_filename"
	if [[ -e "$archive" ]]; then
		actual_hash="$(sha256sum "$archive" | awk '{print $1}')"
		if [[ "$actual_hash" != "$expected_hash" ]]; then
			echo "cached archive hash mismatch for $name: expected $expected_hash, got $actual_hash" >&2
			exit 1
		fi
	else
		if [[ "$offline" == 1 ]]; then
			echo "offline cache is missing $archive_filename" >&2
			exit 1
		fi
		partial="$archive.partial.$$"
		trap 'if [[ -n "${partial:-}" && -f "$partial" ]]; then unlink "$partial"; fi' EXIT
		curl --fail --location --retry 3 --output "$partial" "$archive_url"
		actual_hash="$(sha256sum "$partial" | awk '{print $1}')"
		if [[ "$actual_hash" != "$expected_hash" ]]; then
			echo "downloaded archive hash mismatch for $name: expected $expected_hash, got $actual_hash" >&2
			exit 1
		fi
		mv "$partial" "$archive"
		partial=
		trap - EXIT
		cache_result=cold
	fi

	actual_license_hash="$(tar -xOf "$archive" "$archive_root/$license_path" | sha256sum | awk '{print $1}')"
	if [[ "$actual_license_hash" != "$expected_license_hash" ]]; then
		echo "licence hash mismatch for $name: expected $expected_license_hash, got $actual_license_hash" >&2
		exit 1
	fi

	if [[ "$kind" == wrap-file ]]; then
		package="$packagecache/$archive_filename"
		if [[ -e "$package" ]]; then
			actual_hash="$(sha256sum "$package" | awk '{print $1}')"
			if [[ "$actual_hash" != "$expected_hash" ]]; then
				echo "Meson packagecache hash mismatch for $name: expected $expected_hash, got $actual_hash" >&2
				exit 1
			fi
		else
			install -m 0644 "$archive" "$package"
		fi
	fi
	entries=$((entries + 1))
done < "$manifest"

if (( entries == 0 )); then
	echo "Meson source manifest is empty" >&2
	exit 1
fi

printf 'manifest_key=%s\n' "$manifest_key"
printf 'cache_result=%s\n' "$cache_result"
printf 'verified_sources=%d\n' "$entries"
