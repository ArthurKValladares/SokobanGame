#!/usr/bin/env python3
"""Run every Python asset regression suite without writing repository assets."""
from __future__ import annotations

import argparse
import importlib
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]


def check_dependencies() -> bool:
    if sys.version_info < (3, 11):
        print("Python 3.11 or newer is required.", file=sys.stderr)
        return False
    missing = []
    for name in ("numpy", "PIL"):
        try:
            importlib.import_module(name)
        except ImportError as error:
            missing.append(f"{name}: {error}")
    if missing:
        print("Missing Python asset-test dependencies: " + "; ".join(missing), file=sys.stderr)
        print(f'Install with "{sys.executable}" -m pip install -r '
              f'"{ROOT / "tools/requirements-python-tests.txt"}"', file=sys.stderr)
        return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check-dependencies", action="store_true",
                        help="Check interpreter/dependencies without discovering or running tests.")
    args = parser.parse_args()
    if not check_dependencies():
        return 2
    if args.check_dependencies:
        print("Python asset-test interpreter and dependencies are available.")
        return 0
    tests = ROOT / "tests"
    loader = unittest.TestLoader()
    suite = unittest.TestSuite()
    # Historical suites use PascalCase filenames; newer ones use unittest's
    # conventional prefix. Keep both conventions visible to one strict runner.
    filenames = {path.name for pattern in ("*Tests.py", "test_*.py")
                 for path in tests.glob(pattern)}
    for filename in sorted(filenames):
        suite.addTests(loader.discover(str(tests), pattern=filename))
    count = suite.countTestCases()
    if count == 0:
        print("No Python asset regression tests were discovered.", file=sys.stderr)
        return 2
    print(f"Discovered {count} Python asset regression tests.", flush=True)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
