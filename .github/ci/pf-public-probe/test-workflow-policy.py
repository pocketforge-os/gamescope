#!/usr/bin/env python3
"""Lock the public probe job and the existing public-CI safety boundaries."""

import argparse
import pathlib
import sys


FORK_GUARD = """    if: >-
      github.event_name != 'pull_request' ||
      github.event.pull_request.head.repo.full_name == github.repository
"""
RUNNER_LABELS = "    runs-on: [self-hosted, pf-public-ci]\n"
CHECKOUT = "actions/checkout@08c6903cd8c0fde910a37f88322edcfb5dd907a8"


def job_block(workflow, job):
    lines = workflow.splitlines(keepends=True)
    marker = f"  {job}:\n"
    try:
        start = lines.index(marker)
    except ValueError as exc:
        raise ValueError(f"missing_job name={job}") from exc
    end = len(lines)
    for index in range(start + 1, len(lines)):
        line = lines[index]
        if line.startswith("  ") and not line.startswith("    ") and line.rstrip().endswith(":"):
            end = index
            break
    return "".join(lines[start:end])


def require(block, needle, reason):
    if needle not in block:
        raise ValueError(reason)


def verify(root):
    workflow = (root / ".github/workflows/main.yml").read_text()
    if "permissions:\n  contents: read\n" not in workflow:
        raise ValueError("workflow_contents_permission_not_read_only")

    probe = job_block(workflow, "pf-public-isolation-probe")
    require(probe, FORK_GUARD, "probe_fork_guard_drift")
    require(probe, RUNNER_LABELS, "probe_runner_labels_drift")
    require(probe, CHECKOUT, "probe_checkout_pin_drift")
    require(probe, "          persist-credentials: false\n", "probe_checkout_credentials_persisted")
    require(probe, "          submodules: false\n", "probe_submodules_not_disabled")
    require(probe, "test-vendored-inputs.py", "probe_vendor_drift_test_missing")
    require(probe, "test-workflow-policy.py", "probe_workflow_policy_test_missing")
    require(probe, "run-probe-and-controls.py", "probe_control_runner_missing")
    require(probe, "            --live\n", "probe_live_mode_missing")
    forbidden = (
        "    container:",
        "    permissions:",
        "secrets.",
        "pf-secret",
        "pocketforge-automation",
        "gh api",
        "http://",
        "https://",
    )
    for value in forbidden:
        if value in probe:
            raise ValueError(f"probe_forbidden_workflow_input value={value}")

    for name in ("native-tests", "aarch64-build"):
        existing = job_block(workflow, name)
        require(existing, FORK_GUARD, f"existing_fork_guard_drift job={name}")
        require(existing, RUNNER_LABELS, f"existing_runner_labels_drift job={name}")
        require(existing, "    container:", f"existing_container_missing job={name}")
        require(existing, CHECKOUT, f"existing_checkout_pin_drift job={name}")
        require(existing, "          submodules: recursive\n", f"existing_submodules_drift job={name}")
        require(existing, "Verify exact submodule checkout", f"existing_submodule_check_missing job={name}")
        require(existing, "--wrap-mode=nodownload", f"existing_nodownload_missing job={name}")

    print("pf_public_workflow_policy_status=ok jobs=3 fork_guards=3 permissions=contents-read")


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path, required=True)
    args = parser.parse_args(argv)
    try:
        verify(args.root.resolve())
    except (OSError, ValueError) as exc:
        print(f"reason=pf_public_workflow_policy_invalid detail={str(exc)[:240]}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
