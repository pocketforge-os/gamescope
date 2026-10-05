#!/usr/bin/env python3
"""Lock the public probe job and the existing public-CI safety boundaries."""

import argparse
import hashlib
import pathlib
import sys


FORK_GUARD = """    if: >-
      github.event_name != 'pull_request' ||
      github.event.pull_request.head.repo.full_name == github.repository
"""
RUNNER_LABELS = "    runs-on: [self-hosted, pf-public-ci]\n"
CHECKOUT = "actions/checkout@08c6903cd8c0fde910a37f88322edcfb5dd907a8"
PROBE_JOB_SHA256 = "36f645ca70f9be710f318658c3ddce260aee70d3160854cb593f985d49c2813d"


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


def step_blocks(job):
    lines = job.splitlines(keepends=True)
    starts = [index for index, line in enumerate(lines) if line.startswith("      - name:")]
    for position, start in enumerate(starts):
        end = starts[position + 1] if position + 1 < len(starts) else len(lines)
        yield "".join(lines[start:end])


def continued_meson_setups(step):
    command = []
    for line in step.splitlines():
        stripped = line.strip()
        if not command and "meson setup " not in stripped:
            continue
        command.append(stripped.removesuffix("\\").rstrip())
        if not stripped.endswith("\\"):
            yield " ".join(command)
            command = []
    if command:
        yield " ".join(command)


def verify_workflow(workflow):
    if "permissions:\n  contents: read\n" not in workflow:
        raise ValueError("workflow_contents_permission_not_read_only")

    probe = job_block(workflow, "pf-public-isolation-probe")
    if hashlib.sha256(probe.encode()).hexdigest() != PROBE_JOB_SHA256:
        raise ValueError("probe_job_bytes_drift")
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
        job_header = existing.split("    steps:\n", 1)[0]
        require(existing, FORK_GUARD, f"existing_fork_guard_drift job={name}")
        require(existing, RUNNER_LABELS, f"existing_runner_labels_drift job={name}")
        require(existing, "    container:", f"existing_container_missing job={name}")
        require(existing, CHECKOUT, f"existing_checkout_pin_drift job={name}")
        require(existing, "          persist-credentials: false\n", f"checkout_credentials_persisted job={name}")
        require(existing, "          submodules: false\n", f"source_checkout_not_gitlink_free job={name}")
        require(existing, "MESON_SOURCE_CACHE_ROOT:", f"source_cache_not_job_scoped job={name}")
        require(existing, "MATERIALIZED_SOURCE_ROOT:", f"materialized_root_not_job_scoped job={name}")
        require(existing, ".github/pocketforge-source-closure.tsv", f"closure_manifest_missing job={name}")
        require(existing, ".github/vendored-sources.tsv", f"vendored_registry_missing job={name}")
        require(existing, ".github/scripts/prime-meson-sources.sh", f"connected_admission_missing job={name}")
        require(existing, "PF_MESON_CACHE_OFFLINE=1", f"offline_admission_missing job={name}")
        require(existing, "verified_sources=32", f"complete_pin_count_missing job={name}")
        require(existing, "validated_edges=34", f"complete_edge_count_missing job={name}")
        require(existing, ".github/scripts/materialize-source-closure.py", f"materialization_missing job={name}")
        require(existing, "materialization-receipt.json", f"materialization_receipt_missing job={name}")
        if "${{ runner." in job_header:
            raise ValueError(f"runtime_context_used_in_job_definition job={name}")
        if not (
            existing.index("cold_result=")
            < existing.index("PF_MESON_CACHE_OFFLINE=1")
            < existing.index(".github/scripts/materialize-source-closure.py")
        ):
            raise ValueError(f"source_boundary_order_drift job={name}")
        for value in ("secrets.", "pf-secret"):
            if value in existing:
                raise ValueError(f"build_job_private_input value={value} job={name}")

        setups = []
        for step in step_blocks(existing):
            if any(command in step for command in ("meson setup ", "meson compile ", "meson test ")):
                if "working-directory:" not in step or "SOURCE_ROOT" not in step:
                    raise ValueError(f"meson_outside_materialized_tree job={name}")
            setups.extend(continued_meson_setups(step))
        if not setups:
            raise ValueError(f"meson_setup_missing job={name}")
        for setup in setups:
            if "--wrap-mode=nodownload" not in setup:
                raise ValueError(f"meson_setup_allows_download job={name}")

    forbidden_workflow_source = (
        ".github/meson-sources.lock",
        "subprojects/packagecache",
        "submodules: recursive",
        "git submodule",
        "rm " + "-rf",
    )
    for value in forbidden_workflow_source:
        if value in workflow:
            raise ValueError(f"legacy_source_path_present value={value}")

    native = job_block(workflow, "native-tests")
    for value in (
        "an incomplete offline cache was accepted",
        "a corrupted offline cache was accepted",
        "MISSING_SOURCE_ROOT:",
        "ERROR: Automatic wrap-based subproject downloading is disabled",
    ):
        require(native, value, f"source_negative_control_missing value={value}")

    print(
        "pf_public_workflow_policy_status=ok jobs=3 fork_guards=3 "
        "permissions=contents-read source_authority=materialized-closure"
    )


def verify(root):
    workflow = (root / ".github/workflows/main.yml").read_text()
    verify_workflow(workflow)

    mutations = {
        "legacy_lock": workflow + "\n# .github/meson-sources.lock\n",
        "materialization_omitted": workflow.replace(
            ".github/scripts/materialize-source-closure.py",
            ".github/scripts/materialize-missing.py",
        ),
        "build_outside_materialized_tree": workflow.replace(
            "        working-directory: ${{ env.MISSING_SOURCE_ROOT }}\n",
            "",
            1,
        ),
        "meson_download_allowed": workflow.replace(
            "            --wrap-mode=nodownload \\\n",
            "            --wrap-mode=default \\\n",
            1,
        ),
        "isolation_guard_drift": workflow.replace(
            RUNNER_LABELS,
            "    runs-on: ubuntu-latest\n",
            1,
        ),
        "runtime_context_in_job_definition": workflow.replace(
            "${{ github.workspace }}",
            "${{ runner.temp }}",
            1,
        ),
    }
    for name, mutated in mutations.items():
        try:
            verify_workflow(mutated)
        except (ValueError, OSError):
            print(f"pf_public_workflow_negative_control=ok name={name}")
        else:
            raise ValueError(f"workflow_negative_control_accepted name={name}")
    print(f"pf_public_workflow_test_status=ok positive=1 negative_controls={len(mutations)}")


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
