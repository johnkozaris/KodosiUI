#!/usr/bin/env bash

set -euo pipefail

cd "$(dirname "$0")/.."

probe=${KODOSI_UI_PROBE:-"$PWD/build/dev/src/kodosi-ui-probe"}
app=${KODOSI_QT_APP:-"$PWD/build/dev/src/kodosi-qt"}
artifact_dir=${KODOSI_UI_PROBE_ARTIFACT_DIR:-"$PWD/build/ui-probe-smoke"}
attention_mode=${KODOSI_UI_PROBE_ATTENTION_MODE:-empty}
mkdir -p "$artifact_dir"
config_dir="$artifact_dir/xdg-config.$$"
data_root="$artifact_dir/kodosi-data.$$"
production_data_root="$artifact_dir/kodosi-production-data.$$"
rm -rf -- "$config_dir" "$data_root" "$production_data_root"
mkdir -p "$config_dir" "$data_root" "$production_data_root"
app_pid=
pointer_button_down=false

stop_current_app() {
    if [[ -n "$app_pid" ]] && kill -0 "$app_pid" 2>/dev/null; then
        kill -TERM "$app_pid" 2>/dev/null || true
        for _ in $(seq 1 50); do
            kill -0 "$app_pid" 2>/dev/null || break
            sleep 0.1
        done
        if kill -0 "$app_pid" 2>/dev/null; then
            kill -KILL "$app_pid" 2>/dev/null || true
        fi
    fi
    if [[ -n "$app_pid" ]]; then
        wait "$app_pid" 2>/dev/null || true
    fi
    app_pid=
    app_handle=
}

cleanup() {
    status=$?
    trap - EXIT INT TERM
    if [[ "$pointer_button_down" == true ]]; then
        "$probe" button --button left --up --adapter atspi \
            >/dev/null 2>&1 || true
        pointer_button_down=false
    fi
    stop_current_app
    if [[ "$status" -ne 0 && -f "$artifact_dir/kodosi-qt.log" ]]; then
        cat "$artifact_dir/kodosi-qt.log" >&2
    fi
    if [[ "$status" -ne 0 && -f "$artifact_dir/kodosi-tiling.log" ]]; then
        cat "$artifact_dir/kodosi-tiling.log" >&2
    fi
    if [[ "$status" -ne 0 && -f "$artifact_dir/kodosi-ready.log" ]]; then
        cat "$artifact_dir/kodosi-ready.log" >&2
    fi
    rm -rf -- "$config_dir" "$data_root" "$production_data_root"
    exit "$status"
}
trap cleanup EXIT INT TERM

if [[ ! -x "$probe" || ! -x "$app" ]]; then
    echo "Build kodosi-ui-probe and kodosi-qt before running this smoke." >&2
    exit 2
fi
if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
    echo "The smoke requires the current graphical desktop session." >&2
    exit 3
fi
if [[ "$attention_mode" != empty && "$attention_mode" != populated ]]; then
    echo "KODOSI_UI_PROBE_ATTENTION_MODE must be empty or populated." >&2
    exit 2
fi

app_args=()
if [[ "$attention_mode" == populated ]]; then
    app_args+=(--ui-probe-attention-populated)
fi

json_value() {
    python3 -c '
import json, sys
value = json.load(sys.stdin)
for key in sys.argv[1].split("."):
    value = value[int(key)] if isinstance(value, list) else value[key]
print(value)
' "$1"
}

assert_shell_hidden_by_modal() {
    python3 -c '
import json, sys
tree = json.load(sys.stdin)
forbidden = {
    "app.header",
    "header.sidebar.toggle",
    "header.attention",
    "header.settings",
    "header.account",
    "sidebar.sessions",
    "stage",
}
ids = set()
def visit(value):
    if isinstance(value, dict):
        item_id = value.get("id")
        if isinstance(item_id, str) and item_id:
            ids.add(item_id)
        for child in value.values():
            visit(child)
    elif isinstance(value, list):
        for child in value:
            visit(child)
visit(tree)
leaked = sorted(ids & forbidden)
if leaked:
    raise SystemExit("underlying shell leaked into modal tree: " + ", ".join(leaked))
'
}

app_handle_for_pid() {
    python3 -c '
import json, sys
pid = int(sys.argv[1])
identity = sys.argv[2]
apps = json.load(sys.stdin)["applications"]
matches = [
    app["handle"]
    for app in apps
    if app.get("processId") == pid
    and (app.get("name") == identity or app.get("id") == identity)
]
if len(matches) != 1:
    raise SystemExit(1)
print(matches[0])
' "$1" "$2"
}

XDG_CONFIG_HOME="$config_dir" \
    KODOSI_DATA_ROOT="$data_root" \
    KODOSI_PRODUCTION_DATA_ROOT="$production_data_root" \
    QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 \
    "$app" "${app_args[@]}" >"$artifact_dir/kodosi-qt.log" 2>&1 &
app_pid=$!

window_ready=false
app_handle=
for _ in $(seq 1 50); do
    if "$probe" apps >"$artifact_dir/apps.json" 2>/dev/null; then
        app_handle=$(
            app_handle_for_pid "$app_pid" "Kodosi" \
                <"$artifact_dir/apps.json" || true
        )
    fi
    if [[ -n "$app_handle" ]] \
        && "$probe" wait --app "$app_handle" id=window.main \
        --state showing --timeout-ms 500 >"$artifact_dir/window.json" \
        2>"$artifact_dir/window.err"; then
        window_ready=true
        break
    fi
    kill -0 "$app_pid"
    sleep 0.1
done
if [[ "$window_ready" != true ]]; then
    echo "Kodosi did not become visible through AT-SPI." >&2
    exit 4
fi

"$probe" tree --app "$app_handle" --depth 2 \
    >"$artifact_dir/tree.json"
test "$(
    json_value app.processId <"$artifact_dir/tree.json"
)" = "$app_pid"

"$probe" find --app "$app_handle" --id header.attention \
    >"$artifact_dir/attention-button.json"
attention_handle=$(
    json_value matches.0.handle <"$artifact_dir/attention-button.json"
)
"$probe" click "$attention_handle" >"$artifact_dir/attention-click.json"
"$probe" wait --app "$app_handle" id=panel.attention \
    --state showing --timeout-ms 5000 >"$artifact_dir/attention-panel.json"
"$probe" tree --app "$app_handle" --depth 5 \
    >"$artifact_dir/attention-tree.json"
assert_shell_hidden_by_modal <"$artifact_dir/attention-tree.json"
if [[ "$attention_mode" == populated ]]; then
    "$probe" wait --app "$app_handle" id=panel.attention.list \
        --state showing --timeout-ms 5000 \
        >"$artifact_dir/attention-list.json"
    "$probe" wait --app "$app_handle" id=sidebar.attention.rail \
        --state showing --timeout-ms 5000 \
        >"$artifact_dir/attention-rail.json"
else
    "$probe" wait --app "$app_handle" id=panel.attention.empty \
        --state showing --timeout-ms 5000 \
        >"$artifact_dir/attention-empty.json"
fi
"$probe" find --app "$app_handle" --id panel.attention.close \
    >"$artifact_dir/attention-close.json"
attention_close_handle=$(
    json_value matches.0.handle <"$artifact_dir/attention-close.json"
)
"$probe" click "$attention_close_handle" \
    >"$artifact_dir/attention-close-click.json"

"$probe" find --app "$app_handle" --id header.settings \
    >"$artifact_dir/settings-button.json"
settings_handle=$(
    json_value matches.0.handle <"$artifact_dir/settings-button.json"
)
"$probe" click "$settings_handle" >"$artifact_dir/settings-click.json"
"$probe" wait --app "$app_handle" id=panel.settings \
    --state showing --timeout-ms 5000 >"$artifact_dir/settings-panel.json"
"$probe" tree --app "$app_handle" --depth 6 \
    >"$artifact_dir/settings-tree.json"
assert_shell_hidden_by_modal <"$artifact_dir/settings-tree.json"

"$probe" find --app "$app_handle" \
    --id panel.settings.terminal.fontFamily \
    >"$artifact_dir/font-family.json"
field_handle=$(
    json_value matches.0.handle <"$artifact_dir/font-family.json"
)
"$probe" focus "$field_handle" >"$artifact_dir/font-family-focus.json"
"$probe" set-text "$field_handle" --text "Monospace" \
    >"$artifact_dir/font-family-set.json"
test "$(
    json_value readback <"$artifact_dir/font-family-set.json"
)" = "Monospace"

for category in sessions supervision agents account; do
"$probe" find --app "$app_handle" --id "panel.settings.tab.$category" \
    >"$artifact_dir/settings-tab-$category.json"
category_handle=$(
    json_value matches.0.handle \
        <"$artifact_dir/settings-tab-$category.json"
)
"$probe" click "$category_handle" \
    >"$artifact_dir/settings-tab-$category-click.json"
if [[ "$category" == sessions ]]; then
    "$probe" find --app "$app_handle" \
        --id panel.settings.sessions.browse \
        >"$artifact_dir/settings-sessions-browse.json"
    "$probe" find --app "$app_handle" \
        --id panel.settings.sessions.openFolder \
        >"$artifact_dir/settings-sessions-open-folder.json"
fi
"$probe" inspect "$category_handle" \
    >"$artifact_dir/settings-tab-$category-inspect.json"
python3 -c '
import json, sys
states = json.load(sys.stdin)["element"]["states"]
if "selected" not in states:
    raise SystemExit("active Settings category is not exposed as selected")
' <"$artifact_dir/settings-tab-$category-inspect.json"
if [[ "$category" == agents ]]; then
    "$probe" find --app "$app_handle" \
        --id panel.settings.agents.openIntel \
        >"$artifact_dir/settings-agents-action.json"
    agents_action_handle=$(
        json_value matches.0.handle \
            <"$artifact_dir/settings-agents-action.json"
    )
    "$probe" inspect "$agents_action_handle" \
        >"$artifact_dir/settings-agents-action-inspect.json"
    python3 -c '
import json, sys
states = json.load(sys.stdin)["element"]["states"]
if "enabled" in states or "sensitive" in states:
    raise SystemExit("Agent Intel action is enabled without a selected session")
' <"$artifact_dir/settings-agents-action-inspect.json"
    "$probe" wait --app "$app_handle" id=panel.settings \
        --state showing --timeout-ms 5000 \
        >"$artifact_dir/settings-agents-modal-preserved.json"
fi
done

"$probe" wait --app "$app_handle" \
id=panel.settings.account.openDevices \
--state showing --timeout-ms 5000 \
>"$artifact_dir/settings-account-devices.json"

"$probe" find --app "$app_handle" --id panel.settings.tab.terminal \
>"$artifact_dir/settings-tab-terminal.json"
terminal_tab_handle=$(
json_value matches.0.handle \
    <"$artifact_dir/settings-tab-terminal.json"
)
"$probe" click "$terminal_tab_handle" \
>"$artifact_dir/settings-tab-terminal-click.json"
"$probe" find --app "$app_handle" \
--id panel.settings.terminal.fontFamily \
>"$artifact_dir/font-family-returned.json"
field_handle=$(
json_value matches.0.handle <"$artifact_dir/font-family-returned.json"
)
"$probe" focus "$field_handle" \
>"$artifact_dir/font-family-returned-focus.json"

"$probe" input-status >"$artifact_dir/input-status.json"
if [[ "${XDG_SESSION_TYPE:-}" == wayland \
    || -n "${WAYLAND_DISPLAY:-}" ]]; then
    test "$(
        json_value portalSecurityAuthority <"$artifact_dir/input-status.json"
    )" = "True"
else
    test "$(
        json_value atspi.available <"$artifact_dir/input-status.json"
    )" = "True"
    "$probe" shortcut --keys Ctrl+A --adapter atspi \
        >"$artifact_dir/font-family-select-all.json"
    for key in K o d o s i M o n o; do
        "$probe" key --key "$key" --adapter atspi \
            >>"$artifact_dir/font-family-raw-keys.jsonl"
    done
    "$probe" inspect "$field_handle" \
        >"$artifact_dir/font-family-raw-readback.json"
    test "$(
        json_value element.text.content \
            <"$artifact_dir/font-family-raw-readback.json"
    )" = "KodosiMono"
fi

"$probe" find --app "$app_handle" --id panel.settings.tab.agents \
    >"$artifact_dir/settings-tab-agents-diagnostics.json"
agents_tab_handle=$(
    json_value matches.0.handle \
        <"$artifact_dir/settings-tab-agents-diagnostics.json"
)
"$probe" click "$agents_tab_handle" \
    >"$artifact_dir/settings-tab-agents-diagnostics-click.json"
"$probe" find --app "$app_handle" \
    --id panel.settings.agents.openDiagnostics \
    >"$artifact_dir/settings-diagnostics-action.json"
diagnostics_action_handle=$(
    json_value matches.0.handle \
        <"$artifact_dir/settings-diagnostics-action.json"
)
"$probe" click "$diagnostics_action_handle" \
    >"$artifact_dir/settings-diagnostics-action-click.json"
"$probe" wait --app "$app_handle" id=panel.diagnostics \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/diagnostics-panel.json"
"$probe" tree --app "$app_handle" --depth 6 \
    >"$artifact_dir/diagnostics-tree.json"
assert_shell_hidden_by_modal <"$artifact_dir/diagnostics-tree.json"
"$probe" find --app "$app_handle" \
    --id panel.diagnostics.runtime.collaborationCleanup \
    >"$artifact_dir/diagnostics-cleanup.json"
"$probe" find --app "$app_handle" --id panel.diagnostics.close \
    >"$artifact_dir/diagnostics-close.json"
diagnostics_close_handle=$(
    json_value matches.0.handle <"$artifact_dir/diagnostics-close.json"
)
"$probe" inspect "$diagnostics_close_handle" \
    >"$artifact_dir/diagnostics-close-inspect.json"
python3 -c '
import json, sys
states = json.load(sys.stdin)["element"]["states"]
if "focused" not in states:
    raise SystemExit("Diagnostics did not establish initial focus")
' <"$artifact_dir/diagnostics-close-inspect.json"
"$probe" click "$diagnostics_close_handle" \
    >"$artifact_dir/diagnostics-close-click.json"

"$probe" find --app "$app_handle" --id header.sidebar.toggle \
    >"$artifact_dir/sidebar-toggle.json"
sidebar_toggle_handle=$(
    json_value matches.0.handle <"$artifact_dir/sidebar-toggle.json"
)
"$probe" click "$sidebar_toggle_handle" \
    >"$artifact_dir/sidebar-collapse-click.json"
"$probe" wait --app "$app_handle" id=header.sidebar.toggle \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/sidebar-reopen.json"
"$probe" tree --app "$app_handle" --depth 6 \
    >"$artifact_dir/sidebar-collapsed-tree.json"
python3 -c '
import json, sys
tree = json.load(sys.stdin)
ids = set()
def visit(value):
    if isinstance(value, dict):
        item_id = value.get("id")
        if isinstance(item_id, str) and item_id:
            ids.add(item_id)
        for child in value.values():
            visit(child)
    elif isinstance(value, list):
        for child in value:
            visit(child)
visit(tree)
if "sidebar.sessions" in ids:
    raise SystemExit("sidebar remains exposed after collapse")
' <"$artifact_dir/sidebar-collapsed-tree.json"
"$probe" find --app "$app_handle" --id stage.empty.new \
    >"$artifact_dir/collapsed-new-session.json"
collapsed_new_session_handle=$(
    json_value matches.0.handle <"$artifact_dir/collapsed-new-session.json"
)
"$probe" click "$collapsed_new_session_handle" \
    >"$artifact_dir/collapsed-new-session-click.json"
"$probe" wait --app "$app_handle" id=session.create.name \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/collapsed-new-session-form.json"
"$probe" find --app "$app_handle" --id session.create.cancel \
    >"$artifact_dir/collapsed-new-session-cancel.json"
create_cancel_handle=$(
    json_value matches.0.handle \
        <"$artifact_dir/collapsed-new-session-cancel.json"
)
"$probe" click "$create_cancel_handle" \
    >"$artifact_dir/collapsed-new-session-cancel-click.json"
"$probe" click "$sidebar_toggle_handle" \
    >"$artifact_dir/sidebar-second-collapse-click.json"
"$probe" click "$sidebar_toggle_handle" \
    >"$artifact_dir/sidebar-expand-click.json"
"$probe" wait --app "$app_handle" id=sidebar.sessions \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/sidebar-expanded.json"

"$probe" find --app "$app_handle" --id header.tab.devices \
    >"$artifact_dir/devices-button.json"
devices_handle=$(
    json_value matches.0.handle <"$artifact_dir/devices-button.json"
)
"$probe" click "$devices_handle" >"$artifact_dir/devices-click.json"
"$probe" wait --app "$app_handle" id=surface.devices \
    --state showing --timeout-ms 5000 >"$artifact_dir/devices-surface.json"

"$probe" find --app "$app_handle" --id header.tab.missions \
    >"$artifact_dir/missions-button.json"
missions_handle=$(
    json_value matches.0.handle <"$artifact_dir/missions-button.json"
)
"$probe" click "$missions_handle" >"$artifact_dir/missions-click.json"
"$probe" wait --app "$app_handle" id=surface.missions \
    --state showing --timeout-ms 5000 >"$artifact_dir/missions-surface.json"

"$probe" find --app "$app_handle" --id header.tab.my-agents \
    >"$artifact_dir/agents-button.json"
agents_handle=$(
    json_value matches.0.handle <"$artifact_dir/agents-button.json"
)
"$probe" click "$agents_handle" >"$artifact_dir/agents-click.json"

"$probe" doctor >"$artifact_dir/doctor.json"
if [[ "${KODOSI_UI_PROBE_SKIP_INTERACTIVE_PORTALS:-0}" != 1 \
    && "$(
    json_value portal.screenshotAvailable <"$artifact_dir/doctor.json"
)" = "True" ]]; then
    set +e
    "$probe" screenshot \
        --output "$artifact_dir/kodosi.png" \
        --interactive \
        --timeout-ms 15000 \
        >"$artifact_dir/screenshot.json" \
        2>"$artifact_dir/screenshot.err"
    screenshot_status=$?
    set -e
    if [[ "$screenshot_status" -ne 0 \
        && "$screenshot_status" -ne 6 \
        && "$screenshot_status" -ne 8 \
        && "$screenshot_status" -ne 10 ]]; then
        exit "$screenshot_status"
    fi
fi

stop_current_app
XDG_CONFIG_HOME="$config_dir" \
    KODOSI_DATA_ROOT="$data_root" \
    KODOSI_PRODUCTION_DATA_ROOT="$production_data_root" \
    QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 \
    "$app" --ui-probe-tiling-synthetic --window-size 1100x700 \
    >"$artifact_dir/kodosi-tiling.log" 2>&1 &
app_pid=$!

tiling_ready=false
for _ in $(seq 1 10); do
    if "$probe" apps >"$artifact_dir/tiling-apps.json" 2>/dev/null; then
        app_handle=$(
            app_handle_for_pid "$app_pid" "Kodosi" \
                <"$artifact_dir/tiling-apps.json" || true
        )
    fi
    if [[ -n "$app_handle" ]] \
        && "$probe" wait --app "$app_handle" \
        id=stage.tile.tiling-0 \
        --state showing --timeout-ms 5000 \
        >"$artifact_dir/tiling-ready.json" \
        2>"$artifact_dir/tiling-ready.err"; then
        tiling_ready=true
        break
    fi
    kill -0 "$app_pid"
    sleep 0.1
done
if [[ "$tiling_ready" != true ]]; then
    echo "Synthetic terminal tiling did not become accessible." >&2
    exit 4
fi

"$probe" tree --app "$app_handle" --depth 8 \
    >"$artifact_dir/tiling-tree.json"
test "$(
    json_value app.processId <"$artifact_dir/tiling-tree.json"
)" = "$app_pid"
python3 -c '
import json, sys
tree = json.load(sys.stdin)
ids = set()
def visit(value):
    if isinstance(value, dict):
        item_id = value.get("id")
        if isinstance(item_id, str) and item_id:
            ids.add(item_id)
        for child in value.values():
            visit(child)
    elif isinstance(value, list):
        for child in value:
            visit(child)
visit(tree)
expected = {f"stage.tile.tiling-{index}" for index in range(4)}
missing = sorted(expected - ids)
if missing:
    raise SystemExit("tiling tiles missing from accessibility tree: " + ", ".join(missing))
' <"$artifact_dir/tiling-tree.json"

"$probe" find --app "$app_handle" --id stage.tile.tiling-2.select \
    >"$artifact_dir/tiling-select.json"
tiling_select_handle=$(
    json_value matches.0.handle <"$artifact_dir/tiling-select.json"
)
"$probe" click "$tiling_select_handle" \
    >"$artifact_dir/tiling-select-click.json"
"$probe" inspect "$tiling_select_handle" \
    >"$artifact_dir/tiling-select-inspect.json"
python3 -c '
import json, sys
states = json.load(sys.stdin)["element"]["states"]
if "selected" not in states:
    raise SystemExit("selected terminal did not expose selected state")
' <"$artifact_dir/tiling-select-inspect.json"

"$probe" find --app "$app_handle" --id stage.tile.tiling-2.focus \
    >"$artifact_dir/tiling-focus.json"
tiling_focus_handle=$(
    json_value matches.0.handle <"$artifact_dir/tiling-focus.json"
)
"$probe" click "$tiling_focus_handle" \
    >"$artifact_dir/tiling-focus-click.json"
"$probe" wait --app "$app_handle" id=stage.focus.exit \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/tiling-focus-exit.json"
python3 -c '
import json, sys
element = json.load(sys.stdin)["element"]
if element.get("name") != "Return to terminal grid":
    raise SystemExit("focus exit has the wrong accessible name")
' <"$artifact_dir/tiling-focus-exit.json"
"$probe" find --app "$app_handle" --id stage.focus.title \
    >"$artifact_dir/tiling-focus-title.json"
python3 -c '
import json, sys
name = json.load(sys.stdin)["matches"][0]["name"]
if name != "Tiling 2":
    raise SystemExit("focus header does not name the selected session")
' <"$artifact_dir/tiling-focus-title.json"
"$probe" tree --app "$app_handle" --depth 12 \
    >"$artifact_dir/tiling-focus-tree.json"
python3 -c '
import json, re, sys
tree = json.load(sys.stdin)
ids = set()
descriptions = {}
def visit(value):
    if isinstance(value, dict):
        item_id = value.get("id")
        if isinstance(item_id, str) and item_id:
            ids.add(item_id)
            descriptions[item_id] = value.get("description")
        for child in value.values():
            visit(child)
    elif isinstance(value, list):
        for child in value:
            visit(child)
visit(tree)
tile_ids = sorted(
    item_id for item_id in ids
    if re.fullmatch(r"stage\.tile\.tiling-\d+", item_id)
)
if tile_ids != ["stage.tile.tiling-2"]:
    raise SystemExit(
        "off-focus terminal tiles leaked into accessibility tree: "
        + ", ".join(tile_ids)
    )
if descriptions.get("stage.tile.tiling-2") != "Connecting terminal":
    raise SystemExit("connecting terminal status was not announced")
' <"$artifact_dir/tiling-focus-tree.json"
tiling_exit_handle=$(
    json_value element.handle <"$artifact_dir/tiling-focus-exit.json"
)
"$probe" click "$tiling_exit_handle" \
    >"$artifact_dir/tiling-focus-exit-click.json"
"$probe" wait --app "$app_handle" id=stage.tile.tiling-0 \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/tiling-grid-restored.json"

"$probe" find --app "$app_handle" \
    --id stage.divider.row.0.column.1 \
    >"$artifact_dir/tiling-divider.json"
tiling_divider_handle=$(
    json_value matches.0.handle <"$artifact_dir/tiling-divider.json"
)
"$probe" inspect "$tiling_divider_handle" \
    >"$artifact_dir/tiling-divider-before.json"
python3 -c '
import json, sys
element = json.load(sys.stdin)["element"]
if element.get("role") != "slider":
    raise SystemExit("terminal divider is not exposed as a slider")
value = element.get("value")
if not isinstance(value, dict) or not (0 <= value.get("current", -1) <= 100):
    raise SystemExit("terminal divider has no bounded accessible percentage")
if value.get("minimum") != 0 or value.get("maximum") != 100 or value.get("increment") != 1:
    raise SystemExit("terminal divider has incomplete AT-SPI Value bounds")
' <"$artifact_dir/tiling-divider-before.json"

if [[ "${XDG_SESSION_TYPE:-}" != wayland \
    && -z "${WAYLAND_DISPLAY:-}" ]]; then
    divider_before=$(
        json_value element.value.current \
            <"$artifact_dir/tiling-divider-before.json"
    )
    "$probe" focus "$tiling_divider_handle" \
        >"$artifact_dir/tiling-divider-focus.json"
    "$probe" key --key Right --adapter atspi \
        >"$artifact_dir/tiling-divider-right.json"
    "$probe" inspect "$tiling_divider_handle" \
        >"$artifact_dir/tiling-divider-after.json"
    divider_after=$(
        json_value element.value.current \
            <"$artifact_dir/tiling-divider-after.json"
    )
    python3 -c '
import sys
if float(sys.argv[2]) <= float(sys.argv[1]):
    raise SystemExit("terminal divider did not resize after keyboard adjustment")
' "$divider_before" "$divider_after"

    read -r divider_x divider_y divider_width divider_height < <(
        python3 -c '
import json, sys
bounds = json.load(sys.stdin)["element"]["bounds"]
print(
    round(bounds["x"]),
    round(bounds["y"]),
    round(bounds["width"]),
    round(bounds["height"]),
)
' <"$artifact_dir/tiling-divider-after.json"
    )
    drag_from_x=$((divider_x + divider_width / 2))
    drag_from_y=$((divider_y + divider_height / 2))
    drag_to_x=$((drag_from_x + 48))
    "$probe" drag \
        --from-x "$drag_from_x" \
        --from-y "$drag_from_y" \
        --to-x "$drag_to_x" \
        --to-y "$drag_from_y" \
        --duration-ms 250 \
        --adapter atspi \
        >"$artifact_dir/tiling-divider-drag.json"
    "$probe" inspect "$tiling_divider_handle" \
        >"$artifact_dir/tiling-divider-after-drag.json"
    divider_after_drag=$(
        json_value element.value.current \
            <"$artifact_dir/tiling-divider-after-drag.json"
    )
    python3 -c '
import sys
if float(sys.argv[2]) <= float(sys.argv[1]):
    raise SystemExit("terminal divider drag did not use stable cumulative coordinates")
' "$divider_after" "$divider_after_drag"

    "$probe" inspect "$tiling_divider_handle" \
        >"$artifact_dir/tiling-divider-before-clamp.json"
    read -r divider_x divider_y divider_width divider_height < <(
        python3 -c '
import json, sys
bounds = json.load(sys.stdin)["element"]["bounds"]
print(
    round(bounds["x"]),
    round(bounds["y"]),
    round(bounds["width"]),
    round(bounds["height"]),
)
' <"$artifact_dir/tiling-divider-before-clamp.json"
    )
    clamp_y=$((divider_y + divider_height / 2))
    clamp_start_x=$((divider_x + divider_width / 2))
    clamp_pointer_x=$((clamp_start_x + 1000))
    "$probe" pointer --x "$clamp_start_x" --y "$clamp_y" \
        --adapter atspi >"$artifact_dir/tiling-divider-clamp-start.json"
    "$probe" button --button left --down --adapter atspi \
        >"$artifact_dir/tiling-divider-clamp-down.json"
    pointer_button_down=true
    "$probe" pointer --x "$clamp_pointer_x" --y "$clamp_y" \
        --adapter atspi >"$artifact_dir/tiling-divider-clamp-edge.json"
    "$probe" inspect "$tiling_divider_handle" \
        >"$artifact_dir/tiling-divider-clamped.json"
    divider_clamped=$(
        json_value element.value.current \
            <"$artifact_dir/tiling-divider-clamped.json"
    )
    "$probe" pointer --x "$((clamp_pointer_x - 24))" --y "$clamp_y" \
        --adapter atspi >"$artifact_dir/tiling-divider-clamp-reverse.json"
    "$probe" inspect "$tiling_divider_handle" \
        >"$artifact_dir/tiling-divider-after-clamp-reverse.json"
    divider_after_clamp_reverse=$(
        json_value element.value.current \
            <"$artifact_dir/tiling-divider-after-clamp-reverse.json"
    )
    "$probe" button --button left --up --adapter atspi \
        >"$artifact_dir/tiling-divider-clamp-up.json"
    pointer_button_down=false
    python3 -c '
import sys
if abs(float(sys.argv[2]) - float(sys.argv[1])) > 0.01:
    raise SystemExit("terminal divider jumped on reversal beyond its clamp")
' "$divider_clamped" "$divider_after_clamp_reverse"
fi

stop_current_app
XDG_CONFIG_HOME="$config_dir" \
    KODOSI_DATA_ROOT="$data_root" \
    KODOSI_PRODUCTION_DATA_ROOT="$production_data_root" \
    QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 \
    "$app" --ui-probe-tiling-synthetic --window-size 820x560 \
    >"$artifact_dir/kodosi-tiling-compact.log" 2>&1 &
app_pid=$!

compact_ready=false
for _ in $(seq 1 10); do
    if "$probe" apps >"$artifact_dir/tiling-compact-apps.json" 2>/dev/null; then
        app_handle=$(
            app_handle_for_pid "$app_pid" "Kodosi" \
                <"$artifact_dir/tiling-compact-apps.json" || true
        )
    fi
    if [[ -n "$app_handle" ]] \
        && "$probe" wait --app "$app_handle" \
        id=stage.scrollbar \
        --state showing --timeout-ms 5000 \
        >"$artifact_dir/tiling-compact-scrollbar.json" \
        2>"$artifact_dir/tiling-compact-scrollbar.err"; then
        compact_ready=true
        break
    fi
    kill -0 "$app_pid"
    sleep 0.1
done
if [[ "$compact_ready" != true ]]; then
    echo "Compact terminal grid scroll bar did not become accessible." >&2
    exit 4
fi

"$probe" find --app "$app_handle" --id stage.scroll \
    >"$artifact_dir/tiling-compact-stage-scroll.json"
python3 -c '
import json, sys
scrollbar = json.load(open(sys.argv[1]))["element"]
stage = json.load(sys.stdin)["matches"][0]
if scrollbar.get("name") != "Terminal grid scroll bar":
    raise SystemExit("terminal grid scroll bar has the wrong accessible name")
bar_bounds = scrollbar["bounds"]
stage_bounds = stage["bounds"]
if round(bar_bounds["height"]) != round(stage_bounds["height"]):
    raise SystemExit(
        "terminal grid scroll bar bounds follow content instead of viewport"
    )
if round(bar_bounds["y"]) != round(stage_bounds["y"]):
    raise SystemExit("terminal grid scroll bar is not anchored to viewport top")
bar_width = round(bar_bounds["width"])
if not 8 <= bar_width <= 12:
    raise SystemExit(
        f"terminal grid scroll bar gutter is {bar_width}px; expected 8-12px"
    )
if round(stage_bounds["x"] + stage_bounds["width"]) != round(bar_bounds["x"]):
    raise SystemExit("terminal grid did not reserve the scroll bar gutter")
' "$artifact_dir/tiling-compact-scrollbar.json" \
    <"$artifact_dir/tiling-compact-stage-scroll.json"

"$probe" find --app "$app_handle" --id stage.tile.tiling-0.focus \
    >"$artifact_dir/tiling-compact-focus.json"
compact_focus_handle=$(
    json_value matches.0.handle <"$artifact_dir/tiling-compact-focus.json"
)
"$probe" click "$compact_focus_handle" \
    >"$artifact_dir/tiling-compact-focus-click.json"
"$probe" wait --app "$app_handle" id=stage.focus.exit \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/tiling-compact-focus-exit.json"
"$probe" tree --app "$app_handle" --depth 10 \
    >"$artifact_dir/tiling-compact-focus-tree.json"
python3 -c '
import json, sys
tree = json.load(sys.stdin)
ids = set()
def visit(value):
    if isinstance(value, dict):
        item_id = value.get("id")
        if isinstance(item_id, str) and item_id:
            ids.add(item_id)
        for child in value.values():
            visit(child)
    elif isinstance(value, list):
        for child in value:
            visit(child)
visit(tree)
if "stage.scrollbar" in ids:
    raise SystemExit("terminal grid scroll bar remained accessible in focus mode")
' <"$artifact_dir/tiling-compact-focus-tree.json"

stop_current_app
rm -rf -- "$config_dir"
mkdir -p "$config_dir"
XDG_CONFIG_HOME="$config_dir" \
    KODOSI_DATA_ROOT="$data_root" \
    KODOSI_PRODUCTION_DATA_ROOT="$production_data_root" \
    QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 \
    "$app" >"$artifact_dir/kodosi-ready.log" 2>&1 &
app_pid=$!

ready_window=false
for _ in $(seq 1 50); do
    if "$probe" apps >"$artifact_dir/ready-apps.json" 2>/dev/null; then
        app_handle=$(
            app_handle_for_pid "$app_pid" "Kodosi" \
                <"$artifact_dir/ready-apps.json" || true
        )
    fi
    if [[ -n "$app_handle" ]] \
        && "$probe" wait --app "$app_handle" id=window.main \
        --state showing --timeout-ms 500 \
        >"$artifact_dir/ready-window.json" \
        2>"$artifact_dir/ready-window.err"; then
        ready_window=true
        break
    fi
    kill -0 "$app_pid"
    sleep 0.1
done
if [[ "$ready_window" != true ]]; then
    echo "Fresh isolated Kodosi app did not become accessible." >&2
    exit 4
fi

"$probe" find --app "$app_handle" --id sidebar.session.new \
    >"$artifact_dir/ready-new-session.json"
ready_new_handle=$(
    json_value matches.0.handle <"$artifact_dir/ready-new-session.json"
)
"$probe" click "$ready_new_handle" \
    >"$artifact_dir/ready-new-session-click.json"
"$probe" wait --app "$app_handle" id=session.create.name \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/ready-create-name.json"

"$probe" find --app "$app_handle" --id session.create.name \
    >"$artifact_dir/ready-create-name-find.json"
ready_name_handle=$(
    json_value matches.0.handle <"$artifact_dir/ready-create-name-find.json"
)
"$probe" set-text "$ready_name_handle" --text "Ready proof" \
    >"$artifact_dir/ready-create-name-set.json"
"$probe" find --app "$app_handle" --id session.create.directory \
    >"$artifact_dir/ready-create-directory.json"
ready_directory_handle=$(
    json_value matches.0.handle <"$artifact_dir/ready-create-directory.json"
)
"$probe" find --app "$app_handle" --id session.create.directory.browse \
    >"$artifact_dir/ready-create-directory-browse.json"
"$probe" set-text "$ready_directory_handle" --text "$PWD" \
    >"$artifact_dir/ready-create-directory-set.json"
"$probe" find --app "$app_handle" --id session.create.submit \
    >"$artifact_dir/ready-create-submit.json"
ready_submit_handle=$(
    json_value matches.0.handle <"$artifact_dir/ready-create-submit.json"
)
"$probe" click "$ready_submit_handle" \
    >"$artifact_dir/ready-create-submit-click.json"

ready_session_handle=
for _ in $(seq 1 150); do
    if "$probe" find --app "$app_handle" --pid "$app_pid" \
        --name "Ready proof" \
        >"$artifact_dir/ready-session-row.json" 2>/dev/null; then
        ready_session_handle=$(
            python3 -c '
import json, sys
matches = [
    item for item in json.load(sys.stdin)["matches"]
    if item.get("id", "").startswith("sidebar.session.")
    and ".actions." not in item.get("id", "")
]
print(matches[0]["handle"] if len(matches) == 1 else "")
' <"$artifact_dir/ready-session-row.json"
        )
    fi
    [[ -n "$ready_session_handle" ]] && break
    kill -0 "$app_pid"
    sleep 0.2
done
if [[ -z "$ready_session_handle" ]]; then
    echo "Created real local shell session did not appear in My Agents." >&2
    exit 4
fi
"$probe" button --button left --click \
    --element "$ready_session_handle" --adapter atspi \
    >"$artifact_dir/ready-session-select-click.json"
"$probe" wait --app "$app_handle" --pid "$app_pid" \
    name="Ready proof terminal" \
    --state showing --timeout-ms 30000 \
    >"$artifact_dir/ready-tile.json"
ready_tile_handle=$(
    json_value element.handle <"$artifact_dir/ready-tile.json"
)

terminal_ready=false
for _ in $(seq 1 150); do
    "$probe" inspect "$ready_tile_handle" \
        >"$artifact_dir/ready-tile-inspect.json"
    if python3 -c '
import json, sys
element = json.load(sys.stdin)["element"]
raise SystemExit(
    0 if element.get("description") != "Connecting terminal" else 1
)
' <"$artifact_dir/ready-tile-inspect.json"; then
        terminal_ready=true
        break
    fi
    kill -0 "$app_pid"
    sleep 0.2
done
if [[ "$terminal_ready" != true ]]; then
    echo "Real local shell terminal did not become ready." >&2
    exit 4
fi

"$probe" find --app "$app_handle" --pid "$app_pid" --role terminal \
    >"$artifact_dir/ready-native-terminal.json"
ready_terminal_handle=$(
    json_value matches.0.handle <"$artifact_dir/ready-native-terminal.json"
)
"$probe" inspect "$ready_terminal_handle" \
    >"$artifact_dir/ready-native-terminal-inspect.json"
"$probe" tree --app "$app_handle" --pid "$app_pid" --depth 12 \
    >"$artifact_dir/tiling-ready-tree.json"
test "$(
    json_value app.processId <"$artifact_dir/tiling-ready-tree.json"
)" = "$app_pid"
python3 -c '
import json, sys
tree = json.load(open(sys.argv[1]))
tile = json.load(open(sys.argv[2]))["element"]
terminal = json.load(sys.stdin)["element"]
if tile.get("description") == "Connecting terminal":
    raise SystemExit("ready terminal tile still announces connecting")
if terminal.get("role") != "terminal":
    raise SystemExit("native terminal accessibility role is missing")
states = set(terminal.get("states", []))
if "disabled" in states or "enabled" not in states:
    raise SystemExit("terminalReady semantics were not exposed")
text = terminal.get("text")
if not isinstance(text, dict) or text.get("characterCount", 0) <= 0:
    raise SystemExit("native terminal accessible text is empty")
if not isinstance(text.get("content"), str) or not text["content"]:
    raise SystemExit("native terminal accessible text content is unavailable")
ids = set()
showing_connecting = []
def visit(value):
    if isinstance(value, dict):
        item_id = value.get("id")
        if isinstance(item_id, str) and item_id:
            ids.add(item_id)
        if value.get("name") == "Connecting terminal" \
                and "showing" in value.get("states", []):
            showing_connecting.append(item_id or value.get("role", "unknown"))
        for child in value.values():
            visit(child)
    elif isinstance(value, list):
        for child in value:
            visit(child)
visit(tree)
if any(item_id.endswith(".connecting") for item_id in ids):
    raise SystemExit("connecting overlay subtree remains in ready tree")
if showing_connecting:
    raise SystemExit(
        "connecting overlay remains showing: " + ", ".join(showing_connecting)
    )
' "$artifact_dir/tiling-ready-tree.json" \
    "$artifact_dir/ready-tile-inspect.json" \
    <"$artifact_dir/ready-native-terminal-inspect.json"

echo "kodosi-ui-probe real-app smoke passed (PID $app_pid)"
