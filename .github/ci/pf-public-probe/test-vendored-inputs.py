#!/usr/bin/env python3
"""Exercise positive and hostile drift cases for the vendored probe lock."""

import argparse
import pathlib
import shutil
import subprocess
import sys
import tempfile


CI_ROOT = pathlib.Path(".github/ci/pf-public-probe")
VERIFIER = CI_ROOT / "verify-vendored-inputs.py"


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

    with tempfile.TemporaryDirectory(prefix="pf-public-vendor-extra-") as temporary:
        fixture = pathlib.Path(temporary) / "repo"
        copy_fixture(root, fixture)
        (fixture / CI_ROOT / "vendor/unlisted-input").write_text("unlisted\n")
        require_failure(
            invoke(fixture),
            "unlisted=vendor/unlisted-input",
            "unlisted_input",
        )

    print("vendored_inputs_test_status=ok positive=1 negative_controls=2")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
