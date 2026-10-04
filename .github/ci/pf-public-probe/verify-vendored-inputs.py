#!/usr/bin/env python3
"""Verify the exact pf-public probe inputs committed to Gamescope."""

import argparse
import hashlib
import json
import pathlib
import sys


AUTOMATION_REPOSITORY = "pocketforge-os/pocketforge-automation"
AUTOMATION_COMMIT = "025f7f4716e649e4eb3d9b0dea327a76021d6bbe"
CI_ROOT = pathlib.Path(".github/ci/pf-public-probe")
EXPECTED_INPUTS = {
    "vendor/pf-public-job-probe.py": {
        "source_api_path": (
            "repos/pocketforge-os/pocketforge-automation/contents/scripts/"
            "pf-public-job-probe.py?ref=025f7f4716e649e4eb3d9b0dea327a76021d6bbe"
        ),
        "source_path": "scripts/pf-public-job-probe.py",
        "source_git_blob": "4c10306be65143d35cd6f520493d83ac064d9473",
        "source_sha256": "d314f3c6e3faf65294ee2f5f95a8d47c304750336d6ed977d63174e010f3258f",
        "vendored_git_blob": "4c10306be65143d35cd6f520493d83ac064d9473",
        "vendored_sha256": "d314f3c6e3faf65294ee2f5f95a8d47c304750336d6ed977d63174e010f3258f",
        "transform": "identity",
    },
    "vendor/runner-pool-probe-contract.json": {
        "source_api_path": (
            "repos/pocketforge-os/pocketforge-automation/contents/config/"
            "runner-pool.json?ref=025f7f4716e649e4eb3d9b0dea327a76021d6bbe"
        ),
        "source_path": "config/runner-pool.json",
        "source_git_blob": "09820eb87401969ef8928dbcdc80a4ba20c6ddfc",
        "source_sha256": "ca487e4ee54366820854262b8c5ad8352ff1c2c2cd59f1912bb467fcc90b9a76",
        "extraction": 'network_policies["pf-public"]["probe_contract"]',
        "vendored_git_blob": "fc03352bf5c38e310fefa6069c295d404349819d",
        "vendored_sha256": "538535a37cf8052bd4e3033ce9882955b67684460aef2cc2a54721382a4ea475",
        "transform": "minimal JSON wrapper preserving the probe lookup path",
    },
}


def git_blob_id(data):
    header = f"blob {len(data)}\0".encode("ascii")
    return hashlib.sha1(header + data).hexdigest()


def expected_manifest():
    return {
        "schema": 1,
        "source_repository": AUTOMATION_REPOSITORY,
        "source_commit": AUTOMATION_COMMIT,
        "inputs": EXPECTED_INPUTS,
    }


def verify(root):
    ci_root = root / CI_ROOT
    manifest = json.loads((ci_root / "vendored-inputs.json").read_text())
    if manifest != expected_manifest():
        raise ValueError("provenance_manifest_drift")

    vendor_root = ci_root / "vendor"
    actual = set()
    for path in vendor_root.rglob("*"):
        if path.is_symlink():
            raise ValueError(f"vendored_symlink path={path.relative_to(ci_root)}")
        if path.is_file():
            actual.add(path.relative_to(ci_root).as_posix())
        elif not path.is_dir():
            raise ValueError(f"unsupported_vendored_entry path={path.relative_to(ci_root)}")
    expected = set(EXPECTED_INPUTS)
    if actual != expected:
        missing = ",".join(sorted(expected - actual)) or "none"
        unlisted = ",".join(sorted(actual - expected)) or "none"
        raise ValueError(f"vendored_file_set_drift missing={missing} unlisted={unlisted}")

    for relative, record in EXPECTED_INPUTS.items():
        data = (ci_root / relative).read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        blob = git_blob_id(data)
        if digest != record["vendored_sha256"]:
            raise ValueError(f"vendored_sha256_drift path={relative} actual={digest}")
        if blob != record["vendored_git_blob"]:
            raise ValueError(f"vendored_git_blob_drift path={relative} actual={blob}")
        print(f"vendored_input=ok path={relative} sha256={digest} git_blob={blob}")
    print(
        f"vendored_inputs_status=ok files={len(expected)} "
        f"source_commit={AUTOMATION_COMMIT}"
    )


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path, required=True)
    args = parser.parse_args(argv)
    try:
        verify(args.root.resolve())
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"reason=vendored_inputs_invalid detail={str(exc)[:240]}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
