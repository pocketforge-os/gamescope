#!/usr/bin/env python3
"""Verify the exact pf-public probe inputs committed to Gamescope."""

import argparse
import hashlib
import json
import pathlib
import sys


AUTOMATION_REPOSITORY = "pocketforge-os/pocketforge-automation"
AUTOMATION_COMMIT = "c7186c0ddb77e990e5ba636f3be5ad946b079cd3"
CI_ROOT = pathlib.Path(".github/ci/pf-public-probe")
EXPECTED_INPUTS = {
    "vendor/pf-public-job-probe.py": {
        "source_api_path": (
            "repos/pocketforge-os/pocketforge-automation/contents/scripts/"
            "pf-public-job-probe.py?ref=c7186c0ddb77e990e5ba636f3be5ad946b079cd3"
        ),
        "source_path": "scripts/pf-public-job-probe.py",
        "source_git_blob": "87d1b19adf3b09efacb6f58ee2bbac97a5ad0d59",
        "source_sha256": "966c0472e2e32d07471df094130e9cb13ee612ab7bb74dc5486f2b079b080bb7",
        "vendored_git_blob": "87d1b19adf3b09efacb6f58ee2bbac97a5ad0d59",
        "vendored_sha256": "966c0472e2e32d07471df094130e9cb13ee612ab7bb74dc5486f2b079b080bb7",
        "transform": "identity",
    },
    "vendor/runner-pool-probe-contract.json": {
        "source_api_path": (
            "repos/pocketforge-os/pocketforge-automation/contents/config/"
            "runner-pool.json?ref=c7186c0ddb77e990e5ba636f3be5ad946b079cd3"
        ),
        "source_path": "config/runner-pool.json",
        "source_git_blob": "cbe56db6bb2beb6c75a9379dc7951913f32a221b",
        "source_sha256": "4a341894e20be9f6eb844599788d574515b6f4b00508d79c06d1ad8d8cd0c0b3",
        "extraction": 'network_policies["pf-public"]["probe_contract"]',
        "vendored_git_blob": "233244d35a96878419efbfbef5ca73ec34201a54",
        "vendored_sha256": "091f093efb2acda88c398193c8edd694f57860b7281f31aca5960898afc462f6",
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
