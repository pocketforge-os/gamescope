#!/usr/bin/env python3
"""Run the pf-public probe and derive two fail-closed negative controls."""

import argparse
import copy
import json
import os
import pathlib
import subprocess
import sys
import tempfile


EXPECTED_ASSERTIONS = {
    "no_lan_route",
    "no_ssh_to_host_or_pool",
    "no_metadata_or_credential_endpoint",
    "gateway_dhcp",
    "gateway_dns",
    "public_egress",
}


def invoke(probe, config, source_arguments):
    return subprocess.run(
        [sys.executable, str(probe), "--config", str(config), *source_arguments],
        check=False,
        capture_output=True,
        text=True,
    )


def lines_with_prefix(output, prefix):
    return {
        line.removeprefix(prefix)
        for line in output.splitlines()
        if line.startswith(prefix)
    }


def emit(result):
    sys.stdout.write(result.stdout)
    sys.stdout.write(result.stderr)


def require_positive(result):
    assertions = lines_with_prefix(result.stdout, "public_probe_assertion=ok name=")
    if (
        result.returncode != 0
        or assertions != EXPECTED_ASSERTIONS
        or "public_probe_status=ok assertions=6" not in result.stdout
        or result.stderr
    ):
        emit(result)
        raise RuntimeError(
            f"positive_control_failed rc={result.returncode} "
            f"assertions={','.join(sorted(assertions))}"
        )


def require_negative(result, expected_failure, name):
    emit(result)
    failures = lines_with_prefix(result.stderr, "public_probe_failed name=")
    passed = lines_with_prefix(result.stdout, "public_probe_assertion=ok name=")
    if (
        result.returncode == 0
        or failures != {expected_failure}
        or passed != EXPECTED_ASSERTIONS - {expected_failure}
        or "public_probe_status=ok" in result.stdout
    ):
        raise RuntimeError(
            f"negative_control_failed name={name} rc={result.returncode} "
            f"failures={','.join(sorted(failures))}"
        )
    print(f"public_probe_negative_control=ok name={name} rejected={expected_failure}")


def live_observations(result):
    prefix = "public_probe_observations="
    records = [line.removeprefix(prefix) for line in result.stdout.splitlines() if line.startswith(prefix)]
    if len(records) != 1:
        raise RuntimeError(f"live_observation_count expected=1 actual={len(records)}")
    return json.loads(records[0])


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=pathlib.Path, required=True)
    parser.add_argument("--config", type=pathlib.Path, required=True)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--live", action="store_true")
    source.add_argument("--observations", type=pathlib.Path)
    args = parser.parse_args(argv)

    source_arguments = ["--live"] if args.live else ["--observations", str(args.observations)]
    baseline = invoke(args.probe, args.config, source_arguments)
    require_positive(baseline)
    emit(baseline)
    if args.live:
        observations = live_observations(baseline)
        print("public_probe_live_control=ok assertions=6")
    else:
        observations = json.loads(args.observations.read_text())
        print("public_probe_fixture_control=ok assertions=6")

    contract = json.loads(args.config.read_text())["network_policies"]["pf-public"]["probe_contract"]
    temporary_root = os.environ.get("RUNNER_TEMP")
    with tempfile.TemporaryDirectory(prefix="pf-public-probe-", dir=temporary_root) as temporary:
        temporary_path = pathlib.Path(temporary)

        lan_observations = copy.deepcopy(observations)
        lan_observations["routes"].append({
            "dst": "10.77.0.0/16",
            "gateway": contract["gateway_dns_dhcp"],
        })
        lan_path = temporary_path / "lan-route.json"
        lan_path.write_text(json.dumps(lan_observations, sort_keys=True) + "\n")
        require_negative(
            invoke(args.probe, args.config, ["--observations", str(lan_path)]),
            "no_lan_route",
            "injected_lan_route",
        )

        ssh_observations = copy.deepcopy(observations)
        ssh_target = next(
            endpoint["name"]
            for endpoint in contract["blocked_tcp_endpoints"]
            if endpoint["name"].endswith("-ssh")
        )
        ssh_observations["tcp_reachable"][ssh_target] = True
        ssh_path = temporary_path / "reachable-ssh.json"
        ssh_path.write_text(json.dumps(ssh_observations, sort_keys=True) + "\n")
        require_negative(
            invoke(args.probe, args.config, ["--observations", str(ssh_path)]),
            "no_ssh_to_host_or_pool",
            "injected_reachable_ssh",
        )

    if args.live:
        print("public_probe_ci_status=ok live_controls=1 negative_controls=2")
    else:
        print("public_probe_test_status=ok fixture_controls=1 negative_controls=2")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, StopIteration, ValueError, json.JSONDecodeError) as exc:
        print(f"reason=public_probe_controls_invalid detail={str(exc)[:240]}", file=sys.stderr)
        raise SystemExit(1)
