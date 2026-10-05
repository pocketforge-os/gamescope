#!/usr/bin/env python3
"""Exercise positive and hostile drift cases for the vendored probe lock."""

import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile


CI_ROOT = pathlib.Path(".github/ci/pf-public-probe")
VERIFIER = CI_ROOT / "verify-vendored-inputs.py"
MANIFEST = CI_ROOT / "vendored-inputs.json"
PROBE_INPUT = "vendor/pf-public-job-probe.py"
CONTRACT_INPUT = "vendor/runner-pool-probe-contract.json"
STALE_AUTOMATION_COMMIT = "078ee46be98e62fbc25c37b6cc5c0335236d7edf"
STALE_PROBE_GIT_BLOB = "4fc31a8918b0594da0315660235e187c281cc25e"
STALE_PROBE_SHA256 = "9a40527d91b12f98755d7e05a41653aaef0193d8042a00216a2dd4480eff12bf"
STALE_CONTRACT_GIT_BLOB = "4fe0262a41a75df8c006753cbf30e68016208087"
STALE_CONTRACT_SHA256 = "2b4e4069be3d346e3d1569d7381fdbeccfc6f843f30a4fc46515f92a97d4ea30"


def invoke(root):
    return subprocess.run(
        [sys.executable, str(root / VERIFIER), "--root", str(root)],
        check=False,
        capture_output=True,
        text=True,
    )


def copy_fixture(source, destination):
    target = destination / CI_ROOT
    target.parent.mkdir(parents=True)
    shutil.copytree(source / CI_ROOT, target)


def require_failure(result, needle, name):
    if result.returncode == 0 or needle not in result.stderr:
        raise RuntimeError(
            f"negative_control_failed name={name} rc={result.returncode} "
            f"stdout={result.stdout[:160]!r} stderr={result.stderr[:240]!r}"
        )
    print(f"vendored_inputs_negative_control=ok name={name}")


def manifest_control(root, name, mutate):
    with tempfile.TemporaryDirectory(prefix=f"pf-public-vendor-{name}-") as temporary:
        fixture = pathlib.Path(temporary) / "repo"
        copy_fixture(root, fixture)
        path = fixture / MANIFEST
        manifest = json.loads(path.read_text())
        mutate(manifest)
        path.write_text(json.dumps(manifest, indent=2) + "\n")
        require_failure(invoke(fixture), "provenance_manifest_drift", name)


def policy_v2_probe_bytes(policy_v3):
    """Reconstruct the stale v2 probe so the byte lock rejects it."""
    text = policy_v3.decode()
    policy_observations = (
        '        "rules": ip_json(["ip", "-json", "-4", "rule", "show"], '
        '"rule_collection_failed"),\n'
        '        "routes_all": ip_json(["ip", "-json", "-4", "route", "show", "table", "all"],\n'
        '                              "route_table_collection_failed"),\n'
    )
    if text.count(policy_observations) != 1:
        raise RuntimeError("policy_v2_reconstruction_observations_drift")
    text = text.replace(policy_observations, "")

    start = text.index("def ip_json(command, failure):")
    end = text.index("def link_facts(names):")
    text = text[:start] + text[end:]

    policy_evaluation = (
        "    route_ok = route_policy_ok(contract, observations) "
        "and policy_tables_ok(observations)\n"
    )
    if text.count(policy_evaluation) != 1:
        raise RuntimeError("policy_v2_reconstruction_evaluate_drift")
    return text.replace(
        policy_evaluation,
        "    route_ok = route_policy_ok(contract, observations)\n",
    ).encode()


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path, required=True)
    args = parser.parse_args(argv)
    root = args.root.resolve()

    positive = invoke(root)
    if positive.returncode:
        sys.stdout.write(positive.stdout)
        sys.stderr.write(positive.stderr)
        return positive.returncode
    sys.stdout.write(positive.stdout)

    manifest_control(
        root,
        "stale_v2_source_commit",
        lambda manifest: manifest.__setitem__("source_commit", STALE_AUTOMATION_COMMIT),
    )
    manifest_control(
        root,
        "stale_v2_probe_bytes_identity",
        lambda manifest: manifest["inputs"][PROBE_INPUT].update({
            "vendored_git_blob": STALE_PROBE_GIT_BLOB,
            "vendored_sha256": STALE_PROBE_SHA256,
        }),
    )
    manifest_control(
        root,
        "stale_v2_source_sha",
        lambda manifest: manifest["inputs"][PROBE_INPUT].__setitem__(
            "source_sha256", STALE_PROBE_SHA256
        ),
    )
    manifest_control(
        root,
        "manifest_hash_drift",
        lambda manifest: manifest["inputs"][PROBE_INPUT].__setitem__(
            "vendored_sha256", "0" * 64
        ),
    )
    manifest_control(
        root,
        "manifest_blob_drift",
        lambda manifest: manifest["inputs"][PROBE_INPUT].__setitem__(
            "vendored_git_blob", "0" * 40
        ),
    )

    with tempfile.TemporaryDirectory(prefix="pf-public-vendor-byte-") as temporary:
        fixture = pathlib.Path(temporary) / "repo"
        copy_fixture(root, fixture)
        probe = fixture / CI_ROOT / "vendor/pf-public-job-probe.py"
        probe.write_bytes(probe.read_bytes() + b"\n# byte drift\n")
        require_failure(
            invoke(fixture),
            "vendored_sha256_drift path=vendor/pf-public-job-probe.py",
            "byte_changed",
        )

    with tempfile.TemporaryDirectory(prefix="pf-public-vendor-stale-probe-") as temporary:
        fixture = pathlib.Path(temporary) / "repo"
        copy_fixture(root, fixture)
        probe = fixture / CI_ROOT / PROBE_INPUT
        stale_bytes = policy_v2_probe_bytes(probe.read_bytes())
        stale_sha256 = hashlib.sha256(stale_bytes).hexdigest()
        stale_blob = hashlib.sha1(
            f"blob {len(stale_bytes)}\0".encode() + stale_bytes
        ).hexdigest()
        if stale_sha256 != STALE_PROBE_SHA256 or stale_blob != STALE_PROBE_GIT_BLOB:
            raise RuntimeError(
                f"stale_probe_identity_mismatch sha256={stale_sha256} git_blob={stale_blob}"
            )
        probe.write_bytes(stale_bytes)
        require_failure(
            invoke(fixture),
            f"vendored_sha256_drift path={PROBE_INPUT}",
            "stale_v2_probe_bytes",
        )

    with tempfile.TemporaryDirectory(prefix="pf-public-vendor-stale-contract-") as temporary:
        fixture = pathlib.Path(temporary) / "repo"
        copy_fixture(root, fixture)
        contract_path = fixture / CI_ROOT / CONTRACT_INPUT
        contract = json.loads(contract_path.read_text())
        probe_contract = contract["network_policies"]["pf-public"]["probe_contract"]
        probe_contract["route_policy_version"] = 2
        stale_bytes = (json.dumps(contract, indent=2) + "\n").encode()
        stale_sha256 = hashlib.sha256(stale_bytes).hexdigest()
        stale_blob = hashlib.sha1(
            f"blob {len(stale_bytes)}\0".encode() + stale_bytes
        ).hexdigest()
        if stale_sha256 != STALE_CONTRACT_SHA256 or stale_blob != STALE_CONTRACT_GIT_BLOB:
            raise RuntimeError(
                f"stale_contract_identity_mismatch sha256={stale_sha256} "
                f"git_blob={stale_blob}"
            )
        contract_path.write_bytes(stale_bytes)
        require_failure(
            invoke(fixture),
            f"vendored_sha256_drift path={CONTRACT_INPUT}",
            "stale_v2_contract_bytes",
        )

    with tempfile.TemporaryDirectory(prefix="pf-public-vendor-extra-") as temporary:
        fixture = pathlib.Path(temporary) / "repo"
        copy_fixture(root, fixture)
        (fixture / CI_ROOT / "vendor/unlisted-input").write_text("unlisted\n")
        require_failure(
            invoke(fixture),
            "unlisted=vendor/unlisted-input",
            "unlisted_input",
        )

    print("vendored_inputs_test_status=ok positive=1 negative_controls=9")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
