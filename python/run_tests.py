"""Run all Python tests under the python/test directory."""

import os
import sys
import subprocess


def _run_pytest(test_root: str) -> int:
    import pytest  # noqa: F401

    return subprocess.call([sys.executable, "-m", "pytest", test_root])


def _run_unittest(test_root: str) -> int:
    return subprocess.call([sys.executable, "-m", "unittest", "discover", "-s", test_root, "-p", "test_*.py"])


def main() -> int:
    script_dir = os.path.dirname(os.path.abspath(__file__))
    test_root = os.path.join(script_dir, "test")

    code = _run_pytest(test_root)
    if code == -1:
        print("pytest not found, falling back to unittest discovery.")
        code = _run_unittest(test_root)

    return code


if __name__ == "__main__":
    raise SystemExit(main())
