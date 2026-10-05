#!/usr/bin/env python3
"""Fail if a configured Gamescope test surface silently shrinks."""

from __future__ import annotations

import argparse
import sys
from collections import Counter
from pathlib import Path


EXPECTED_COUNTS = {
	"gamescope": 7,
	"libdisplay-info": 65,
	"libliftoff": 58,
}
REQUIRED = {
	"gamescope:output-staging",
	"gamescope:output-rotation",
	"gamescope:system-overlay-auth",
	"gamescope:output-rotation-vulkan",
	"gamescope:output-staging-vulkan",
	"gamescope:convar",
	"gamescope:vulkan_present_features",
	"libdisplay-info:pocketforge-source-locator",
	"libliftoff:check_ndebug",
	"libliftoff:alloc@basic",
	"libliftoff:dynamic@same",
	"libliftoff:priority@basic",
	"libliftoff:prop@default-alpha",
	"libliftoff:candidate@basic",
}


def parse_args() -> argparse.Namespace:
	parser = argparse.ArgumentParser()
	parser.add_argument("--list-file", type=Path)
	return parser.parse_args()


def main() -> int:
	args = parse_args()
	text = args.list_file.read_text(encoding="utf-8") if args.list_file else sys.stdin.read()
	names = [line.strip() for line in text.splitlines() if line.strip()]
	counts = Counter(name.partition(":")[0] for name in names if ":" in name)
	for suite, expected in EXPECTED_COUNTS.items():
		actual = counts[suite]
		if actual != expected:
			print(f"test registration count mismatch: {suite}: expected {expected}, got {actual}", file=sys.stderr)
			return 1
	missing = sorted(REQUIRED - set(names))
	if missing:
		print(f"missing required registered test: {missing[0]}", file=sys.stderr)
		return 1
	print("registered_gamescope=7")
	print("registered_libdisplay_info=65")
	print("registered_libliftoff=58")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
