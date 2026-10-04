#!/usr/bin/env python3
"""Hermetic positive and negative tests for the source-closure validator."""

from __future__ import annotations

import csv
import hashlib
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VALIDATOR = ROOT / ".github" / "scripts" / "validate-source-closure.py"
FIELDS = [
	"schema",
	"edge_id",
	"parent_id",
	"project_id",
	"path",
	"kind",
	"upstream_url",
	"upstream_revision",
	"declared_url",
	"locator_revision",
	"pf_url",
	"pf_revision",
	"tree_oid",
	"content_sha256",
	"license_path",
	"license_sha256",
	"selectors",
	"tests",
	"patch_status",
	"transform_receipt",
	"fork_history_proof",
	"fork_pin_ref",
]


def run(*args: str, cwd: Path) -> str:
	result = subprocess.run(args, cwd=cwd, check=True, text=True, capture_output=True)
	return result.stdout.strip()


def sha256(data: bytes) -> str:
	return hashlib.sha256(data).hexdigest()


class SourceClosureTests(unittest.TestCase):
	def setUp(self) -> None:
		temp_parent = Path(os.environ.get("RUNNER_TEMP", ROOT / ".ci-cache"))
		temp_parent.mkdir(parents=True, exist_ok=True)
		self.temp = tempfile.TemporaryDirectory(prefix="gamescope-source-closure-", dir=temp_parent)
		self.base = Path(self.temp.name)
		self.cache = self.base / "cache"
		(self.cache / "repos").mkdir(parents=True)
		self.root = self.base / "root"
		self.root.mkdir()
		run("git", "init", "-q", cwd=self.root)
		run("git", "config", "user.name", "Fixture", cwd=self.root)
		run("git", "config", "user.email", "fixture@example.invalid", cwd=self.root)

		self.grandchild = self.make_project("grandchild", "grandchild licence\n")
		self.child = self.make_project(
			"child",
			"child licence\n",
			gitlink=("deps/grandchild", self.grandchild),
		)
		self.wrapped = self.make_project("wrapped", "wrapped licence\n")
		self.snapshot = self.make_project(
			"snapshot",
			"snapshot licence\n",
			extra_files={"snapshot.bin": b"admitted snapshot\n"},
		)

		(self.root / ".gitmodules").write_text(
			"[submodule \"deps/child\"]\n"
			"\tpath = deps/child\n"
			"\turl = https://github.com/pocketforge-os/child.git\n",
			encoding="utf-8",
		)
		(self.root / "subprojects").mkdir()
		(self.root / "subprojects" / "wrapped.wrap").write_text(
			"[wrap-git]\n"
			"url = https://github.com/pocketforge-os/wrapped.git\n"
			f"revision = {self.wrapped['commit']}\n",
			encoding="utf-8",
		)
		(self.root / "vendor").mkdir()
		(self.root / "vendor" / "snapshot.bin").write_bytes(b"admitted snapshot\n")
		(self.root / "fixture-transform-v1").write_text("fixture transform\n", encoding="utf-8")
		(self.root / ".github").mkdir()
		self.registry = self.root / ".github" / "vendored-sources.tsv"
		self.registry.write_text(
			"# edge_id\tpath\n"
			"snapshot\tvendor/snapshot.bin\n",
			encoding="utf-8",
		)
		run("git", "add", ".", cwd=self.root)
		run(
			"git",
			"update-index",
			"--add",
			"--cacheinfo",
			"160000",
			self.child["commit"],
			"deps/child",
			cwd=self.root,
		)
		run("git", "commit", "-qm", "fixture root", cwd=self.root)

		self.manifest = self.root / ".github" / "pocketforge-source-closure.tsv"
		self.rows = [
			self.row(
				"child",
				"gamescope",
				"child",
				"deps/child",
				"gitlink",
				self.child,
			),
			self.row(
				"grandchild",
				"child",
				"grandchild",
				"deps/child/deps/grandchild",
				"gitlink",
				self.grandchild,
			),
			self.row(
				"wrapped",
				"gamescope",
				"wrapped",
				"subprojects/wrapped.wrap",
				"wrap-git",
				self.wrapped,
			),
			self.row(
				"snapshot",
				"gamescope",
				"snapshot",
				"vendor/snapshot.bin",
				"vendored-snapshot",
				self.snapshot,
				content_sha256=sha256(b"admitted snapshot\n"),
				transform_receipt="fixture-transform-v1",
			),
		]
		self.write_manifest(self.rows)

	def tearDown(self) -> None:
		self.temp.cleanup()

	def make_project(
		self,
		name: str,
		license_text: str,
		*,
		gitlink: tuple[str, dict[str, str]] | None = None,
		extra_files: dict[str, bytes] | None = None,
	) -> dict[str, str]:
		work = self.base / f"{name}-work"
		work.mkdir()
		run("git", "init", "-q", cwd=work)
		run("git", "config", "user.name", "Fixture", cwd=work)
		run("git", "config", "user.email", "fixture@example.invalid", cwd=work)
		(work / "LICENSE").write_text(license_text, encoding="utf-8")
		for path, data in (extra_files or {}).items():
			target = work / path
			target.parent.mkdir(parents=True, exist_ok=True)
			target.write_bytes(data)
		if gitlink is not None:
			path, child = gitlink
			(work / ".gitmodules").write_text(
				f"[submodule \"{path}\"]\n"
				f"\tpath = {path}\n"
				f"\turl = https://github.com/pocketforge-os/{child['name']}.git\n",
				encoding="utf-8",
			)
		run("git", "add", ".", cwd=work)
		if gitlink is not None:
			path, child = gitlink
			run(
				"git",
				"update-index",
				"--add",
				"--cacheinfo",
				"160000",
				child["commit"],
				path,
				cwd=work,
			)
		run("git", "commit", "-qm", f"{name} fixture", cwd=work)
		commit = run("git", "rev-parse", "HEAD", cwd=work)
		tree = run("git", "rev-parse", "HEAD^{tree}", cwd=work)
		bare = self.cache / "repos" / f"{name}.git"
		run("git", "clone", "-q", "--bare", str(work), str(bare), cwd=self.base)
		return {
			"name": name,
			"commit": commit,
			"tree": tree,
			"license_sha256": sha256(license_text.encode()),
		}

	def row(
		self,
		edge_id: str,
		parent_id: str,
		project_id: str,
		path: str,
		kind: str,
		project: dict[str, str],
		*,
		content_sha256: str = "-",
		transform_receipt: str = "-",
	) -> dict[str, str]:
		pf_url = f"https://github.com/pocketforge-os/{project_id}.git"
		return {
			"schema": "1",
			"edge_id": edge_id,
			"parent_id": parent_id,
			"project_id": project_id,
			"path": path,
			"kind": kind,
			"upstream_url": f"https://example.invalid/{project_id}.git",
			"upstream_revision": project["commit"],
			"declared_url": "-" if kind == "vendored-snapshot" else pf_url,
			"locator_revision": project["commit"],
			"pf_url": pf_url,
			"pf_revision": project["commit"],
			"tree_oid": project["tree"],
			"content_sha256": content_sha256,
			"license_path": "LICENSE",
			"license_sha256": project["license_sha256"],
			"selectors": "fixture",
			"tests": "fixture",
			"patch_status": "unpatched",
			"transform_receipt": transform_receipt,
			"fork_history_proof": "fixture-proof",
			"fork_pin_ref": "pocketforge",
		}

	def write_manifest(self, rows: list[dict[str, str]], path: Path | None = None) -> Path:
		path = path or self.manifest
		with path.open("w", encoding="utf-8", newline="") as stream:
			stream.write("# gamescope-source-closure-v1\n")
			writer = csv.DictWriter(stream, fieldnames=FIELDS, dialect="excel-tab", lineterminator="\n")
			writer.writeheader()
			writer.writerows(rows)
		return path

	def invoke(self, manifest: Path | None = None) -> subprocess.CompletedProcess[str]:
		return subprocess.run(
			[
				sys.executable,
				str(VALIDATOR),
				"--repo-root",
				str(self.root),
				"--manifest",
				str(manifest or self.manifest),
				"--cache-root",
				str(self.cache),
				"--vendored-registry",
				str(self.registry),
			],
			text=True,
			capture_output=True,
		)

	def mutated_manifest(self, edge_id: str, field: str, value: str) -> Path:
		rows = [row.copy() for row in self.rows]
		for row in rows:
			if row["edge_id"] == edge_id:
				row[field] = value
				break
		else:
			raise AssertionError(edge_id)
		return self.write_manifest(rows, self.base / f"{edge_id}-{field}.tsv")

	def assert_rejected(self, fragment: str, manifest: Path | None = None) -> None:
		result = self.invoke(manifest)
		self.assertNotEqual(result.returncode, 0, result.stdout)
		self.assertIn(fragment, result.stderr)

	def test_positive_and_negative_controls(self) -> None:
		positive = self.invoke()
		self.assertEqual(positive.returncode, 0, positive.stderr)
		self.assertIn("validated_edges=4", positive.stdout)

		extra_wrap = self.root / "subprojects" / "unlisted.wrap"
		extra_wrap.write_text(
			"[wrap-git]\n"
			"url = https://github.com/pocketforge-os/unlisted.git\n"
			f"revision = {self.wrapped['commit']}\n",
			encoding="utf-8",
		)
		self.assert_rejected("unlisted dependency edge: gamescope:subprojects/unlisted.wrap")
		extra_wrap.unlink()

		self.assert_rejected(
			"non-PocketForge resolution URL: child",
			self.mutated_manifest("child", "pf_url", "https://example.invalid/child.git"),
		)
		self.assert_rejected(
			"mutable revision: child",
			self.mutated_manifest("child", "pf_revision", "main"),
		)
		self.assert_rejected(
			"missing full-history fork proof: child",
			self.mutated_manifest("child", "fork_history_proof", "-"),
		)

		child_cache = self.cache / "repos" / "child.git"
		missing_cache = self.cache / "repos" / "child.git.missing"
		child_cache.rename(missing_cache)
		self.assert_rejected("missing cache repository: child")
		missing_cache.rename(child_cache)

		self.assert_rejected(
			"locator revision mismatch: child",
			self.mutated_manifest("child", "locator_revision", "0" * 40),
		)
		self.assert_rejected(
			"content hash mismatch: snapshot",
			self.mutated_manifest("snapshot", "content_sha256", "0" * 64),
		)
		self.assert_rejected(
			"licence hash mismatch: child",
			self.mutated_manifest("child", "license_sha256", "0" * 64),
		)
		self.assert_rejected(
			"missing upstream base object: child",
			self.mutated_manifest("child", "upstream_revision", "0" * 40),
		)
		self.assert_rejected(
			"unresolved vendored snapshot: snapshot",
			self.mutated_manifest("snapshot", "transform_receipt", "unresolved"),
		)

		recursive_rows = [row for row in self.rows if row["edge_id"] != "grandchild"]
		recursive_manifest = self.write_manifest(recursive_rows, self.base / "recursive-drift.tsv")
		self.assert_rejected("unlisted dependency edge: child:deps/grandchild", recursive_manifest)

		extra_rows = [row.copy() for row in self.rows]
		ghost = self.rows[0].copy()
		ghost["edge_id"] = "ghost"
		ghost["path"] = "deps/ghost"
		extra_rows.append(ghost)
		extra_manifest = self.write_manifest(extra_rows, self.base / "extra-row.tsv")
		self.assert_rejected("manifest edge not discovered: ghost", extra_manifest)


if __name__ == "__main__":
	unittest.main(verbosity=2)
