#!/usr/bin/env python3

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
QML = ROOT / "src" / "qml"
CONTROL_DIR = QML / "Controls"

RAW_CONTROL = re.compile(
    r"\b(Button|TextField|TextArea|ComboBox|SpinBox|Switch|CheckBox|"
    r"Slider|ScrollView|ItemDelegate|MenuItem|MenuSeparator|Menu|Dialog|"
    r"Popup|BusyIndicator)\s*\{"
)
COLOR_LITERAL = re.compile(r'#[0-9a-fA-F]{6,8}')
FORBIDDEN_UI_SOURCE = {
    "gradient": re.compile(
        r"\b(?:Gradient|LinearGradient|RadialGradient|ConicalGradient)\s*\{"
    ),
    "generic typeface": re.compile(
        r'(?:"|\b)(?:Inter|Space Grotesk|Instrument Serif)(?:"|\b)',
        re.IGNORECASE,
    ),
    "generic icon library": re.compile(r"\bLucide\b", re.IGNORECASE),
}
USER_VISIBLE_EM_DASH = re.compile(r'qsTr\("[^"]*—[^"]*"\)')
USER_VISIBLE_EMOJI = re.compile(
    r'qsTr\("[^"]*[\U0001F300-\U0001FAFF][^"]*"\)'
)


def fail(message: str) -> None:
    print(f"visual-parity: {message}", file=sys.stderr)
    raise SystemExit(1)


def theme_color(theme: str, palette: str, name: str) -> str:
    authored_name = palette + name[0].upper() + name[1:]
    match = re.search(
        rf'readonly property color {re.escape(authored_name)}: '
        rf'"(#[0-9a-fA-F]{{6}})"',
        theme,
    )
    if not match:
        fail(f"missing required {palette} color token {name!r}")
    return match.group(1)


def relative_luminance(color: str) -> float:
    channels = [int(color[index : index + 2], 16) / 255 for index in (1, 3, 5)]
    linear = [
        channel / 12.92
        if channel <= 0.04045
        else ((channel + 0.055) / 1.055) ** 2.4
        for channel in channels
    ]
    return 0.2126 * linear[0] + 0.7152 * linear[1] + 0.0722 * linear[2]


def contrast_ratio(foreground: str, background: str) -> float:
    lighter, darker = sorted(
        (relative_luminance(foreground), relative_luminance(background)),
        reverse=True,
    )
    return (lighter + 0.05) / (darker + 0.05)


def require_contrast(
    foreground: str,
    background: str,
    minimum: float,
    description: str,
) -> None:
    ratio = contrast_ratio(foreground, background)
    if ratio < minimum:
        fail(f"{description} contrast is {ratio:.2f}:1; expected at least {minimum:.1f}:1")


for path in sorted(QML.rglob("*.qml")):
    text = path.read_text()
    relative = path.relative_to(ROOT)
    if CONTROL_DIR not in path.parents and RAW_CONTROL.search(text):
        fail(f"raw Qt Quick control remains in {relative}")
    if path.name != "KodosiTheme.qml" and COLOR_LITERAL.search(text):
        fail(f"palette literal outside KodosiTheme.qml in {relative}")
    if re.search(r"\bborder\.(?:width|color)\s*:", text):
        fail(f"element outline remains in {relative}")
    if "Qt.rgba(" in text:
        fail(f"transparent color remains in {relative}")
    if path.name != "KIcon.qml" and '"transparent"' in text:
        fail(f"transparent surface remains in {relative}")
    for description, pattern in FORBIDDEN_UI_SOURCE.items():
        if pattern.search(text):
            fail(f"forbidden {description} detected in {relative}")
    if USER_VISIBLE_EM_DASH.search(text):
        fail(f"em dash detected in user-visible copy in {relative}")
    if USER_VISIBLE_EMOJI.search(text):
        fail(f"emoji detected in user-visible copy in {relative}")

ui_donts = (ROOT / "UI-DONTS.md").read_text()
for prohibition in (
    "Purple-to-blue gradients",
    "Gradient hero text",
    "Emojis in headings",
    "Inter used as the default font everywhere",
    "Colored-border cards",
    "Glassmorphism cards",
    "Low-contrast dark mode",
    "Repetitive rows of three icon boxes",
    "Badges above headlines",
    "Lucide icons used as the universal icon language",
    "Untouched shadcn UI",
    "Fade-in-on-scroll effects",
    "Cursor-following beams",
    "Buttons that fade on hover",
    "Inconsistent spacing",
    "Excessive em dashes",
    "Generic buzzword copy",
    "Decorative serif italic accents",
    "The Space Grotesk and Instrument Serif pairing",
    "Grain overlays on gradients",
    "Thin outlines around controls, cards, rows, dialogs, or panels",
    "Transparent or alpha-tinted UI surfaces",
    "Counts or badges inside navigation tabs",
    "Connection or Local status furniture in the top bar",
    "A separate Attention section in the session sidebar",
    "Helper labels, captions, and explanatory copy beside obvious controls",
    "Multiple Mission panes visible at the same time",
    "Controls added only to expose internal backend state",
    "Session rows with mode initials, counts, or repeated project metadata",
    "New UI patterns that do not exist in the shipping Swift client",
):
    if prohibition not in ui_donts:
        fail(f"missing binding UI prohibition {prohibition!r}")

theme = (QML / "Theme" / "KodosiTheme.qml").read_text()
for token in (
    "radiusSmall: 8",
    "radiusMedium: 10",
    "radiusLarge: 12",
    "radiusModal: 16",
    'darkFocusRing: "#ef9d75"',
    'lightFocusRing: "#944014"',
    "sidebarWidth: 272",
    "headerHeight: 56",
    "motionFast: reduceMotion ? 0 : motionFastAuthored",
    "motionNormal: reduceMotion ? 0 : motionNormalAuthored",
):
    if token not in theme:
        fail(f"missing required theme token {token!r}")

if re.search(r"#[0-9a-fA-F]{0,4}(?:00f|06f|07f|08f|09f|0af|0bf)", theme):
    fail("blue focus or selection literal detected in theme")

for palette in ("dark", "light"):
    on_danger = theme_color(theme, palette, "dangerForeground")
    for state in ("danger", "dangerHover", "dangerPressed"):
        require_contrast(
            on_danger,
            theme_color(theme, palette, state),
            4.5,
            f"{palette} onDanger against {state}",
        )
    require_contrast(
        theme_color(theme, palette, "accentForeground"),
        theme_color(theme, palette, "accent"),
        4.5,
        f"{palette} accent foreground against accent",
    )
    for state in ("accentHover", "accentPressed"):
        require_contrast(
            theme_color(theme, palette, "accentForeground"),
            theme_color(theme, palette, state),
            4.5,
            f"{palette} accent foreground against {state}",
        )
    require_contrast(
        theme_color(theme, palette, "placeholderText"),
        theme_color(theme, palette, "input"),
        4.5,
        f"{palette} placeholder text against input",
    )
    require_contrast(
        theme_color(theme, palette, "textPrimary"),
        theme_color(theme, palette, "canvas"),
        4.5,
        f"{palette} primary text against canvas",
    )
    require_contrast(
        theme_color(theme, palette, "textSecondary"),
        theme_color(theme, palette, "canvas"),
        4.5,
        f"{palette} secondary text against canvas",
    )
    require_contrast(
        theme_color(theme, palette, "textPrimary"),
        theme_color(theme, palette, "terminal"),
        4.5,
        f"{palette} primary text against terminal surface",
    )
    for role in ("textPrimary", "textSecondary", "textTertiary"):
        for surface in (
            "canvas",
            "surface",
            "surfaceRaised",
            "surfaceElevated",
            "input",
            "terminal",
        ):
            require_contrast(
                theme_color(theme, palette, role),
                theme_color(theme, palette, surface),
                4.5,
                f"{palette} {role} against {surface}",
            )
    for role in ("accent", "success", "warning", "danger", "reconnecting"):
        for surface in (
            "canvas",
            "surface",
            "surfaceRaised",
            "surfaceElevated",
            "surfaceSelected",
            "input",
        ):
            require_contrast(
                theme_color(theme, palette, role),
                theme_color(theme, palette, surface),
                4.5,
                f"{palette} {role} text against {surface}",
            )
    for surface in (
        "canvas",
        "surface",
        "surfaceRaised",
        "surfaceElevated",
        "input",
    ):
        require_contrast(
            theme_color(theme, palette, "controlBorder"),
            theme_color(theme, palette, surface),
            3.0,
            f"{palette} control border against {surface}",
        )

main = (QML / "Main.qml").read_text()
for contract in (
    "setShellAccessibilityIgnored(",
    "enabled: !window.blockingOverlayOpen",
    'objectName: "header.sidebar.toggle"',
    'objectName: "header.utility.menu"',
    'objectName: "header.logo"',
    'Accessible.name: qsTr("Kodosi")',
    'source: "assets/kodosi-logo-dark.png"',
    "Models.DesktopState.sidebarOpen = true\n                        "
    "sessionSidebar.createOpen = true",
):
    if contract not in main:
        fail(f"missing shell contract {contract!r}")
for removed in (
    'objectName: "header.settings"',
    'objectName: "header.attention"',
    'objectName: "header.tab.devices"',
    "badgeCount:",
    "StatusPill {",
):
    if removed in main:
        fail(f"removed shell noise returned: {removed!r}")

appearance_menu = (QML / "Appearance" / "AppearanceMenu.qml").read_text()
for contract in (
    'objectName: "panel.utility"',
    'objectName: "panel.utility.settings"',
    'objectName: "panel.utility.appearance.light"',
    'objectName: "panel.utility.appearance.dark"',
    'objectName: "panel.utility.appearance.system"',
    '"panel.utility.signOut"',
    "root.openSettingsRequested()",
    "root.signOutRequested()",
    "Models.Appearance.setPreference(",
):
    if contract not in appearance_menu:
        fail(f"missing appearance menu contract {contract!r}")

busy_indicator = (CONTROL_DIR / "KBusyIndicator.qml").read_text()
for contract in (
    "duration: KodosiTheme.motionSpinner",
    "running: root.running && !KodosiTheme.reduceMotion",
):
    if contract not in busy_indicator:
        fail(f"missing reduced-motion busy indicator contract {contract!r}")
for path in sorted(QML.rglob("*.qml")):
    text = path.read_text()
    if re.search(r"\bduration:\s*(?:900|[^(]*\*\s*120)\b", text):
        fail(f"scattered animation duration remains in {path.relative_to(ROOT)}")

desktop_state = (ROOT / "src" / "models" / "DesktopStateModel.cpp").read_text()
for contract in (
    "minimumWindowWidth = 820",
    "minimumWindowHeight = 560",
    "m_window->setMinimumSize(QSize(",
    "std::min(minimumWindowWidth, available.width())",
    "std::min(minimumWindowHeight, available.height())",
):
    if contract not in desktop_state:
        fail(f"missing native window-layout contract {contract!r}")

stage = (QML / "Workbench" / "TerminalStage.qml").read_text()
if 'objectName: "stage.empty.showSidebar"' not in stage:
    fail("missing collapsed-sidebar reopen affordance")
for contract in (
    "Models.TerminalTiling.contentHeight",
    "Models.DesktopState.stagedSessionIds",
    'qsTr("Terminal stage, %1")',
    'qsTr("Terminal grid, %1")',
    'qsTr("Scroll for more terminals")',
    'objectName: "stage.focus.exit"',
    'objectName: "stage.scrollbar"',
    'Accessible.name: qsTr("Terminal grid scroll bar")',
    "anchors.rightMargin: root.verticalOverflow",
    "prominent: true",
    "anchors.top: stageViewport.top",
    "function setTerminalSubtreeAccessibility(item, ignored)",
    'iconName: "chevron-left"',
    'qsTr("Start a session")',
    'qsTr("Open a terminal for Claude, Copilot, or your shell.")',
):
    if contract not in stage:
        fail(f"missing terminal Stage contract {contract!r}")

tile = (QML / "Workbench" / "TerminalTile.qml").read_text()
for contract in (
    "selectionBackground: KodosiTheme.accent",
    "selectionForeground: KodosiTheme.accentForeground",
    "preeditBackground: KodosiTheme.accent",
    "preeditForeground: KodosiTheme.accentForeground",
    '".terminal"',
    '".remove"',
    '".select"',
    '".focus"',
    '".overflow"',
    '".retry"',
    "width < 340 ? 0",
    "width < 640 ? 1 : 2",
    "terminalAccessibilityStatus",
    'qsTr("Terminal unavailable: %1")',
    "KodosiTheme.accentMuted",
):
    if contract not in tile:
        fail(f"missing terminal tile contract {contract!r}")

divider = (QML / "Workbench" / "TerminalDivider.qml").read_text()
for contract in (
    "Accessible.Slider",
    "Accessible.onIncreaseAction",
    "Accessible.onDecreaseAction",
    "root.applyAdjustment(24)",
    "root.applyAdjustment(-24)",
):
    if contract not in divider:
        fail(f"missing terminal divider contract {contract!r}")

settings = (QML / "Settings" / "SettingsDrawer.qml").read_text()
for category in ("terminal", "sessions", "supervision", "agents", "account"):
    if f'key: "{category}"' not in settings:
        fail(f"missing Settings category {category}")
for contract in (
    "function resetTerminalDraft()",
    'visible: root.selectedCategory === "terminal"',
    "root.resetTerminalDraft()",
    "enabled: root.agentIntelAvailable",
    'qsTr("No session selected")',
    "Accessible.selected: root.selectedCategory",
):
    if contract not in settings:
        fail(f"missing Settings behavior contract {contract!r}")
for contract in (
    "bottomRightRadius: KodosiTheme.radiusModal",
):
    if contract not in settings:
        fail(f"missing Settings footer contract {contract!r}")
if "if (Models.DesktopSettings.resetTerminal())\n                                root.resetDraft()" in settings:
    fail("Restore Defaults reloads non-terminal Settings drafts")

button = (CONTROL_DIR / "KButton.qml").read_text()
for contract in (
    "readonly property bool selected: checkable && checked",
    "KodosiTheme.surfaceSelected",
):
    if contract not in button:
        fail(f"missing checked-button contract {contract!r}")
for contract in (
    "KodosiTheme.dangerForeground",
    "KodosiTheme.dangerHover",
    "KodosiTheme.dangerPressed",
):
    if contract not in button:
        fail(f"missing danger button contract {contract!r}")

scroll_bar = (CONTROL_DIR / "KScrollBar.qml").read_text()
for contract in (
    "property bool prominent: false",
    "? (prominent ? 10 : 8)",
    "? KodosiTheme.textSecondary",
    "root.prominent\n            ? 1",
):
    if contract not in scroll_bar:
        fail(f"missing prominent scroll bar contract {contract!r}")

for control_name in ("KTextField.qml", "KTextArea.qml", "KPopover.qml"):
    control = (CONTROL_DIR / control_name).read_text()
    if "KodosiTheme.placeholderText" not in control:
        fail(f"{control_name} does not use the dedicated placeholder token")

agent_intel = (QML / "AgentIntel" / "AgentIntelDrawer.qml").read_text()
for contract in (
    "Qt.callLater(function()",
    "overviewTab.forceActiveFocus()",
    "closeButton.forceActiveFocus()",
    "bottomLeftRadius: Models.SessionActions.lastError.length === 0",
    "bottomRightRadius: KodosiTheme.radiusLarge",
):
    if contract not in agent_intel:
        fail(f"missing Agent Intelligence presentation contract {contract!r}")

diagnostics = (QML / "Diagnostics" / "DiagnosticsDrawer.qml").read_text()
for contract in (
    'objectName: "panel.diagnostics"',
    '"panel.diagnostics.runtime.collaborationCleanup"',
    "Models.RuntimeDiagnostics.protocolVersion",
    "Models.SessionActions.inactiveCleanupCount",
    "Models.AgentGlobal.mcpServers",
):
    if contract not in diagnostics:
        fail(f"missing Diagnostics presentation contract {contract!r}")

read_only_text = (CONTROL_DIR / "KReadOnlyText.qml").read_text()
for contract in (
    "readOnly: true",
    "selectionColor: KodosiTheme.accent",
    "selectedTextColor: KodosiTheme.accentForeground",
):
    if contract not in read_only_text:
        fail(f"missing read-only text contract {contract!r}")
for relative in (
    Path("AgentIntel/AgentIntelDrawer.qml"),
    Path("AgentIntel/AgentCustomAgentsSurface.qml"),
):
    source = (QML / relative).read_text()
    if re.search(r"\bTextEdit\s*\{", source):
        fail(f"raw selectable TextEdit remains in {relative}")

spin_box = (CONTROL_DIR / "KSpinBox.qml").read_text()
if spin_box.count("x: root.width - width") != 2:
    fail("KSpinBox arrows are not stacked in one right-side column")

sidebar = (QML / "Workbench" / "SessionSidebar.qml").read_text()
for action in (
    "sidebar.session.delete.confirm",
    "sidebar.session.leave.confirm",
    "sidebar.share.access.revoke.confirm",
):
    start = sidebar.find(f'objectName: "{action}"')
    if start < 0 or 'variant: "danger"' not in sidebar[start : start + 300]:
        fail(f"destructive confirmation is not danger-styled: {action}")
for action in (
    "sidebar.session.delete.cancel",
    "sidebar.session.leave.cancel",
    "sidebar.share.access.revoke.cancel",
):
    start = sidebar.find(f'objectName: "{action}"')
    end = sidebar.find("\n                KButton {", start + 1)
    block = sidebar[start : end if end >= 0 else start + 300]
    if start < 0 or 'variant: "danger"' in block:
        fail(f"confirmation cancel action is not neutral: {action}")

for relative, object_prefix, confirmation_flag in (
    (Path("Devices/DevicesView.qml"), "devices.revoke.", "device.confirming"),
):
    source = (QML / relative).read_text()
    start = source.find(f'objectName: "{object_prefix}"')
    if start < 0:
        fail(f"missing destructive action {object_prefix} in {relative}")
    block = source[start : start + 1000]
    expected = f'variant: {confirmation_flag}'
    if expected not in block or '? "danger"' not in block or ': "secondary"' not in block:
        fail(
            f"{object_prefix} must remain neutral before confirmation and danger on final confirmation"
        )

icons = (CONTROL_DIR / "KIcon.qml").read_text()
if "PathSvg" not in icons or 'case "terminal"' not in icons:
    fail("authored Shape icon system is incomplete")

print("visual-parity contracts passed")
