#!/usr/bin/env python3
"""Validate Gamescope's complete PocketForge source dependency closure."""

from __future__ import annotations

import argparse
import configparser
import csv
import hashlib
import io
import os
import re
import subprocess
import sys
import tarfile
import tempfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath


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
HEX40 = re.compile(r"^[0-9a-f]{40}$")
HEX64 = re.compile(r"^[0-9a-f]{64}$")
SAFE_ID = re.compile(r"^[A-Za-z0-9._-]+$")
PF_URL = re.compile(r"^https://github\.com/pocketforge-os/[A-Za-z0-9._-]+(?:\.git)?$")
KINDS = {"gitlink", "wrap-git", "vendored-snapshot"}
PATCH_STATUSES = {"unpatched", "patched", "snapshot-exact", "snapshot-patched"}
SNAPSHOT_TRANSFORMS = {"exact-copy", "sol2-amalgamation-v1"}


class ClosureError(Exception):
	pass


@dataclass(frozen=True)
class Edge:
	values: dict[str, str]

	def __getattr__(self, name: str) -> str:
		try:
			return self.values[name]
		except KeyError as error:
			raise AttributeError(name) from error


@dataclass(frozen=True)
class DiscoveredEdge:
	edge_id: str
	parent_id: str
	path: str
	kind: str
	declared_url: str
	locator_revision: str
	content_sha256: str = "-"


def fail(message: str) -> None:
	raise ClosureError(message)


def safe_path(value: str, field: str, edge_id: str) -> None:
	path = PurePosixPath(value)
	if value in {"", ".", "-"} or path.is_absolute() or ".." in path.parts:
		fail(f"unsafe {field}: {edge_id}")


def run_git(git_dir: Path, *args: str, check: bool = True) -> subprocess.CompletedProcess[bytes]:
	return subprocess.run(
		["git", f"--git-dir={git_dir}", *args],
		check=check,
		capture_output=True,
	)


def parse_manifest(path: Path) -> tuple[list[Edge], bytes]:
	try:
		raw = path.read_bytes()
	except OSError as error:
		fail(f"cannot read manifest: {error}")
	lines = raw.decode("utf-8").splitlines()
	if not lines or lines[0] != "# gamescope-source-closure-v1":
		fail("unsupported manifest schema marker")
	reader = csv.DictReader(lines[1:], dialect="excel-tab")
	if reader.fieldnames != FIELDS:
		fail("manifest header mismatch")

	edges: list[Edge] = []
	by_id: set[str] = set()
	by_parent_path: set[tuple[str, str]] = set()
	for number, values in enumerate(reader, start=3):
		if None in values or any(value is None for value in values.values()):
			fail(f"invalid manifest field count on line {number}")
		edge = Edge(dict(values))
		if edge.schema != "1":
			fail(f"unsupported row schema: {edge.edge_id or number}")
		for field in ("edge_id", "parent_id", "project_id"):
			if not SAFE_ID.fullmatch(getattr(edge, field)):
				fail(f"unsafe {field}: {edge.edge_id or number}")
		safe_path(edge.path, "path", edge.edge_id)
		if edge.kind not in KINDS:
			fail(f"unsupported source kind: {edge.edge_id}")
		if not edge.upstream_url.startswith("https://"):
			fail(f"missing canonical upstream URL: {edge.edge_id}")
		for field in ("upstream_revision", "locator_revision", "pf_revision", "tree_oid"):
			value = getattr(edge, field)
			if not HEX40.fullmatch(value):
				fail(f"mutable revision: {edge.edge_id}")
		if edge.kind == "vendored-snapshot":
			if edge.declared_url != "-":
				fail(f"snapshot has acquisition locator: {edge.edge_id}")
			if not HEX64.fullmatch(edge.content_sha256):
				fail(f"missing snapshot hash: {edge.edge_id}")
			if edge.transform_receipt in {"", "-", "unresolved"}:
				fail(f"unresolved vendored snapshot: {edge.edge_id}")
			safe_path(edge.transform_receipt, "transform receipt", edge.edge_id)
		else:
			if not edge.declared_url.startswith("https://"):
				fail(f"missing declared locator: {edge.edge_id}")
			if edge.content_sha256 != "-" or edge.transform_receipt != "-":
				fail(f"unexpected content transform: {edge.edge_id}")
		if not PF_URL.fullmatch(edge.pf_url):
			fail(f"non-PocketForge resolution URL: {edge.edge_id}")
		safe_path(edge.license_path, "licence path", edge.edge_id)
		if not HEX64.fullmatch(edge.license_sha256):
			fail(f"missing licence digest: {edge.edge_id}")
		if edge.selectors in {"", "-"} or edge.tests in {"", "-"}:
			fail(f"missing selector or test role: {edge.edge_id}")
		if edge.patch_status not in PATCH_STATUSES:
			fail(f"missing patch status: {edge.edge_id}")
		if edge.fork_history_proof in {"", "-"}:
			fail(f"missing full-history fork proof: {edge.edge_id}")
		if edge.fork_pin_ref in {"", "-"}:
			fail(f"missing fork pin ref: {edge.edge_id}")
		if edge.edge_id in by_id:
			fail(f"duplicate edge id: {edge.edge_id}")
		parent_path = (edge.parent_id, edge.path)
		if parent_path in by_parent_path:
			fail(f"duplicate parent/path edge: {edge.parent_id}:{edge.path}")
		by_id.add(edge.edge_id)
		by_parent_path.add(parent_path)
		edges.append(edge)

	if not edges:
		fail("source closure manifest is empty")
	if any(edge.project_id == "libdisplay-info" for edge in edges) and not any(
		edge.project_id == "v4l-utils" for edge in edges
	):
		fail("missing required v4l-utils edge")
	return edges, raw


def parse_config(text: str, source: str) -> configparser.ConfigParser:
	parser = configparser.ConfigParser(interpolation=None, strict=True)
	parser.optionxform = str
	try:
		parser.read_string(text, source=source)
	except configparser.Error as error:
		fail(f"invalid source locator {source}: {error}")
	return parser


def submodules_from_text(text: str, source: str) -> list[tuple[str, str]]:
	parser = parse_config(text, source)
	result: list[tuple[str, str]] = []
	for section in parser.sections():
		if not section.startswith('submodule "'):
			continue
		try:
			path = parser[section]["path"]
			url = parser[section]["url"]
		except KeyError:
			fail(f"incomplete submodule locator in {source}")
		safe_path(path, "submodule path", source)
		result.append((path, url))
	return result


def wrap_from_text(text: str, source: str) -> tuple[str, str]:
	parser = parse_config(text, source)
	if parser.sections() != ["wrap-git"]:
		fail(f"unsupported or mutable wrap kind: {source}")
	try:
		return parser["wrap-git"]["url"], parser["wrap-git"]["revision"]
	except KeyError:
		fail(f"incomplete wrap locator: {source}")


def root_gitlinks(repo_root: Path) -> dict[str, str]:
	result = subprocess.run(
		["git", "-C", str(repo_root), "ls-files", "--stage", "-z"],
		check=True,
		capture_output=True,
	)
	gitlinks: dict[str, str] = {}
	for record in result.stdout.split(b"\0"):
		if not record:
			continue
		metadata, raw_path = record.split(b"\t", 1)
		mode, oid, stage = metadata.decode().split()
		if mode == "160000" and stage == "0":
			gitlinks[raw_path.decode()] = oid
	return gitlinks


def repo_for(cache_root: Path, project_id: str) -> Path:
	return cache_root / "repos" / f"{project_id}.git"


def git_show(repo: Path, revision: str, path: str) -> bytes | None:
	result = run_git(repo, "show", f"{revision}:{path}", check=False)
	if result.returncode == 0:
		return result.stdout
	return None


def gitlinks_at(repo: Path, revision: str) -> dict[str, str]:
	result = run_git(repo, "ls-tree", "-r", "-z", revision)
	gitlinks: dict[str, str] = {}
	for record in result.stdout.split(b"\0"):
		if not record:
			continue
		metadata, raw_path = record.split(b"\t", 1)
		mode, _kind, oid = metadata.decode().split()
		if mode == "160000":
			gitlinks[raw_path.decode()] = oid
	return gitlinks


def wrap_paths_at(repo: Path, revision: str) -> list[str]:
	result = run_git(repo, "ls-tree", "-r", "--name-only", "-z", revision)
	paths = []
	for raw_path in result.stdout.split(b"\0"):
		if not raw_path:
			continue
		path = raw_path.decode()
		parts = PurePosixPath(path).parts
		if len(parts) == 2 and parts[0] == "subprojects" and path.endswith(".wrap"):
			paths.append(path)
	return sorted(paths)


def source_directory(edge: Edge) -> PurePosixPath:
	path = PurePosixPath(edge.path)
	if edge.kind == "wrap-git":
		return path.with_suffix("")
	return path


def find_edge(edges: list[Edge], parent_id: str, path: str) -> Edge | None:
	for edge in edges:
		if edge.parent_id == parent_id and edge.path == path:
			return edge
	return None


def discover_edges(
	repo_root: Path,
	cache_root: Path,
	edges: list[Edge],
	registry: Path,
) -> dict[str, DiscoveredEdge]:
	discovered: dict[str, DiscoveredEdge] = {}
	queue: list[Edge] = []

	modules_path = repo_root / ".gitmodules"
	modules = submodules_from_text(modules_path.read_text(encoding="utf-8"), str(modules_path))
	gitlinks = root_gitlinks(repo_root)
	module_paths = {path for path, _url in modules}
	if module_paths != set(gitlinks):
		missing = sorted(module_paths.symmetric_difference(gitlinks))[0]
		fail(f"root gitlink/.gitmodules drift: {missing}")
	for path, url in sorted(modules):
		edge = find_edge(edges, "gamescope", path)
		if edge is None:
			fail(f"unlisted dependency edge: gamescope:{path}")
		actual = DiscoveredEdge(edge.edge_id, "gamescope", path, "gitlink", url, gitlinks[path])
		discovered[edge.edge_id] = actual
		queue.append(edge)

	for wrap_path in sorted((repo_root / "subprojects").glob("*.wrap")):
		relative = wrap_path.relative_to(repo_root).as_posix()
		url, revision = wrap_from_text(wrap_path.read_text(encoding="utf-8"), relative)
		edge = find_edge(edges, "gamescope", relative)
		if edge is None:
			fail(f"unlisted dependency edge: gamescope:{relative}")
		actual = DiscoveredEdge(edge.edge_id, "gamescope", relative, "wrap-git", url, revision)
		discovered[edge.edge_id] = actual
		queue.append(edge)

	seen_occurrences: set[tuple[str, str, str]] = set()
	while queue:
		parent = queue.pop(0)
		if parent.kind == "vendored-snapshot":
			continue
		prefix = source_directory(parent)
		occurrence_key = (parent.project_id, parent.pf_revision, prefix.as_posix())
		if occurrence_key in seen_occurrences:
			continue
		seen_occurrences.add(occurrence_key)
		repo = repo_for(cache_root, parent.project_id)
		if not repo.is_dir():
			fail(f"missing cache repository: {parent.project_id}")
		if run_git(repo, "cat-file", "-e", f"{parent.pf_revision}^{{commit}}", check=False).returncode != 0:
			fail(f"missing cache object: {parent.edge_id}")

		parent_gitlinks = gitlinks_at(repo, parent.pf_revision)
		modules_raw = git_show(repo, parent.pf_revision, ".gitmodules")
		modules = (
			submodules_from_text(modules_raw.decode("utf-8"), f"{parent.project_id}:.gitmodules")
			if modules_raw is not None
			else []
		)
		module_paths = {relative for relative, _url in modules}
		if module_paths != set(parent_gitlinks):
			relative = sorted(module_paths.symmetric_difference(parent_gitlinks))[0]
			full_path = (prefix / relative).as_posix()
			fail(f"recursive gitlink/.gitmodules drift: {parent.project_id}:{full_path}")
		for relative, url in sorted(modules):
				full_path = (prefix / relative).as_posix()
				edge = find_edge(edges, parent.project_id, full_path)
				if edge is None:
					fail(f"unlisted dependency edge: {parent.project_id}:{full_path}")
				actual = DiscoveredEdge(
					edge.edge_id,
					parent.project_id,
					full_path,
					"gitlink",
					url,
					parent_gitlinks[relative],
				)
				discovered[edge.edge_id] = actual
				queue.append(edge)

		for relative in wrap_paths_at(repo, parent.pf_revision):
			wrap_raw = git_show(repo, parent.pf_revision, relative)
			assert wrap_raw is not None
			url, revision = wrap_from_text(wrap_raw.decode("utf-8"), f"{parent.project_id}:{relative}")
			full_path = (prefix / relative).as_posix()
			edge = find_edge(edges, parent.project_id, full_path)
			if edge is None:
				fail(f"unlisted dependency edge: {parent.project_id}:{full_path}")
			actual = DiscoveredEdge(
				edge.edge_id,
				parent.project_id,
				full_path,
				"wrap-git",
				url,
				revision,
			)
			discovered[edge.edge_id] = actual
			queue.append(edge)

	registry_rows = parse_vendored_registry(registry)
	for edge_id, rows in registry_rows.items():
		edge = next((item for item in edges if item.edge_id == edge_id), None)
		if edge is None:
			fail(f"unlisted vendored snapshot: {edge_id}")
		if edge.kind != "vendored-snapshot":
			fail(f"vendored registry kind mismatch: {edge_id}")
		digests: list[tuple[str, str]] = []
		for relative, _source, _transform in sorted(rows):
			try:
				content = (repo_root / relative).read_bytes()
			except OSError:
				fail(f"missing vendored snapshot file: {edge_id}:{relative}")
			digests.append((relative, hashlib.sha256(content).hexdigest()))
		if len(digests) == 1:
			content_sha256 = digests[0][1]
		else:
			encoded = "".join(f"{path}\t{digest}\n" for path, digest in digests).encode()
			content_sha256 = hashlib.sha256(encoded).hexdigest()
		discovered[edge.edge_id] = DiscoveredEdge(
			edge.edge_id,
			edge.parent_id,
			edge.path,
			edge.kind,
			"-",
			edge.locator_revision,
			content_sha256,
		)

	return discovered


def parse_vendored_registry(path: Path) -> dict[str, list[tuple[str, str, str]]]:
	lines = path.read_text(encoding="utf-8").splitlines()
	if len(lines) < 2 or lines[0] != "# gamescope-vendored-sources-v1":
		fail("unsupported vendored registry schema marker")
	reader = csv.DictReader(lines[1:], dialect="excel-tab")
	if reader.fieldnames != ["edge_id", "target_path", "source_path", "transform"]:
		fail("vendored registry header mismatch")
	rows: dict[str, list[tuple[str, str, str]]] = {}
	seen_targets: set[str] = set()
	for number, values in enumerate(reader, start=3):
		if None in values or any(value is None for value in values.values()):
			fail(f"invalid vendored registry row: {number}")
		edge_id = values["edge_id"]
		target = values["target_path"]
		source = values["source_path"]
		transform = values["transform"]
		if not SAFE_ID.fullmatch(edge_id):
			fail(f"unsafe vendored edge id: {number}")
		safe_path(target, "vendored target path", edge_id)
		safe_path(source, "vendored source path", edge_id)
		if transform not in SNAPSHOT_TRANSFORMS:
			fail(f"unsupported vendored transform: {edge_id}")
		if target in seen_targets:
			fail(f"duplicate vendored target: {target}")
		seen_targets.add(target)
		rows.setdefault(edge_id, []).append((target, source, transform))
	return rows


def compare_inventory(edges: list[Edge], discovered: dict[str, DiscoveredEdge]) -> None:
	for edge in edges:
		actual = discovered.get(edge.edge_id)
		if actual is None:
			fail(f"manifest edge not discovered: {edge.edge_id}")
		if actual.kind != edge.kind or actual.parent_id != edge.parent_id or actual.path != edge.path:
			fail(f"recursive edge mismatch: {edge.edge_id}")
		if actual.declared_url != edge.declared_url:
			fail(f"declared locator mismatch: {edge.edge_id}")
		if actual.locator_revision != edge.locator_revision:
			fail(f"locator revision mismatch: {edge.edge_id}")
		if edge.kind == "vendored-snapshot" and actual.content_sha256 != edge.content_sha256:
			fail(f"content hash mismatch: {edge.edge_id}")
	if len(discovered) != len(edges):
		fail("discovered edge count mismatch")


def verify_sol2_amalgamation(cache_root: Path, repo: Path, edge: Edge) -> None:
	archive = run_git(repo, "archive", edge.pf_revision).stdout
	with tempfile.TemporaryDirectory(prefix="sol2-transform-", dir=cache_root) as temporary:
		root = Path(temporary)
		with tarfile.open(fileobj=io.BytesIO(archive), mode="r:") as source:
			for member in source.getmembers():
				safe_path(member.name, "sol2 archive path", edge.edge_id)
				if member.issym() or member.islnk():
					fail(f"unsupported sol2 archive link: {member.name}")
			source.extractall(root, filter="data")
		test_path = root / "tests" / "single_header_snapshot.py"
		test_text = test_path.read_text(encoding="utf-8")
		match = re.search(r'^EXPECTED_SHA256 = "([0-9a-f]{64})"$', test_text, re.MULTILINE)
		if match is None or match.group(1) != edge.content_sha256:
			fail(f"sol2 transform digest mismatch: {edge.edge_id}")
		environment = os.environ.copy()
		environment["TMPDIR"] = temporary
		result = subprocess.run(
			[sys.executable, str(test_path)],
			cwd=root,
			env=environment,
			capture_output=True,
			text=True,
		)
		if result.returncode != 0:
			fail(f"sol2 transform failed: {edge.edge_id}: {result.stderr.strip()}")


def verify_vendored_sources(
	cache_root: Path,
	repo_root: Path,
	edges: list[Edge],
	registry: Path,
) -> None:
	rows = parse_vendored_registry(registry)
	snapshot_ids = {edge.edge_id for edge in edges if edge.kind == "vendored-snapshot"}
	if set(rows) != snapshot_ids:
		fail("vendored registry/manifest edge mismatch")
	for edge in edges:
		if edge.kind != "vendored-snapshot":
			continue
		repo = repo_for(cache_root, edge.project_id)
		transforms = {transform for _target, _source, transform in rows[edge.edge_id]}
		if transforms == {"sol2-amalgamation-v1"}:
			if len(rows[edge.edge_id]) != 1:
				fail(f"invalid sol2 transform row count: {edge.edge_id}")
			verify_sol2_amalgamation(cache_root, repo, edge)
		elif transforms == {"exact-copy"}:
			for target, source, _transform in rows[edge.edge_id]:
				source_content = git_show(repo, edge.pf_revision, source)
				if source_content is None:
					fail(f"missing vendored source path: {edge.edge_id}:{source}")
				if (repo_root / target).read_bytes() != source_content:
					fail(f"vendored source byte mismatch: {edge.edge_id}:{target}")
		else:
			fail(f"mixed vendored transforms: {edge.edge_id}")
		receipt = (repo_root / edge.transform_receipt).read_text(encoding="utf-8")
		for token in (edge.upstream_revision, edge.pf_revision, edge.content_sha256):
			if token not in receipt:
				fail(f"stale transform receipt: {edge.edge_id}")


def validate_cache(
	cache_root: Path,
	repo_root: Path,
	edges: list[Edge],
	registry: Path,
) -> int:
	projects: set[tuple[str, str]] = set()
	for edge in edges:
		repo = repo_for(cache_root, edge.project_id)
		if not repo.is_dir():
			fail(f"missing cache repository: {edge.project_id}")
		for label, revision in (
			("upstream base", edge.upstream_revision),
			("locator", edge.locator_revision),
			("PocketForge pin", edge.pf_revision),
		):
			if run_git(repo, "cat-file", "-e", f"{revision}^{{commit}}", check=False).returncode != 0:
				fail(f"missing {label} object: {edge.edge_id}")
		actual_tree = run_git(repo, "rev-parse", f"{edge.pf_revision}^{{tree}}").stdout.decode().strip()
		if actual_tree != edge.tree_oid:
			fail(f"tree pin mismatch: {edge.edge_id}")
		license_content = git_show(repo, edge.pf_revision, edge.license_path)
		if license_content is None:
			fail(f"missing licence path: {edge.edge_id}")
		actual_license = hashlib.sha256(license_content).hexdigest()
		if actual_license != edge.license_sha256:
			fail(f"licence hash mismatch: {edge.edge_id}")
		if edge.upstream_revision != edge.pf_revision:
			ancestry = run_git(
				repo,
				"merge-base",
				"--is-ancestor",
				edge.upstream_revision,
				edge.pf_revision,
				check=False,
			)
			if ancestry.returncode != 0:
				fail(f"PocketForge pin is not based on upstream base: {edge.edge_id}")
		if edge.locator_revision != edge.pf_revision:
			if edge.patch_status != "patched":
				fail(f"unpatched locator differs from PocketForge pin: {edge.edge_id}")
			ancestry = run_git(
				repo,
				"merge-base",
				"--is-ancestor",
				edge.upstream_revision,
				edge.locator_revision,
				check=False,
			)
			if ancestry.returncode != 0:
				fail(f"locator pin is not based on upstream base: {edge.edge_id}")
		if edge.kind == "vendored-snapshot":
			receipt = repo_root / edge.transform_receipt
			if not receipt.is_file():
				fail(f"missing transform receipt: {edge.edge_id}")
		projects.add((edge.project_id, edge.pf_revision))
	verify_vendored_sources(cache_root, repo_root, edges, registry)
	return len(projects)


def parse_args() -> argparse.Namespace:
	parser = argparse.ArgumentParser()
	parser.add_argument("--repo-root", type=Path, required=True)
	parser.add_argument("--manifest", type=Path, required=True)
	parser.add_argument("--cache-root", type=Path, required=True)
	parser.add_argument("--vendored-registry", type=Path, required=True)
	return parser.parse_args()


def main() -> int:
	args = parse_args()
	try:
		edges, raw_manifest = parse_manifest(args.manifest)
		discovered = discover_edges(args.repo_root, args.cache_root, edges, args.vendored_registry)
		compare_inventory(edges, discovered)
		project_count = validate_cache(
			args.cache_root,
			args.repo_root,
			edges,
			args.vendored_registry,
		)
	except (ClosureError, OSError, subprocess.CalledProcessError, UnicodeError) as error:
		print(f"closure: {error}", file=sys.stderr)
		return 1

	print(f"manifest_sha256={hashlib.sha256(raw_manifest).hexdigest()}")
	print(f"validated_edges={len(edges)}")
	print(f"verified_project_pins={project_count}")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
