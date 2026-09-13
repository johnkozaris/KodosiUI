#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
from pathlib import Path

from release_common import verify_source_pins


def fail(message: str) -> None:
    raise SystemExit(f"pinned checkout verification error: {message}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dependencies", required=True, type=Path)
    parser.add_argument("--client-root", type=Path)
    parser.add_argument("--runtime-root", required=True, type=Path)
    parser.add_argument("--ghostty-package-root", required=True, type=Path)
    arguments = parser.parse_args()
    try:
        dependencies = json.loads(
            arguments.dependencies.read_text(encoding="utf-8")
        )
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read dependency pins: {error}")
    client_root = arguments.client_root or Path(__file__).resolve().parents[2]
    try:
        _, ghostty = verify_source_pins(
            client_root,
            arguments.runtime_root,
            arguments.ghostty_package_root,
            dependencies,
            require_client_clean=False,
        )
    except (OSError, ValueError) as error:
        fail(str(error))
    print(
        "Pinned runtime and Ghostty package verified clean: "
        f"linux-upstream={ghostty['linuxUpstreamCommit']} "
        f"archive={ghostty['linuxVtArchiveSha256']}"
    )


if __name__ == "__main__":
    main()
