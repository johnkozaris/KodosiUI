#!/usr/bin/env python3

from dataclasses import dataclass
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
QML_ROOT = ROOT / "src/qml"

ACTIONABLE_TYPES = {
    "Button",
    "CheckBox",
    "ComboBox",
    "ItemDelegate",
    "MenuItem",
    "RadioButton",
    "Slider",
    "SpinBox",
    "Switch",
    "TerminalView",
    "TextArea",
    "TextField",
    "ToolButton",
}

ACCESSIBLE_ID_UNSUPPORTED_TYPES = {
    "ApplicationWindow",
    "Dialog",
    "KDialog",
    "KPopover",
    "Popup",
    "Repeater",
    "Shortcut",
}

BLOCK_START = re.compile(
    r"(?P<prefix>\b[A-Za-z_][A-Za-z0-9_.]*|delegate\s*:\s*[A-Za-z_][A-Za-z0-9_.]*)"
    r"\s*\{"
)
OBJECT_NAME = re.compile(r"\bobjectName\s*:")
ACCESSIBLE_ID = re.compile(r"\bAccessible\.id\s*:\s*objectName\b")


@dataclass(frozen=True)
class Block:
    type_name: str
    is_delegate: bool
    start: int
    end: int
    line: int
    body: str


def masked_source(source: str) -> str:
    result = list(source)
    index = 0
    while index < len(source):
        if source.startswith("//", index):
            end = source.find("\n", index)
            end = len(source) if end < 0 else end
            result[index:end] = " " * (end - index)
            index = end
        elif source.startswith("/*", index):
            end = source.find("*/", index + 2)
            end = len(source) if end < 0 else end + 2
            for offset in range(index, end):
                if result[offset] != "\n":
                    result[offset] = " "
            index = end
        elif source[index] in {'"', "'"}:
            quote = source[index]
            index += 1
            while index < len(source):
                if source[index] == "\\":
                    result[index] = " "
                    if index + 1 < len(source):
                        result[index + 1] = " "
                    index += 2
                elif source[index] == quote:
                    index += 1
                    break
                else:
                    if result[index] != "\n":
                        result[index] = " "
                    index += 1
        else:
            index += 1
    return "".join(result)


def direct_body(source: str, masked: str, start: int, end: int) -> str:
    body = list(source[start:end])
    depth = 0
    for offset, character in enumerate(masked[start:end]):
        if character == "{":
            depth += 1
        elif character == "}":
            depth -= 1
        elif depth > 0 and character != "\n":
            body[offset] = " "
    return "".join(body)


def blocks(source: str) -> list[Block]:
    masked = masked_source(source)
    result: list[Block] = []
    for match in BLOCK_START.finditer(masked):
        opening = match.end() - 1
        depth = 1
        cursor = opening + 1
        while cursor < len(masked) and depth > 0:
            if masked[cursor] == "{":
                depth += 1
            elif masked[cursor] == "}":
                depth -= 1
            cursor += 1
        if depth != 0:
            continue
        prefix = match.group("prefix")
        type_name = prefix.split(":")[-1].strip().split(".")[-1]
        result.append(
            Block(
                type_name=type_name,
                is_delegate=prefix.startswith("delegate"),
                start=opening + 1,
                end=cursor - 1,
                line=source.count("\n", 0, match.start()) + 1,
                body=direct_body(source, masked, opening + 1, cursor - 1),
            )
        )
    return result


def main() -> int:
    failures: list[str] = []
    qml_files = sorted(QML_ROOT.rglob("*.qml"))
    if not qml_files:
        failures.append("no QML files found")

    for path in qml_files:
        relative = path.relative_to(ROOT)
        is_control_definition = path.parent == QML_ROOT / "Controls"
        source = path.read_text(encoding="utf-8")
        file_blocks = blocks(source)
        for block in file_blocks:
            has_object_name = OBJECT_NAME.search(block.body) is not None
            has_accessible_id = ACCESSIBLE_ID.search(block.body) is not None
            if (
                block.type_name in ACTIONABLE_TYPES
                and not has_object_name
                and not is_control_definition
            ):
                failures.append(
                    f"{relative}:{block.line}: actionable {block.type_name} "
                    "has no objectName"
                )
            if (
                has_object_name
                and block.type_name not in ACCESSIBLE_ID_UNSUPPORTED_TYPES
                and not has_accessible_id
            ):
                failures.append(
                    f"{relative}:{block.line}: objectName does not bind "
                    "Accessible.id: objectName"
                )
            in_delegate = block.is_delegate or any(
                parent.is_delegate
                and parent.start <= block.start
                and block.end <= parent.end
                for parent in file_blocks
            )
            if (
                block.type_name in ACTIONABLE_TYPES
                and in_delegate
                and has_object_name
                and has_accessible_id
            ):
                object_name = OBJECT_NAME.search(block.body)
                accessible_id = ACCESSIBLE_ID.search(block.body)
                assert object_name is not None
                assert accessible_id is not None
                expression = block.body[
                    object_name.end() : accessible_id.start()
                ]
                if "+" not in expression:
                    failures.append(
                        f"{relative}:{block.line}: delegate "
                        f"{block.type_name} objectName is not row-unique"
                    )

    nav_tab = (QML_ROOT / "Controls/NavTab.qml").read_text(encoding="utf-8")
    if '"header.tab." + title' in nav_tab:
        failures.append("NavTab Accessible.id depends on translated title")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    print(
        f"Accessible.id audit passed "
        f"({len(qml_files)} QML files scanned)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
