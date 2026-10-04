#!/usr/bin/env python3
"""Admit exact PocketForge source objects into a manifest-keyed cache."""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
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


def run(*args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
	return subprocess.run(args, check=check, capture_output=True, text=True)


def has_commit(repo: Path, revision: str) -> bool:
	return run(
		"git",
		f"--git-dir={repo}",
		"cat-file",
		"-e",
		f"{revision}^{{commit}}",
		check=False,
	).returncode == 0


def parse_args() -> argparse.Namespace:
	parser = argparse.ArgumentParser()
	parser.add_argument("--repo-root", type=Path, required=True)
	parser.add_argument("--manifest", type=Path, required=True)
	parser.add_argument("--vendored-registry", type=Path, required=True)
	parser.add_argument("--cache-root", type=Path, required=True)
	parser.add_argument("--offline", action="store_true")
	parser.add_argument("--receipt", type=Path)
	return parser.parse_args()


def main() -> int:
	args = parse_args()
	validator = load_validator()
	try:
		edges, raw_manifest = validator.parse_manifest(args.manifest)
		manifest_sha256 = hashlib.sha256(raw_manifest).hexdigest()
		cache = args.cache_root / manifest_sha256
		repos = cache / "repos"
		repos.mkdir(parents=True, exist_ok=True)

		projects: dict[str, dict[str, object]] = {}
		for edge in edges:
			project = projects.setdefault(
				edge.project_id,
				{"url": edge.pf_url, "revisions": set(), "pins": set()},
			)
			if project["url"] != edge.pf_url:
				raise validator.ClosureError(
					f"inconsistent PocketForge URL for project: {edge.project_id}"
				)
			project["revisions"].update(
				(edge.upstream_revision, edge.locator_revision, edge.pf_revision)
			)
			project["pins"].add(edge.pf_revision)

		cache_result = "warm"
		for project_id, values in sorted(projects.items()):
			repo = validator.repo_for(cache, project_id)
			if not repo.exists():
				if args.offline:
					raise validator.ClosureError(f"missing cache repository: {project_id}")
				run("git", "init", "--bare", "-q", str(repo))
				cache_result = "cold"
			elif not repo.is_dir():
				raise validator.ClosureError(f"invalid cache repository: {project_id}")
			for revision in sorted(values["revisions"]):
				if has_commit(repo, revision):
					continue
				if args.offline:
					raise validator.ClosureError(
						f"missing cache object: {project_id}@{revision}"
					)
				run(
					"git",
					f"--git-dir={repo}",
					"fetch",
					"--quiet",
					"--no-tags",
					"--no-write-fetch-head",
					str(values["url"]),
					revision,
				)
				if not has_commit(repo, revision):
					raise validator.ClosureError(
						f"fetch did not admit exact object: {project_id}@{revision}"
					)
				cache_result = "cold"

		discovered = validator.discover_edges(args.repo_root, cache, edges, args.vendored_registry)
		validator.compare_inventory(edges, discovered)
		pin_count = validator.validate_cache(
			cache,
			args.repo_root,
			edges,
			args.vendored_registry,
		)
		gamescope_head = run("git", "-C", str(args.repo_root), "rev-parse", "HEAD").stdout.strip()
		receipt_path = args.receipt or cache / "admission-receipt.json"
		receipt_path.parent.mkdir(parents=True, exist_ok=True)
		receipt = {
			"schema": "gamescope-source-admission-v1",
			"gamescope_head": gamescope_head,
			"manifest_sha256": manifest_sha256,
			"projects": [
				{
					"project_id": project_id,
					"pf_url": values["url"],
					"pins": sorted(values["pins"]),
				}
				for project_id, values in sorted(projects.items())
			],
			"validated_edges": len(edges),
			"verified_project_pins": pin_count,
		}
		receipt_path.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n", encoding="utf-8")
	except (
		validator.ClosureError,
		OSError,
		subprocess.CalledProcessError,
		UnicodeError,
		ValueError,
	) as error:
		print(f"admission: {error}", file=sys.stderr)
		return 1

	print(f"manifest_sha256={manifest_sha256}")
	print(f"cache_result={cache_result}")
	print(f"cache_path={cache}")
	print(f"validated_edges={len(edges)}")
	print(f"verified_sources={pin_count}")
	print(f"receipt={receipt_path}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
