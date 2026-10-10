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
ADMITTER = ROOT / ".github" / "scripts" / "admit-source-closure.py"
MATERIALIZER = ROOT / ".github" / "scripts" / "materialize-source-closure.py"
TEST_REGISTRATION = ROOT / ".github" / "scripts" / "assert-test-registration.py"
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
	def test_test_registration_assertion(self) -> None:
		gamescope_names = [
			"gamescope:output-staging",
			"gamescope:output-rotation",
			"gamescope:system-overlay-auth",
			"gamescope:output-rotation-vulkan",
			"gamescope:output-staging-vulkan",
			"gamescope:convar",
			"gamescope:vulkan_present_features",
			"gamescope:drm-device-selection",
			"gamescope:drm-commit-probe",
			"gamescope:pipeline-compile-probe",
			"gamescope:compositor-diagnostics",
			"gamescope:staged-readback",
			"gamescope:drm-format-selection",
		]
		names = gamescope_names + ["libdisplay-info:pocketforge-source-locator"]
		names.extend(f"libdisplay-info:fixture-{index}" for index in range(64))
		names.extend(
			[
				"libliftoff:check_ndebug",
				"libliftoff:alloc@basic",
				"libliftoff:dynamic@same",
				"libliftoff:priority@basic",
				"libliftoff:prop@default-alpha",
				"libliftoff:candidate@basic",
			]
		)
		names.extend(f"libliftoff:fixture-{index}" for index in range(52))
		fixture = "\n".join(names) + "\n"
		positive = subprocess.run(
			[sys.executable, str(TEST_REGISTRATION)],
			input=fixture,
			text=True,
			capture_output=True,
		)
		self.assertEqual(positive.returncode, 0, positive.stderr)
		self.assertIn("registered_gamescope=13", positive.stdout)
		shrink_negative = subprocess.run(
			[sys.executable, str(TEST_REGISTRATION)],
			input=fixture.replace("gamescope:output-staging\n", ""),
			text=True,
			capture_output=True,
		)
		self.assertNotEqual(shrink_negative.returncode, 0, shrink_negative.stdout)
		self.assertIn("test registration count mismatch: gamescope", shrink_negative.stderr)
		substitution_negative = subprocess.run(
			[sys.executable, str(TEST_REGISTRATION)],
			input=fixture.replace(
				"gamescope:output-staging\n",
				"gamescope:substituted-test\n",
			),
			text=True,
			capture_output=True,
		)
		self.assertNotEqual(substitution_negative.returncode, 0, substitution_negative.stdout)
		self.assertIn(
			"missing required registered test: gamescope:output-staging",
			substitution_negative.stderr,
		)

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
		self.extra_gitlink = self.make_project(
			"extra-gitlink",
			"extra gitlink licence\n",
			gitlinks=[
				("deps/grandchild", self.grandchild),
				("deps/unlisted", self.grandchild),
			],
			module_paths={"deps/grandchild"},
		)
		self.no_modules_gitlink = self.make_project(
			"no-modules-gitlink",
			"no modules gitlink licence\n",
			gitlinks=[("deps/unlisted", self.grandchild)],
			module_paths=set(),
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
			"\turl = https://github.com/pocketforge-os/child.git\n"
			"[submodule \"deps/child-second\"]\n"
			"\tpath = deps/child-second\n"
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
		snapshot_digest = sha256(b"admitted snapshot\n")
		(self.root / "fixture-transform-v1").write_text(
			f"upstream={self.snapshot['commit']}\n"
			f"pocketforge={self.snapshot['commit']}\n"
			f"content={snapshot_digest}\n",
			encoding="utf-8",
		)
		(self.root / ".github").mkdir()
		self.registry = self.root / ".github" / "vendored-sources.tsv"
		self.registry.write_text(
			"# gamescope-vendored-sources-v1\n"
			"edge_id\ttarget_path\tsource_path\ttransform\n"
			"snapshot\tvendor/snapshot.bin\tsnapshot.bin\texact-copy\n",
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
		run(
			"git",
			"update-index",
			"--add",
			"--cacheinfo",
			"160000",
			self.child["commit"],
			"deps/child-second",
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
				"child-second",
				"gamescope",
				"child",
				"deps/child-second",
				"gitlink",
				self.child,
			),
			self.row(
				"grandchild-second",
				"child",
				"grandchild",
				"deps/child-second/deps/grandchild",
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
		self.cache_base = self.base / "cache-base"
		admitted_cache = self.cache_base / sha256(self.manifest.read_bytes())
		admitted_cache.parent.mkdir()
		self.cache.rename(admitted_cache)
		self.cache = admitted_cache

	def tearDown(self) -> None:
		self.temp.cleanup()

	def make_project(
		self,
		name: str,
		license_text: str,
		*,
		gitlink: tuple[str, dict[str, str]] | None = None,
		gitlinks: list[tuple[str, dict[str, str]]] | None = None,
		module_paths: set[str] | None = None,
		extra_files: dict[str, bytes] | None = None,
	) -> dict[str, str]:
		if gitlink is not None:
			if gitlinks is not None:
				raise ValueError("gitlink and gitlinks are mutually exclusive")
			gitlinks = [gitlink]
		gitlinks = gitlinks or []
		if module_paths is None:
			module_paths = {path for path, _child in gitlinks}

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
		if module_paths:
			by_path = dict(gitlinks)
			(work / ".gitmodules").write_text(
				"".join(
					f"[submodule \"{path}\"]\n"
					f"\tpath = {path}\n"
					f"\turl = https://github.com/pocketforge-os/{by_path[path]['name']}.git\n"
					for path in sorted(module_paths)
				),
				encoding="utf-8",
			)
		run("git", "add", ".", cwd=work)
		for path, child in gitlinks:
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

	def assert_recursive_gitlink_drift_rejected(
		self,
		project: dict[str, str],
		*,
		declared_child: bool,
		expected_path: str,
	) -> None:
		project_id = project["name"]
		wrap_path = self.root / "subprojects" / f"{project_id}.wrap"
		wrap_path.write_text(
			"[wrap-git]\n"
			f"url = https://github.com/pocketforge-os/{project_id}.git\n"
			f"revision = {project['commit']}\n",
			encoding="utf-8",
		)
		rows = [row.copy() for row in self.rows]
		rows.append(
			self.row(
				project_id,
				"gamescope",
				project_id,
				f"subprojects/{project_id}.wrap",
				"wrap-git",
				project,
			)
		)
		if declared_child:
			rows.append(
				self.row(
					f"{project_id}-grandchild",
					project_id,
					"grandchild",
					f"subprojects/{project_id}/deps/grandchild",
					"gitlink",
					self.grandchild,
				)
			)
		manifest = self.write_manifest(rows, self.base / f"{project_id}-drift.tsv")
		try:
			self.assert_rejected(
				f"recursive gitlink/.gitmodules drift: {project_id}:{expected_path}",
				manifest,
			)
		finally:
			wrap_path.unlink()

	def test_positive_and_negative_controls(self) -> None:
		positive = self.invoke()
		self.assertEqual(positive.returncode, 0, positive.stderr)
		self.assertIn("validated_edges=6", positive.stdout)

		admission_receipt = self.base / "admission.json"
		admission = subprocess.run(
			[
				sys.executable,
				str(ADMITTER),
				"--repo-root",
				str(self.root),
				"--manifest",
				str(self.manifest),
				"--vendored-registry",
				str(self.registry),
				"--cache-root",
				str(self.cache_base),
				"--offline",
				"--receipt",
				str(admission_receipt),
			],
			text=True,
			capture_output=True,
		)
		self.assertEqual(admission.returncode, 0, admission.stderr)
		self.assertIn("cache_result=warm", admission.stdout)
		self.assertTrue(admission_receipt.is_file())

		materialized = self.base / "materialized"
		materialization_receipt = self.base / "materialization.json"
		materialization = subprocess.run(
			[
				sys.executable,
				str(MATERIALIZER),
				"--repo-root",
				str(self.root),
				"--manifest",
				str(self.manifest),
				"--vendored-registry",
				str(self.registry),
				"--cache-root",
				str(self.cache),
				"--output",
				str(materialized),
				"--receipt",
				str(materialization_receipt),
			],
			text=True,
			capture_output=True,
		)
		self.assertEqual(materialization.returncode, 0, materialization.stderr)
		self.assertIn("materialized_git_inputs=5", materialization.stdout)
		self.assertIn("verified_materialized_locator_targets=5", materialization.stdout)
		self.assertTrue((materialized / "deps" / "child" / "LICENSE").is_file())
		self.assertTrue((materialized / "deps" / "child" / "deps" / "grandchild" / "LICENSE").is_file())
		self.assertTrue((materialized / "deps" / "child-second" / "LICENSE").is_file())
		self.assertTrue(
			(materialized / "deps" / "child-second" / "deps" / "grandchild" / "LICENSE").is_file()
		)
		self.assertTrue((materialized / "subprojects" / "wrapped" / "LICENSE").is_file())
		self.assertTrue(materialization_receipt.is_file())

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
		self.assert_rejected(
			"unlisted dependency edge: child:deps/child/deps/grandchild",
			recursive_manifest,
		)

		second_recursive_rows = [row for row in self.rows if row["edge_id"] != "grandchild-second"]
		second_recursive_manifest = self.write_manifest(
			second_recursive_rows,
			self.base / "recursive-second-drift.tsv",
		)
		self.assert_rejected(
			"unlisted dependency edge: child:deps/child-second/deps/grandchild",
			second_recursive_manifest,
		)

		for project, declared_child, expected_path in (
			(self.extra_gitlink, True, "subprojects/extra-gitlink/deps/unlisted"),
			(self.no_modules_gitlink, False, "subprojects/no-modules-gitlink/deps/unlisted"),
		):
			with self.subTest(reverse_gitlink_drift=project["name"]):
				self.assert_recursive_gitlink_drift_rejected(
					project,
					declared_child=declared_child,
					expected_path=expected_path,
				)

		extra_rows = [row.copy() for row in self.rows]
		ghost = self.rows[0].copy()
		ghost["edge_id"] = "ghost"
		ghost["path"] = "deps/ghost"
		extra_rows.append(ghost)
		extra_manifest = self.write_manifest(extra_rows, self.base / "extra-row.tsv")
		self.assert_rejected("manifest edge not discovered: ghost", extra_manifest)


if __name__ == "__main__":
	unittest.main(verbosity=2)
