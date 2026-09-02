#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
from pathlib import Path

from release_common import (
    canonical_json,
    source_identity,
    validate_source_identity,
)


def fail(message: str) -> None:
    raise SystemExit(f"source identity verification error: {message}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--identity", required=True, type=Path)
    parser.add_argument("--expected", type=Path)
    parser.add_argument("--client-root", type=Path)
    parser.add_argument("--runtime-root", type=Path)
    parser.add_argument("--ghostty-package-root", type=Path)
    parser.add_argument("--require-clean", action="store_true")
    arguments = parser.parse_args()
    try:
        payload = arguments.identity.read_bytes()
        value = json.loads(payload)
        identity = validate_source_identity(value)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        fail(str(error))
    if payload != canonical_json(identity):
        fail("identity is not canonical JSON")
    if arguments.expected is not None:
        try:
            expected = validate_source_identity(
                json.loads(arguments.expected.read_text(encoding="utf-8"))
            )
        except (OSError, ValueError, json.JSONDecodeError) as error:
            fail(f"expected identity is invalid: {error}")
        if identity != expected:
            fail("identity differs from the expected package build identity")
    roots = (
        arguments.client_root,
        arguments.runtime_root,
        arguments.ghostty_package_root,
    )
    if any(root is not None for root in roots):
        if not all(root is not None for root in roots):
            fail("all three source roots are required for measured verification")
        try:
            measured = source_identity(*roots)
        except (OSError, ValueError) as error:
            fail(str(error))
        if identity != measured:
            fail("identity differs from the measured source roots")
    if arguments.require_clean:
        dirty = [
            name
            for name in ("client", "runtime", "ghosttyPackage")
            if identity[name]["dirty"]
        ]
        if dirty:
            fail(f"identity records dirty sources: {', '.join(dirty)}")
    summary = ", ".join(
        f"{name}={identity[name]['repository']}@{identity[name]['commit']} "
        f"dirty={str(identity[name]['dirty']).lower()}"
        for name in ("client", "runtime", "ghosttyPackage")
    )
    print(f"source identity verified: {summary}")


if __name__ == "__main__":
    main()
