#!/usr/bin/env python3

from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

from release_common import verify_source_pins


ROOT = Path(__file__).resolve().parents[1]
RUNTIME_ROOT = ROOT.parent / "Kodosi"
SWIFT_ROOT = ROOT.parent / "kodosiSwift"
GHOSTTY_ROOT = ROOT.parent / "kodosi-ghostty"
MANIFEST_PATH = ROOT / "protocol" / "desktop-client-parity.json"
DEPENDENCIES_PATH = ROOT / "dependencies.lock.json"
HEADER_PATH = (
    SWIFT_ROOT
    / "Frameworks"
    / "KodosiKit.xcframework"
    / "Headers"
    / "kodosi_runtime.h"
)
CI_PATH = ROOT / ".github" / "workflows" / "ci.yml"
MANJARO_BUILD_PATH = ROOT / "scripts" / "build-manjaro-package.sh"

REQUIRED_FEATURES = {
    "app.lifecycle",
    "app.window-restoration",
    "app.shell",
    "app.commands-shortcuts",
    "bridge.runtime-handle",
    "bridge.event-lanes",
    "bridge.command-lanes",
    "bridge.codecs",
    "auth",
    "friends",
    "devices",
    "trust",
    "sessions.catalog-projection",
    "sessions.reconciliation",
    "sessions.creation",
    "sessions.resume",
    "sessions.close-delete",
    "sessions.action-availability",
    "sessions.sharing",
    "agent.global",
    "agent.intel",
    "agent.project-memory",
    "agent.custom-agents",
    "permissions.snapshot",
    "permissions.decision",
    "attention",
    "steering",
    "missions.directory",
    "missions.roster",
    "missions.invitations",
    "missions.chat",
    "missions.tasks",
    "missions.hydration",
    "missions.mutations",
    "terminal.subscription",
    "terminal.checkpoint",
    "terminal.raw-output",
    "terminal.control-frames",
    "terminal.input",
    "terminal.focus",
    "terminal.resize",
    "terminal.renderer-policy",
    "terminal.tiling",
    "settings",
    "design-system",
    "localization",
    "accessibility",
    "notifications.approval",
    "notifications.terminal",
    "deep-links",
    "file-picker",
    "editor-launch",
    "diagnostics",
    "logging",
    "release.runtime-pin",
    "release.renderer-pin",
    "release.native-notices",
    "release.bundled-cli",
    "release.signing-packaging",
}


def fail(message: str) -> None:
    raise SystemExit(f"parity error: {message}")


def load_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read {path}: {error}")


def git_head(path: Path) -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(path), "rev-parse", "HEAD"],
            text=True,
            stderr=subprocess.STDOUT,
        ).strip()
    except subprocess.CalledProcessError as error:
        fail(f"cannot inspect {path}: {error.output.strip()}")


def require_clean_checkout(path: Path, label: str) -> None:
    try:
        status = subprocess.check_output(
            [
                "git",
                "-C",
                str(path),
                "status",
                "--porcelain=v1",
                "--untracked-files=all",
            ],
            text=True,
            stderr=subprocess.STDOUT,
        )
    except subprocess.CalledProcessError as error:
        fail(f"cannot inspect {label} checkout: {error.output.strip()}")
    if status:
        fail(f"{label} checkout must be clean for immutable parity validation")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def macro_value(header: str, name: str) -> int:
    match = re.search(rf"^#define {re.escape(name)} ([0-9]+)$", header, re.MULTILINE)
    if not match:
        fail(f"{name} is missing from {HEADER_PATH}")
    return int(match.group(1))


def main() -> None:
    manifest = load_json(MANIFEST_PATH)
    dependencies = load_json(DEPENDENCIES_PATH)
    baseline = manifest.get("baseline", {})

    if manifest.get("schemaVersion") != 1:
        fail("unsupported desktop parity schema")
    if dependencies.get("schemaVersion") != 1:
        fail("unsupported dependency lock schema")
    require_clean_checkout(SWIFT_ROOT, "Swift")
    try:
        verify_source_pins(
            ROOT,
            RUNTIME_ROOT,
            GHOSTTY_ROOT,
            dependencies,
            require_client_clean=False,
        )
    except (OSError, ValueError) as error:
        fail(str(error))
    if git_head(RUNTIME_ROOT) != baseline.get("runtimeCommit"):
        fail("runtime checkout does not match the parity baseline")
    if git_head(SWIFT_ROOT) != baseline.get("swiftCommit"):
        fail("Swift checkout does not match the parity baseline")

    for name, authority in manifest.get("authorities", {}).items():
        path = RUNTIME_ROOT / authority["path"]
        if not path.is_file():
            fail(f"{name} authority is missing: {path}")
        actual = sha256(path)
        if actual != authority["sha256"]:
            fail(f"{name} authority digest changed: {actual}")

    feature_rows = manifest.get("features", [])
    feature_ids = [row.get("id") for row in feature_rows]
    if len(feature_ids) != len(set(feature_ids)):
        fail("feature IDs must be unique")
    if set(feature_ids) != REQUIRED_FEATURES:
        missing = sorted(REQUIRED_FEATURES - set(feature_ids))
        extra = sorted(set(feature_ids) - REQUIRED_FEATURES)
        fail(f"feature inventory mismatch; missing={missing}, extra={extra}")
    for row in feature_rows:
        if not row.get("owner") or not row.get("qtRole"):
            fail(f"feature {row.get('id')} lacks ownership metadata")
        if row["owner"] == "qt-authoritative":
            fail(f"feature {row['id']} makes Qt an authority")

    architecture = manifest.get("architecture", {})
    if architecture != {
        "runtimeAuthority": "rust",
        "qtRole": "presentation-adapter",
        "initialTransport": "ffi-abi-5",
        "qmlMayDecodeWireJson": False,
        "terminalBytesEnterQml": False,
        "checkpointAdmission": "synchronous",
    }:
        fail("architecture invariants changed")

    try:
        header = HEADER_PATH.read_text(encoding="utf-8")
    except OSError as error:
        fail(f"cannot read generated runtime header: {error}")
    if macro_value(header, "KODOSI_FFI_ABI_VERSION") != baseline["ffiAbiVersion"]:
        fail("generated header ABI does not match the baseline")
    if "uint32_t kodosi_protocol_version(void)" not in header:
        fail("generated header lacks protocol-version negotiation")

    locked_runtime = dependencies["kodosi"]
    if locked_runtime["commit"] != baseline["runtimeCommit"]:
        fail("dependency lock and parity runtime commits differ")
    if locked_runtime["ffiAbiVersion"] != baseline["ffiAbiVersion"]:
        fail("dependency lock and parity ABI versions differ")
    if locked_runtime["desktopProtocolVersion"] != baseline["desktopProtocolVersion"]:
        fail("dependency lock and parity protocol versions differ")
    try:
        workflow = CI_PATH.read_text(encoding="utf-8")
    except OSError as error:
        fail(f"cannot read Linux CI workflow: {error}")
    actions = dependencies["tools"]["githubActions"]
    required_ci_pins = {
        f"actions/checkout@{actions['checkout']['commit']}": "checkout action",
        f"actions/setup-python@{actions['setupPython']['commit']}": "setup-python action",
        f"astral-sh/setup-uv@{actions['setupUv']['commit']}": "setup-uv action",
        f"ref: {baseline['runtimeCommit']}": "runtime checkout",
        f"ref: {baseline['swiftCommit']}": "Swift checkout",
        f"ref: {dependencies['ghostty']['packageCommit']}": "Ghostty package checkout",
        f'version: "{dependencies["tools"]["uv"]["version"]}"': "uv tool",
        f'JUST_VERSION: "{dependencies["tools"]["just"]["version"]}"': "just tool version",
        dependencies["tools"]["just"]["sha256"]["linux-x86_64-musl"]:
            "just Linux artifact digest",
    }
    for pin, label in required_ci_pins.items():
        if pin not in workflow:
            fail(f"Linux CI {label} does not match dependencies.lock.json")

    try:
        manjaro_build = MANJARO_BUILD_PATH.read_text(encoding="utf-8")
    except OSError as error:
        fail(f"cannot read Manjaro package builder: {error}")
    arch_image = dependencies["packaging"]["archLinuxContainer"]
    expected_image = f"{arch_image['image']}@{arch_image['digest']}"
    if expected_image not in manjaro_build:
        fail("Manjaro builder image does not match dependencies.lock.json")
    desktop_build = (ROOT / "scripts" / "verify-linux-desktops.sh").read_text(
        encoding="utf-8"
    )
    ubuntu_image = dependencies["packaging"]["ubuntuContainer"]
    expected_ubuntu = f"{ubuntu_image['image']}@{ubuntu_image['digest']}"
    if expected_ubuntu not in desktop_build:
        fail("Linux desktop verifier image does not match dependencies.lock.json")

    print(
        f"parity ok: {len(feature_ids)} features, ABI "
        f"{baseline['ffiAbiVersion']}, protocol {baseline['desktopProtocolVersion']}"
    )


if __name__ == "__main__":
    main()
