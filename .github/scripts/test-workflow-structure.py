#!/usr/bin/env python3
"""Check the container-job checkout boundary in the CI workflow."""

from pathlib import Path
import re
import sys


def main() -> int:
    workflow = Path(sys.argv[1] if len(sys.argv) > 1 else ".github/workflows/main.yml")
    text = workflow.read_text(encoding="utf-8")
    expected = re.compile(
        r"""      - name: Check out the tested revision and exact submodules
        uses: actions/checkout@[^\n]+
        with:
          submodules: recursive

      - name: Configure Git safe directory
        run: git config --global --add safe\.directory \"\$GITHUB_WORKSPACE\"

      - name: Verify exact submodule checkout
"""
    )

    jobs = re.findall(r"^  (native-tests|aarch64-build):\n(.*?)(?=^  [a-z][a-z0-9-]*:\n|\Z)", text, re.MULTILINE | re.DOTALL)
    if {name for name, _ in jobs} != {"native-tests", "aarch64-build"}:
        raise SystemExit("expected both container jobs")
    for name, body in jobs:
        if "container:" not in body:
            raise SystemExit(f"{name}: missing container")
        if not expected.search(body):
            raise SystemExit(f"{name}: checkout is not followed by the safe-directory step")
    print("workflow structure: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
