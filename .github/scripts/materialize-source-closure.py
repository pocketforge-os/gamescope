#!/usr/bin/env python3
"""Materialize a download-free Gamescope source tree from admitted objects."""

from __future__ import annotations

import argparse
import configparser
import hashlib
import importlib.util
import json
import os
import re
import shutil
import stat
import subprocess
import sys
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
VALIDATOR_PATH = SCRIPT_DIR / "validate-source-closure.py"


def load_validator():
	spec = importlib.util.spec_from_file_location("source_closure_validator", VALIDATOR_PATH)
	if spec is None or spec.loader is None:
		raise RuntimeError("cannot load source-closure validator")
	module = importlib.util.module_from_spec(spec)
	sys.modules[spec.name] = module
	spec.loader.exec_module(module)
	return module


def run(*args: str, check: bool = True, **kwargs) -> subprocess.CompletedProcess:
	return subprocess.run(args, check=check, **kwargs)


def extract_archive(command: list[str], destination: Path) -> None:
	if destination.exists():
		if not destination.is_dir() or any(destination.iterdir()):
			raise ValueError(f"archive destination is not empty: {destination}")
	else:
		destination.mkdir(parents=True)
	archive = subprocess.Popen(command, stdout=subprocess.PIPE)
	assert archive.stdout is not None
	extract = subprocess.run(["tar", "-x", "-C", str(destination)], stdin=archive.stdout)
	archive.stdout.close()
	archive_status = archive.wait()
	if archive_status != 0 or extract.returncode != 0:
		raise subprocess.CalledProcessError(archive_status or extract.returncode, command)


def rewrite_gitmodules(path: Path, resolutions: dict[str, str]) -> int:
	parser = configparser.ConfigParser(interpolation=None, strict=True)
	parser.optionxform = str
	parser.read(path, encoding="utf-8")
	changed = 0
	seen: set[str] = set()
	for section in parser.sections():
		if not section.startswith('submodule "'):
			continue
		module_path = parser[section].get("path")
		if module_path not in resolutions:
			raise ValueError(f"unlisted materialized gitlink: {path}:{module_path}")
		parser[section]["url"] = resolutions[module_path]
		seen.add(module_path)
		changed += 1
	if seen != set(resolutions):
		missing = sorted(set(resolutions) - seen)[0]
		raise ValueError(f"missing materialized gitlink: {path}:{missing}")
	with path.open("w", encoding="utf-8") as stream:
		parser.write(stream, space_around_delimiters=True)
	return changed


def rewrite_wrap(path: Path, url: str, revision: str) -> None:
	text = path.read_text(encoding="utf-8")
	text, url_count = re.subn(r"^url\s*=.*$", f"url = {url}", text, count=1, flags=re.MULTILINE)
	text, revision_count = re.subn(
		r"^revision\s*=.*$",
		f"revision = {revision}",
		text,
		count=1,
		flags=re.MULTILINE,
	)
	if url_count != 1 or revision_count != 1:
		raise ValueError(f"cannot normalize materialized wrap: {path}")
	path.write_text(text, encoding="utf-8")


def tree_digest(root: Path) -> str:
	records: list[str] = []
	for path in sorted(root.rglob("*")):
		relative = path.relative_to(root).as_posix()
		mode = stat.S_IMODE(path.lstat().st_mode)
		if path.is_symlink():
			digest = hashlib.sha256(os.readlink(path).encode()).hexdigest()
			type_name = "symlink"
		elif path.is_file():
			digest = hashlib.sha256(path.read_bytes()).hexdigest()
			type_name = "file"
		else:
			continue
		records.append(f"{relative}\t{type_name}\t{mode:o}\t{digest}\n")
	return hashlib.sha256("".join(records).encode()).hexdigest()


def parse_args() -> argparse.Namespace:
	parser = argparse.ArgumentParser()
	parser.add_argument("--repo-root", type=Path, required=True)
	parser.add_argument("--manifest", type=Path, required=True)
	parser.add_argument("--vendored-registry", type=Path, required=True)
	parser.add_argument("--cache-root", type=Path, required=True)
	parser.add_argument("--output", type=Path, required=True)
	parser.add_argument("--receipt", type=Path, required=True)
	return parser.parse_args()


def main() -> int:
	args = parse_args()
	validator = load_validator()
	try:
		if args.output.exists():
			raise validator.ClosureError(f"materialization output already exists: {args.output}")
		edges, raw_manifest = validator.parse_manifest(args.manifest)
		discovered = validator.discover_edges(
			args.repo_root,
			args.cache_root,
			edges,
			args.vendored_registry,
		)
		validator.compare_inventory(edges, discovered)
		validator.validate_cache(
			args.cache_root,
			args.repo_root,
			edges,
			args.vendored_registry,
		)

		gamescope_head = run(
			"git",
			"-C",
			str(args.repo_root),
			"rev-parse",
			"HEAD",
			capture_output=True,
			text=True,
		).stdout.strip()
		extract_archive(
			["git", "-C", str(args.repo_root), "archive", gamescope_head],
			args.output,
		)

		materialized = 0
		for edge in edges:
			if edge.kind == "vendored-snapshot":
				continue
			destination = args.output / validator.source_directory(edge)
			destination.parent.mkdir(parents=True, exist_ok=True)
			extract_archive(
				[
					"git",
					f"--git-dir={validator.repo_for(args.cache_root, edge.project_id)}",
					"archive",
					edge.pf_revision,
				],
				destination,
			)
			materialized += 1

		root_gitlinks = {
			edge.path: edge.pf_url
			for edge in edges
			if edge.parent_id == "gamescope" and edge.kind == "gitlink"
		}
		normalized_gitlinks = rewrite_gitmodules(args.output / ".gitmodules", root_gitlinks)
		for parent in edges:
			if parent.kind == "vendored-snapshot":
				continue
			parent_root = args.output / validator.source_directory(parent)
			modules = parent_root / ".gitmodules"
			if not modules.is_file():
				continue
			children = {}
			for edge in edges:
				if edge.parent_id != parent.project_id or edge.kind != "gitlink":
					continue
				relative = Path(edge.path).relative_to(validator.source_directory(parent)).as_posix()
				children[relative] = edge.pf_url
			normalized_gitlinks += rewrite_gitmodules(modules, children)

		for edge in edges:
			if edge.kind == "wrap-git":
				rewrite_wrap(args.output / edge.path, edge.pf_url, edge.pf_revision)
			if edge.parent_id == "gamescope" and edge.kind == "wrap-git":
				overlay = args.output / "subprojects" / "packagefiles" / edge.project_id
				if overlay.is_dir():
					shutil.copytree(
						overlay,
						args.output / validator.source_directory(edge),
						dirs_exist_ok=True,
					)

		for edge in edges:
			if edge.kind != "wrap-git":
				continue
			url, revision = validator.wrap_from_text(
				(args.output / edge.path).read_text(encoding="utf-8"),
				edge.path,
			)
			if url != edge.pf_url or revision != edge.pf_revision:
				raise validator.ClosureError(f"materialized wrap mismatch: {edge.edge_id}")
		for locator in args.output.rglob(".gitmodules"):
			for _path, url in validator.submodules_from_text(
				locator.read_text(encoding="utf-8"),
				str(locator.relative_to(args.output)),
			):
				if validator.PF_URL.fullmatch(url) is None:
					raise validator.ClosureError(
						f"materialized upstream gitlink URL: {locator.relative_to(args.output)}"
					)
		for locator in args.output.rglob("*.wrap"):
			url, revision = validator.wrap_from_text(
				locator.read_text(encoding="utf-8"),
				str(locator.relative_to(args.output)),
			)
			if validator.PF_URL.fullmatch(url) is None or validator.HEX40.fullmatch(revision) is None:
				raise validator.ClosureError(
					f"materialized mutable or upstream wrap: {locator.relative_to(args.output)}"
			)

		manifest_sha256 = hashlib.sha256(raw_manifest).hexdigest()
		receipt = {
			"schema": "gamescope-source-materialization-v1",
			"gamescope_head": gamescope_head,
			"manifest_sha256": manifest_sha256,
			"materialized_git_inputs": materialized,
			"normalized_gitlink_urls": normalized_gitlinks,
			"source_tree_sha256": tree_digest(args.output),
		}
		args.receipt.parent.mkdir(parents=True, exist_ok=True)
		args.receipt.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8")
	except (
		validator.ClosureError,
		configparser.Error,
		OSError,
		subprocess.CalledProcessError,
		UnicodeError,
		ValueError,
	) as error:
		print(f"materialize: {error}", file=sys.stderr)
		return 1

	print(f"manifest_sha256={manifest_sha256}")
	print(f"gamescope_head={gamescope_head}")
	print(f"materialized_git_inputs={materialized}")
	print(f"normalized_gitlink_urls={normalized_gitlinks}")
	print(f"source_tree_sha256={receipt['source_tree_sha256']}")
	print(f"receipt={args.receipt}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
