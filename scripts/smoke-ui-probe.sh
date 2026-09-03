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
        for _ in $(seq 1 150); do
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
    "header.utility.menu",
    "header.tab.my-agents",
    "header.tab.missions",
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

assert_tree_id() {
    python3 -c '
import json, sys
expected = sys.argv[1]
tree = json.load(sys.stdin)
found = False
def visit(value):
    global found
    if isinstance(value, dict):
        if value.get("id") == expected:
            found = True
        for child in value.values():
            visit(child)
    elif isinstance(value, list):
        for child in value:
            visit(child)
visit(tree)
if not found:
    raise SystemExit("accessible tree is missing " + expected)
' "$1"
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

start_synthetic_app() {
    local stem=$1
    shift
    local readiness_id=window.main
    if [[ "${1:-}" == --ready-id ]]; then
        readiness_id=$2
        shift 2
    fi
    stop_current_app
    (
        exec env \
            XDG_CONFIG_HOME="$config_dir" \
            KODOSI_DATA_ROOT="$data_root" \
            KODOSI_PRODUCTION_DATA_ROOT="$production_data_root" \
            QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 \
            "$app" "$@"
    ) >"$artifact_dir/$stem.log" 2>&1 &
    app_pid=$!

    local ready=false
    for _ in $(seq 1 150); do
        if "$probe" apps >"$artifact_dir/$stem-apps.json" 2>/dev/null; then
            app_handle=$(
                app_handle_for_pid "$app_pid" "Kodosi" \
                    <"$artifact_dir/$stem-apps.json" || true
            )
        fi
        if [[ -n "$app_handle" \
            && "$readiness_id" == QApplication.window.main ]] \
            && python3 -c '
import json, sys
handle = sys.argv[1]
apps = json.load(sys.stdin)["applications"]
matches = [app for app in apps if app.get("handle") == handle]
raise SystemExit(
    0 if len(matches) == 1
    and any(
        window.get("id") == "QApplication.window.main"
        and "showing" in window.get("states", [])
        for window in matches[0].get("windows", [])
    )
    else 1
)
' "$app_handle" <"$artifact_dir/$stem-apps.json"; then
            ready=true
            break
        elif [[ -n "$app_handle" ]] \
            && "$probe" find --app "$app_handle" --id "$readiness_id" \
            >"$artifact_dir/$stem-ready.json" 2>/dev/null; then
            if python3 -c '
import json, sys
matches = json.load(sys.stdin)["matches"]
raise SystemExit(
    0 if len(matches) == 1 and "showing" in matches[0].get("states", [])
    else 1
)
' <"$artifact_dir/$stem-ready.json"; then
                ready=true
                break
            fi
        fi
        kill -0 "$app_pid"
        sleep 0.1
    done
    if [[ "$ready" != true ]]; then
        echo "$stem did not become visible through AT-SPI." >&2
        return 4
    fi
    sleep 0.5
}

parity_only=${KODOSI_UI_PROBE_AGENT_PARITY_ONLY:-0}
if [[ "$parity_only" != 0 && "$parity_only" != 1 ]]; then
    echo "KODOSI_UI_PROBE_AGENT_PARITY_ONLY must be 0 or 1." >&2
    exit 2
fi
missions_only=${KODOSI_UI_PROBE_MISSIONS_ONLY:-0}
if [[ "$missions_only" != 0 && "$missions_only" != 1 ]]; then
    echo "KODOSI_UI_PROBE_MISSIONS_ONLY must be 0 or 1." >&2
    exit 2
fi
if [[ "$missions_only" == 1 ]]; then
    parity_only=1

    start_synthetic_app \
        missions-real \
        --ready-id QApplication.window.main
    "$probe" find --app "$app_handle" --id header.tab.missions \
        >"$artifact_dir/missions-real-tab.json"
    missions_real_tab_handle=$(
        json_value matches.0.handle \
            <"$artifact_dir/missions-real-tab.json"
    )
    "$probe" click "$missions_real_tab_handle" \
        >"$artifact_dir/missions-real-tab-click.json"
    "$probe" wait --app "$app_handle" id=surface.missions \
        --state showing --timeout-ms 5000 \
        >"$artifact_dir/missions-real-surface.json"
    if "$probe" wait --app "$app_handle" id=missions.refresh \
        --state showing --timeout-ms 1000 \
        >"$artifact_dir/missions-real-directory.json" 2>/dev/null; then
        "$probe" tree --app "$app_handle" --depth 12 \
            >"$artifact_dir/missions-real-directory-tree.json"
        python3 -c '
import json, sys
tree = json.load(sys.stdin)
ids = set()
def visit(value):
    if isinstance(value, dict):
        item_id = value.get("id")
        if isinstance(item_id, str):
            ids.add(item_id)
        for child in value.values():
            visit(child)
    elif isinstance(value, list):
        for child in value:
            visit(child)
visit(tree)
if not any(item.startswith("missions.item.") for item in ids):
    if "missions.directory.empty" not in ids:
        raise SystemExit("real Mission directory has no bounded state")
' <"$artifact_dir/missions-real-directory-tree.json"
    else
        "$probe" wait --app "$app_handle" id=auth.gate.missions \
            --state showing --timeout-ms 5000 \
            >"$artifact_dir/missions-real-auth-gate.json"
    fi
    stop_current_app
fi

if [[ "$parity_only" != 1 ]]; then
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
        && "$probe" find --app "$app_handle" --id window.main \
        >"$artifact_dir/window.json" 2>"$artifact_dir/window.err"; then
        if python3 -c '
import json, sys
matches = json.load(sys.stdin)["matches"]
raise SystemExit(
    0 if len(matches) == 1 and "showing" in matches[0].get("states", [])
    else 1
)
' <"$artifact_dir/window.json"; then
            window_ready=true
            break
        fi
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

"$probe" find --app "$app_handle" --id header.utility.menu \
    >"$artifact_dir/utility-button.json"
utility_handle=$(
    json_value matches.0.handle <"$artifact_dir/utility-button.json"
)
"$probe" click "$utility_handle" >"$artifact_dir/utility-click.json"
"$probe" wait --app "$app_handle" id=panel.utility.content \
    --state showing --timeout-ms 5000 >"$artifact_dir/utility-panel.json"
"$probe" find --app "$app_handle" --id panel.utility.settings \
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
id=panel.settings.account.signIn \
--state showing --timeout-ms 5000 \
>"$artifact_dir/settings-account-sign-in.json"

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
    "$probe" pointer --element "$field_handle" --position center \
        --adapter atspi >"$artifact_dir/font-family-raw-pointer.json"
    "$probe" button --button left --click --adapter atspi \
        >"$artifact_dir/font-family-raw-click.json"
    "$probe" wait --app "$app_handle" \
        id=panel.settings.terminal.fontFamily \
        --state focused --timeout-ms 5000 \
        >"$artifact_dir/font-family-raw-focused.json"
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

"$probe" click "$utility_handle" >"$artifact_dir/devices-utility-click.json"
"$probe" wait --app "$app_handle" id=panel.utility.content \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/devices-utility-panel.json"
"$probe" find --app "$app_handle" --id panel.utility.settings \
    >"$artifact_dir/devices-settings-button.json"
devices_settings_handle=$(
    json_value matches.0.handle \
        <"$artifact_dir/devices-settings-button.json"
)
"$probe" click "$devices_settings_handle" \
    >"$artifact_dir/devices-settings-click.json"
"$probe" wait --app "$app_handle" id=panel.settings \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/devices-settings-panel.json"
"$probe" find --app "$app_handle" --id panel.settings.tab.account \
    >"$artifact_dir/devices-account-tab.json"
devices_account_handle=$(
    json_value matches.0.handle <"$artifact_dir/devices-account-tab.json"
)
"$probe" click "$devices_account_handle" \
    >"$artifact_dir/devices-account-click.json"
"$probe" find --app "$app_handle" \
    --id panel.settings.account.openDevices \
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
if "$probe" wait --app "$app_handle" id=missions.refresh \
    --state showing --timeout-ms 1000 \
    >"$artifact_dir/missions-directory-refresh.json" 2>/dev/null; then
    "$probe" tree --app "$app_handle" --depth 9 \
        >"$artifact_dir/missions-directory-tree.json"
    first_mission_id=$(
        python3 -c '
import json, sys
tree = json.load(sys.stdin)
matches = []
def visit(value):
    if isinstance(value, dict):
        item_id = value.get("id")
        if (
            isinstance(item_id, str)
            and item_id.startswith("missions.item.")
        ):
            matches.append(item_id)
        for child in value.values():
            visit(child)
    elif isinstance(value, list):
        for child in value:
            visit(child)
visit(tree)
print(sorted(set(matches))[0] if matches else "")
' <"$artifact_dir/missions-directory-tree.json"
    )
    if [[ -n "$first_mission_id" ]]; then
        "$probe" find --app "$app_handle" --id "$first_mission_id" \
            >"$artifact_dir/missions-real-item.json"
        mission_item_handle=$(
            json_value matches.0.handle \
                <"$artifact_dir/missions-real-item.json"
        )
        "$probe" click "$mission_item_handle" \
            >"$artifact_dir/missions-real-open.json"
        "$probe" wait --app "$app_handle" id=missions.detail \
            --state showing --timeout-ms 5000 \
            >"$artifact_dir/missions-real-detail.json"
    else
        python3 -c '
import json, sys
tree = json.load(sys.stdin)
ids = set()
def visit(value):
    if isinstance(value, dict):
        item_id = value.get("id")
        if isinstance(item_id, str):
            ids.add(item_id)
        for child in value.values():
            visit(child)
    elif isinstance(value, list):
        for child in value:
            visit(child)
visit(tree)
expected = {
    "missions.directory.empty",
    "missions.directory.loading",
    "missions.directory.failure",
    "missions.directory.recovery",
    "missions.directory.state",
}
if not ids.intersection(expected):
    raise SystemExit("Mission directory exposed no bounded empty/loading/recovery/failure state")
' <"$artifact_dir/missions-directory-tree.json"
    fi
else
    "$probe" wait --app "$app_handle" id=auth.gate.missions \
        --state showing --timeout-ms 5000 \
        >"$artifact_dir/missions-auth-gate.json"
fi

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

fi

stop_current_app

start_synthetic_app \
    missions-pending-compact \
    --ready-id QApplication.window.main \
    --ui-probe-missions-pending \
    --window-size 820x560
"$probe" tree --app "$app_handle" --depth 14 \
    >"$artifact_dir/missions-pending-compact.json"
assert_tree_id missions.chat \
    <"$artifact_dir/missions-pending-compact.json"

start_synthetic_app \
    missions-unknown-wide \
    --ready-id QApplication.window.main \
    --ui-probe-missions-unknown \
    --window-size 1240x800
"$probe" tree --app "$app_handle" --depth 14 \
    >"$artifact_dir/missions-unknown-wide.json"
assert_tree_id missions.chat \
    <"$artifact_dir/missions-unknown-wide.json"

start_synthetic_app \
    missions-focus-compact \
    --ready-id QApplication.window.main \
    --ui-probe-missions-focus \
    --window-size 820x560
"$probe" tree --app "$app_handle" --depth 14 \
    >"$artifact_dir/missions-focus-compact.json"
assert_tree_id missions.chat \
    <"$artifact_dir/missions-focus-compact.json"
assert_tree_id missions.detail.people \
    <"$artifact_dir/missions-focus-compact.json"
"$probe" find --app "$app_handle" --id missions.detail.people \
    >"$artifact_dir/missions-focus-compact-people.json"
focus_people_handle=$(
    json_value matches.0.handle \
        <"$artifact_dir/missions-focus-compact-people.json"
)
"$probe" click "$focus_people_handle" \
    >"$artifact_dir/missions-focus-compact-people-click.json"
"$probe" wait --app "$app_handle" id=missions.focus.actions \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/missions-focus-compact-actions.json"

start_synthetic_app \
    missions-focus-wide \
    --ready-id QApplication.window.main \
    --ui-probe-missions-focus \
    --window-size 1240x800
"$probe" tree --app "$app_handle" --depth 14 \
    >"$artifact_dir/missions-focus-wide.json"
assert_tree_id missions.chat \
    <"$artifact_dir/missions-focus-wide.json"
"$probe" find --app "$app_handle" --id missions.detail.people \
    >"$artifact_dir/missions-focus-wide-people.json"
focus_people_handle=$(
    json_value matches.0.handle \
        <"$artifact_dir/missions-focus-wide-people.json"
)
"$probe" click "$focus_people_handle" \
    >"$artifact_dir/missions-focus-wide-people-click.json"
"$probe" wait --app "$app_handle" id=missions.focus.actions \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/missions-focus-wide-actions.json"

if [[ "$missions_only" == 1 ]]; then
    echo "kodosi-ui-probe Mission real/synthetic smoke passed (PID $app_pid)"
    exit 0
fi

start_synthetic_app \
    resume-agent-work \
    --ready-id panel.resumeAgentWork \
    --ui-probe-resume-agent-work \
    --window-size 820x560
"$probe" wait --app "$app_handle" id=panel.resumeAgentWork \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/resume-agent-work-panel.json"
"$probe" tree --app "$app_handle" --depth 7 \
    >"$artifact_dir/resume-agent-work-tree.json"
assert_shell_hidden_by_modal \
    <"$artifact_dir/resume-agent-work-tree.json"
"$probe" wait --app "$app_handle" id=resumeAgentWork.search \
    --state focused --timeout-ms 5000 \
    >"$artifact_dir/resume-agent-work-search-focused.json"
"$probe" find --app "$app_handle" --id resumeAgentWork.search \
    >"$artifact_dir/resume-agent-work-search.json"
resume_search_handle=$(
    json_value matches.0.handle \
        <"$artifact_dir/resume-agent-work-search.json"
)
"$probe" inspect "$resume_search_handle" \
    >"$artifact_dir/resume-agent-work-search-inspect.json"
python3 -c '
import json, sys
states = json.load(sys.stdin)["element"]["states"]
if "focused" not in states:
    raise SystemExit("Resume Agent Work did not focus search")
' <"$artifact_dir/resume-agent-work-search-inspect.json"
"$probe" set-text "$resume_search_handle" --text "no such conversation" \
    >"$artifact_dir/resume-agent-work-no-match-set.json"
"$probe" wait --app "$app_handle" id=resumeAgentWork.noMatches \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/resume-agent-work-no-match.json"
"$probe" set-text "$resume_search_handle" --text "Linux" \
    >"$artifact_dir/resume-agent-work-match-set.json"
"$probe" wait --app "$app_handle" id=resumeAgentWork.detail.title \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/resume-agent-work-detail.json"
"$probe" find --app "$app_handle" --id resumeAgentWork.preview \
    >"$artifact_dir/resume-agent-work-preview.json"
"$probe" find --app "$app_handle" --id resumeAgentWork.folder.browse \
    >"$artifact_dir/resume-agent-work-browse.json"
"$probe" find --app "$app_handle" --id resumeAgentWork.resume \
    >"$artifact_dir/resume-agent-work-resume.json"
resume_button_handle=$(
    json_value matches.0.handle \
        <"$artifact_dir/resume-agent-work-resume.json"
)
"$probe" inspect "$resume_button_handle" \
    >"$artifact_dir/resume-agent-work-resume-inspect.json"
python3 -c '
import json, sys
states = json.load(sys.stdin)["element"]["states"]
if "enabled" in states or "sensitive" in states:
    raise SystemExit("synthetic Resume action must not mutate a provider archive")
' <"$artifact_dir/resume-agent-work-resume-inspect.json"
"$probe" find --app "$app_handle" --id resumeAgentWork.cancel \
    >"$artifact_dir/resume-agent-work-cancel.json"
resume_cancel_handle=$(
    json_value matches.0.handle \
        <"$artifact_dir/resume-agent-work-cancel.json"
)
"$probe" click "$resume_cancel_handle" \
    >"$artifact_dir/resume-agent-work-cancel-click.json"

start_synthetic_app \
    project-intel-wide \
    --ready-id panel.projectIntel \
    --ui-probe-project-intel-populated \
    --window-size 1240x800
"$probe" wait --app "$app_handle" id=panel.projectIntel \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/project-intel-wide-panel.json"
python3 -c '
import json, sys
bounds = json.load(sys.stdin)["element"]["bounds"]
if not 1000 <= bounds["width"] <= 1080:
    raise SystemExit("wide Project Intelligence is not compact")
if not 560 <= bounds["height"] <= 680:
    raise SystemExit("wide Project Intelligence is not compact")
' <"$artifact_dir/project-intel-wide-panel.json"
"$probe" find --app "$app_handle" \
    --id panel.projectIntel.source.selected \
    >"$artifact_dir/project-intel-source-selected.json"
project_source_handle=$(
    json_value matches.0.handle \
        <"$artifact_dir/project-intel-source-selected.json"
)
"$probe" inspect "$project_source_handle" \
    >"$artifact_dir/project-intel-source-focus.json"
python3 -c '
import json, sys
element = json.load(sys.stdin)["element"]
states = set(element.get("states", []))
if "focused" not in states:
    raise SystemExit("selected Project Intelligence source button lacks focus")
if "selected" not in states:
    raise SystemExit("selected Project Intelligence source lacks selected state")
' <"$artifact_dir/project-intel-source-focus.json"

for tab in sessions memory servers agents; do
    "$probe" find --app "$app_handle" \
        --id "panel.projectIntel.tab.$tab" \
        >"$artifact_dir/project-intel-tab-$tab.json"
    tab_handle=$(
        json_value matches.0.handle \
            <"$artifact_dir/project-intel-tab-$tab.json"
    )
    "$probe" click "$tab_handle" \
        >"$artifact_dir/project-intel-tab-$tab-click.json"
    "$probe" inspect "$tab_handle" \
        >"$artifact_dir/project-intel-tab-$tab-inspect.json"
    python3 -c '
import json, sys
if "selected" not in json.load(sys.stdin)["element"].get("states", []):
    raise SystemExit("Project Intelligence tab is not selected")
' <"$artifact_dir/project-intel-tab-$tab-inspect.json"
    case "$tab" in
        sessions) pane_id=panel.projectIntel.sessions.list ;;
        memory) pane_id=panel.projectIntel.memory.list ;;
        servers) pane_id=panel.projectIntel.servers.list ;;
        agents) pane_id=panel.projectIntel.agents.list ;;
    esac
    "$probe" wait --app "$app_handle" --id "$pane_id" \
        --state showing --timeout-ms 5000 \
        >"$artifact_dir/project-intel-pane-$tab.json"
done
"$probe" find --app "$app_handle" \
    --id panel.projectIntel.agent.open \
    >"$artifact_dir/project-intel-agent-open.json"
"$probe" find --app "$app_handle" \
    --id panel.projectIntel.agents.detail.parseErrors \
    >"$artifact_dir/project-intel-agent-errors.json"
"$probe" find --app "$app_handle" \
    --id panel.projectIntel.tab.memory \
    >"$artifact_dir/project-intel-memory-tab.json"
project_memory_tab_handle=$(
    json_value matches.0.handle \
        <"$artifact_dir/project-intel-memory-tab.json"
)
"$probe" click "$project_memory_tab_handle" \
    >"$artifact_dir/project-intel-memory-tab-click.json"
"$probe" find --app "$app_handle" \
    --id panel.projectIntel.memory.open \
    >"$artifact_dir/project-intel-memory-open.json"
"$probe" find --app "$app_handle" \
    --id panel.projectIntel.memory.copy \
    >"$artifact_dir/project-intel-memory-copy.json"

start_synthetic_app \
    project-intel-empty \
    --ready-id panel.projectIntel \
    --ui-probe-project-intel-empty \
    --window-size 820x560
"$probe" wait --app "$app_handle" id=panel.projectIntel \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/project-intel-empty-panel.json"
python3 -c '
import json, sys
bounds = json.load(sys.stdin)["element"]["bounds"]
if bounds["width"] > 820 or bounds["height"] > 560:
    raise SystemExit("compact Project Intelligence exceeds the app window")
' <"$artifact_dir/project-intel-empty-panel.json"
for pair in \
    sessions:panel.projectIntel.sessions.empty \
    memory:panel.projectIntel.memory.empty \
    servers:panel.projectIntel.servers.empty \
    agents:panel.projectIntel.agents.empty; do
    tab=${pair%%:*}
    state_id=${pair#*:}
    "$probe" find --app "$app_handle" \
        --id "panel.projectIntel.tab.$tab" \
        >"$artifact_dir/project-intel-empty-tab-$tab.json"
    tab_handle=$(
        json_value matches.0.handle \
            <"$artifact_dir/project-intel-empty-tab-$tab.json"
    )
    "$probe" click "$tab_handle" \
        >"$artifact_dir/project-intel-empty-tab-$tab-click.json"
    "$probe" wait --app "$app_handle" --id "$state_id" \
        --state showing --timeout-ms 5000 \
        >"$artifact_dir/project-intel-empty-$tab.json"
done

start_synthetic_app \
    project-intel-archive \
    --ready-id panel.projectIntel \
    --ui-probe-project-intel-archive \
    --window-size 820x560
"$probe" wait --app "$app_handle" id=panel.projectIntel \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/project-intel-archive-panel.json"
for pair in \
    sessions:panel.projectIntel.sessions.empty \
    servers:panel.projectIntel.servers.gate \
    agents:panel.projectIntel.agents.gate; do
    tab=${pair%%:*}
    state_id=${pair#*:}
    "$probe" find --app "$app_handle" \
        --id "panel.projectIntel.tab.$tab" \
        >"$artifact_dir/project-intel-archive-tab-$tab.json"
    tab_handle=$(
        json_value matches.0.handle \
            <"$artifact_dir/project-intel-archive-tab-$tab.json"
    )
    "$probe" click "$tab_handle" \
        >"$artifact_dir/project-intel-archive-tab-$tab-click.json"
    "$probe" wait --app "$app_handle" --id "$state_id" \
        --state showing --timeout-ms 5000 \
        >"$artifact_dir/project-intel-archive-$tab.json"
done
python3 -c '
import json, sys
sessions = json.load(open(sys.argv[1]))["element"].get("name", "")
servers = json.load(open(sys.argv[2]))["element"].get("name", "")
agents = json.load(open(sys.argv[3]))["element"].get("name", "")
if "No archived sessions for this project" not in sessions:
    raise SystemExit("archive sessions empty copy drifted")
if "MCP configuration requires an active project" not in servers:
    raise SystemExit("archive MCP gate copy drifted")
if "Agents browser is only available for active projects." not in agents:
    raise SystemExit("archive Agents gate copy drifted")
' "$artifact_dir/project-intel-archive-sessions.json" \
    "$artifact_dir/project-intel-archive-servers.json" \
    "$artifact_dir/project-intel-archive-agents.json"

start_synthetic_app \
    agent-settings-parity \
    --ready-id panel.settings \
    --ui-probe-agent-settings-populated \
    --window-size 820x560
"$probe" wait --app "$app_handle" id=panel.settings \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/agent-settings-panel.json"
python3 -c '
import json, sys
bounds = json.load(sys.stdin)["element"]["bounds"]
if bounds["width"] > 820 or bounds["height"] > 560:
    raise SystemExit("compact Agents settings exceeds the app window")
' <"$artifact_dir/agent-settings-panel.json"
"$probe" wait --app "$app_handle" \
    id=panel.settings.scroll --state showing --timeout-ms 5000 \
    >"$artifact_dir/agent-settings-compact-scroll.json"
"$probe" wait --app "$app_handle" \
    id=panel.settings.agents.scope --state showing --timeout-ms 5000 \
    >"$artifact_dir/agent-settings-compact-scope.json"
"$probe" wait --app "$app_handle" \
    id=panel.settings.apply --state showing --timeout-ms 5000 \
    >"$artifact_dir/agent-settings-compact-footer-action.json"
python3 -c '
import json, sys
panel = json.load(open(sys.argv[1]))["element"]["bounds"]
scroll = json.load(open(sys.argv[2]))["element"]["bounds"]
scope = json.load(open(sys.argv[3]))["element"]["bounds"]
apply = json.load(open(sys.argv[4]))["element"]["bounds"]
footer_top = panel["y"] + panel["height"] - 66
if scroll["y"] + scroll["height"] > footer_top:
    raise SystemExit("compact settings scroll viewport overlaps the 66px footer")
if scope["y"] < scroll["y"] or scope["y"] + scope["height"] > scroll["y"] + scroll["height"]:
    raise SystemExit("compact settings scope control is not fully visible on first paint")
if apply["y"] < footer_top:
    raise SystemExit("compact Apply & Save action escaped the footer")
' "$artifact_dir/agent-settings-panel.json" \
  "$artifact_dir/agent-settings-compact-scroll.json" \
  "$artifact_dir/agent-settings-compact-scope.json" \
  "$artifact_dir/agent-settings-compact-footer-action.json"
"$probe" find --app "$app_handle" \
    --id panel.settings.autoMode.allow \
    >"$artifact_dir/agent-settings-auto-allow.json"
auto_allow_handle=$(
    json_value matches.0.handle \
        <"$artifact_dir/agent-settings-auto-allow.json"
)
"$probe" focus "$auto_allow_handle" \
    >"$artifact_dir/agent-settings-auto-allow-focus.json"
"$probe" set-text "$auto_allow_handle" --text "unsaved probe draft" \
    >"$artifact_dir/agent-settings-auto-allow-set.json"
"$probe" find --app "$app_handle" \
    --id panel.settings.autoMode.reload \
    >"$artifact_dir/agent-settings-auto-reload.json"
auto_reload_handle=$(
    json_value matches.0.handle \
        <"$artifact_dir/agent-settings-auto-reload.json"
)
"$probe" click "$auto_reload_handle" \
    >"$artifact_dir/agent-settings-auto-reload-click.json"
"$probe" wait --app "$app_handle" \
    id=panel.settings.autoMode.reload.cancel \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/agent-settings-auto-reload-cancel.json"
auto_cancel_handle=$(
    json_value element.handle \
        <"$artifact_dir/agent-settings-auto-reload-cancel.json"
)
"$probe" inspect "$auto_cancel_handle" \
    >"$artifact_dir/agent-settings-auto-reload-cancel-inspect.json"
python3 -c '
import json, sys
if "focused" not in json.load(sys.stdin)["element"].get("states", []):
    raise SystemExit("Auto Mode replacement confirmation did not isolate focus")
' <"$artifact_dir/agent-settings-auto-reload-cancel-inspect.json"
"$probe" click "$auto_cancel_handle" \
    >"$artifact_dir/agent-settings-auto-reload-cancel-click.json"
"$probe" inspect "$auto_allow_handle" \
    >"$artifact_dir/agent-settings-auto-allow-preserved.json"
test "$(
    json_value element.text.content \
        <"$artifact_dir/agent-settings-auto-allow-preserved.json"
)" = "unsaved probe draft"

"$probe" find --app "$app_handle" --name "Copy Source Path" \
    >"$artifact_dir/agent-settings-external-copy.json"
external_copy_handle=$(
    python3 -c '
import json, sys
matches = [
    item for item in json.load(sys.stdin)["matches"]
    if item.get("id", "").startswith("panel.settings.external.copy.")
]
if not matches:
    raise SystemExit("external Copy Source Path action is missing")
print(matches[0]["handle"])
' <"$artifact_dir/agent-settings-external-copy.json"
)
"$probe" find --app "$app_handle" --name Open \
    >"$artifact_dir/agent-settings-external-open.json"
"$probe" find --app "$app_handle" --name "Reveal in File Manager" \
    >"$artifact_dir/agent-settings-external-reveal.json"
"$probe" focus "$external_copy_handle" \
    >"$artifact_dir/agent-settings-external-copy-focus.json"
sleep 0.1
"$probe" click "$external_copy_handle" \
    >"$artifact_dir/agent-settings-external-copy-click.json"
"$probe" wait --app "$app_handle" \
    id=panel.settings.externalDiscovery.status \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/agent-settings-external-status.json"
if "$probe" inspect "$external_copy_handle" \
    >"$artifact_dir/agent-settings-external-stale.json" 2>&1; then
    python3 -c '
import json, sys
states = json.load(sys.stdin)["element"].get("states", [])
if "showing" in states:
    raise SystemExit("consumed external action handle remained actionable")
' <"$artifact_dir/agent-settings-external-stale.json"
fi

"$probe" find --app "$app_handle" \
    --id panel.settings.agents.openProjectBrowser \
    >"$artifact_dir/agent-settings-open-project.json"
settings_project_handle=$(
    json_value matches.0.handle \
        <"$artifact_dir/agent-settings-open-project.json"
)
"$probe" click "$settings_project_handle" \
    >"$artifact_dir/agent-settings-open-project-click.json"
"$probe" wait --app "$app_handle" id=panel.projectIntel \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/agent-settings-project-transfer.json"

start_synthetic_app \
    agent-settings-wide \
    --ready-id panel.settings \
    --ui-probe-agent-settings-populated \
    --window-size 1240x800
"$probe" wait --app "$app_handle" id=panel.settings \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/agent-settings-wide-panel.json"
"$probe" wait --app "$app_handle" \
    id=panel.settings.scroll --state showing --timeout-ms 5000 \
    >"$artifact_dir/agent-settings-wide-scroll.json"
"$probe" wait --app "$app_handle" \
    id=panel.settings.agents.scope --state showing --timeout-ms 5000 \
    >"$artifact_dir/agent-settings-wide-scope.json"
"$probe" wait --app "$app_handle" \
    id=panel.settings.apply --state showing --timeout-ms 5000 \
    >"$artifact_dir/agent-settings-wide-footer-action.json"
python3 -c '
import json, sys
panel = json.load(open(sys.argv[1]))["element"]["bounds"]
scroll = json.load(open(sys.argv[2]))["element"]["bounds"]
scope = json.load(open(sys.argv[3]))["element"]["bounds"]
apply = json.load(open(sys.argv[4]))["element"]["bounds"]
footer_top = panel["y"] + panel["height"] - 66
if scroll["y"] + scroll["height"] > footer_top:
    raise SystemExit("wide settings scroll viewport overlaps the 66px footer")
if scope["y"] < scroll["y"] or scope["y"] + scope["height"] > scroll["y"] + scroll["height"]:
    raise SystemExit("wide settings scope control is not fully visible on first paint")
if apply["y"] < footer_top:
    raise SystemExit("wide Apply & Save action escaped the footer")
' "$artifact_dir/agent-settings-wide-panel.json" \
  "$artifact_dir/agent-settings-wide-scroll.json" \
  "$artifact_dir/agent-settings-wide-scope.json" \
  "$artifact_dir/agent-settings-wide-footer-action.json"

if [[ "$parity_only" == 1 ]]; then
    echo "kodosi-ui-probe agent parity smoke passed (PID $app_pid)"
    exit 0
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

"$probe" tree --app "$app_handle" --depth 12 \
    >"$artifact_dir/tiling-tree.json"
test "$(
    json_value app.processId <"$artifact_dir/tiling-tree.json"
)" = "$app_pid"
for index in 0 1 2 3; do
    "$probe" find --app "$app_handle" \
        --id "stage.tile.tiling-$index" \
        >"$artifact_dir/tiling-tile-$index.json"
done

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
"$probe" find --app "$app_handle" --id stage.tile.tiling-2 \
    >"$artifact_dir/tiling-focus-selected-tile.json"
python3 -c '
import json, sys
matches = json.load(sys.stdin)["matches"]
if len(matches) != 1 or matches[0].get("description") != "Connecting terminal":
    raise SystemExit("connecting terminal status was not announced")
' <"$artifact_dir/tiling-focus-selected-tile.json"
for index in 0 1 3; do
    if "$probe" find --app "$app_handle" \
        --id "stage.tile.tiling-$index" \
        >"$artifact_dir/tiling-focus-hidden-$index.json" 2>/dev/null; then
        echo "Off-focus terminal tile $index remained accessible." >&2
        exit 1
    fi
done
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
scrollbar_states = None
def visit(value):
    global scrollbar_states
    if isinstance(value, dict):
        item_id = value.get("id")
        if item_id == "stage.scrollbar":
            scrollbar_states = set(value.get("states", []))
        for child in value.values():
            visit(child)
    elif isinstance(value, list):
        for child in value:
            visit(child)
visit(tree)
if scrollbar_states is not None \
        and {"showing", "visible"} & scrollbar_states:
    raise SystemExit("terminal grid scroll bar remained accessible in focus mode")
' <"$artifact_dir/tiling-compact-focus-tree.json"

start_synthetic_app startup-failure --ready-id startup.retry \
    --test-startup-failure-once
"$probe" wait --app "$app_handle" id=startup.retry \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/startup-failure-retry.json"
"$probe" wait --app "$app_handle" id=startup.retry \
    --state focused --timeout-ms 5000 \
    >"$artifact_dir/startup-failure-retry-focused.json"
"$probe" tree --app "$app_handle" --depth 7 \
    >"$artifact_dir/startup-failure-tree.json"
assert_shell_hidden_by_modal <"$artifact_dir/startup-failure-tree.json"

secondary_pid=
set +e
XDG_CONFIG_HOME="$config_dir" \
    KODOSI_DATA_ROOT="$data_root" \
    KODOSI_PRODUCTION_DATA_ROOT="$production_data_root" \
    QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 \
    "$app" "kodosi://session/startup-forwarded" \
    >"$artifact_dir/startup-failure-secondary.log" 2>&1 &
secondary_pid=$!
wait "$secondary_pid"
secondary_status=$?
set -e
if [[ "$secondary_status" -ne 0 ]]; then
    echo "Startup-failed owner did not acknowledge activation." >&2
    exit 4
fi
if ! kill -0 "$app_pid" 2>/dev/null; then
    echo "Startup-failed owner exited during activation forwarding." >&2
    exit 4
fi
"$probe" tree --app "$app_handle" --depth 7 \
    >"$artifact_dir/startup-failure-forwarded-tree.json"
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
leaked = sorted(ids & {
    "deepLink.status",
    "deepLink.status.label",
    "deepLink.status.dismiss",
})
if leaked:
    raise SystemExit(
        "deep-link status leaked before runtime recovery: "
        + ", ".join(leaked)
    )
' <"$artifact_dir/startup-failure-forwarded-tree.json"

"$probe" find --app "$app_handle" --id startup.diagnostics \
    >"$artifact_dir/startup-failure-diagnostics.json"
startup_diagnostics_handle=$(
    json_value matches.0.handle \
        <"$artifact_dir/startup-failure-diagnostics.json"
)
"$probe" click "$startup_diagnostics_handle" \
    >"$artifact_dir/startup-failure-diagnostics-click.json"
"$probe" wait --app "$app_handle" id=panel.diagnostics \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/startup-failure-diagnostics-panel.json"
"$probe" tree --app "$app_handle" --depth 7 \
    >"$artifact_dir/startup-failure-diagnostics-tree.json"
assert_shell_hidden_by_modal \
    <"$artifact_dir/startup-failure-diagnostics-tree.json"
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
leaked = sorted(ids & {
    "startup.overlay",
    "startup.content",
    "startup.title",
    "startup.failure.detail",
    "startup.retry",
    "startup.diagnostics",
})
if leaked:
    raise SystemExit(
        "startup controls leaked into Diagnostics AT-SPI tree: "
        + ", ".join(leaked)
    )
' <"$artifact_dir/startup-failure-diagnostics-tree.json"
"$probe" find --app "$app_handle" --id panel.diagnostics.close \
    >"$artifact_dir/startup-failure-diagnostics-close.json"
startup_diagnostics_close_handle=$(
    json_value matches.0.handle \
        <"$artifact_dir/startup-failure-diagnostics-close.json"
)
"$probe" click "$startup_diagnostics_close_handle" \
    >"$artifact_dir/startup-failure-diagnostics-close-click.json"
"$probe" wait --app "$app_handle" id=startup.retry \
    --state focused --timeout-ms 5000 \
    >"$artifact_dir/startup-failure-retry-refocused.json"
startup_retry_handle=$(
    json_value element.handle \
        <"$artifact_dir/startup-failure-retry-refocused.json"
)
"$probe" click "$startup_retry_handle" \
    >"$artifact_dir/startup-failure-retry-click.json"
"$probe" wait --app "$app_handle" id=header.utility.menu \
    --state showing --timeout-ms 30000 \
    >"$artifact_dir/startup-failure-ready.json"
"$probe" wait --app "$app_handle" id=deepLink.status \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/startup-failure-deep-link-status.json"
"$probe" find --app "$app_handle" --id deepLink.status.dismiss \
    >"$artifact_dir/startup-failure-deep-link-dismiss.json"
startup_deep_link_dismiss_handle=$(
    json_value matches.0.handle \
        <"$artifact_dir/startup-failure-deep-link-dismiss.json"
)
"$probe" click "$startup_deep_link_dismiss_handle" \
    >"$artifact_dir/startup-failure-deep-link-dismiss-click.json"
"$probe" tree --app "$app_handle" --depth 4 \
    >"$artifact_dir/startup-failure-ready-tree.json"
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
if "startup.overlay" in ids:
    raise SystemExit("startup overlay remained accessible after retry")
if "deepLink.status" in ids:
    raise SystemExit("dismissed deep-link status remained accessible")
' <"$artifact_dir/startup-failure-ready-tree.json"

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
for _ in $(seq 1 150); do
    if "$probe" apps >"$artifact_dir/ready-apps.json" 2>/dev/null; then
        app_handle=$(
            app_handle_for_pid "$app_pid" "Kodosi" \
                <"$artifact_dir/ready-apps.json" || true
        )
    fi
    if [[ -n "$app_handle" ]] \
        && "$probe" find --app "$app_handle" --id window.main \
        >"$artifact_dir/ready-window.json" \
        2>"$artifact_dir/ready-window.err"; then
        if python3 -c '
import json, sys
matches = json.load(sys.stdin)["matches"]
raise SystemExit(
    0 if len(matches) == 1 and "showing" in matches[0].get("states", [])
    else 1
)
' <"$artifact_dir/ready-window.json"; then
            ready_window=true
            break
        fi
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
ready_session_id=$(
    python3 -c '
import json, sys
matches = [
    item for item in json.load(sys.stdin)["matches"]
    if item.get("id", "").startswith("sidebar.session.")
    and ".actions." not in item.get("id", "")
]
if len(matches) != 1:
    raise SystemExit(1)
print(matches[0]["id"].removeprefix("sidebar.session."))
' <"$artifact_dir/ready-session-row.json"
)
test -n "$ready_session_id"

"$probe" find --app "$app_handle" --id header.utility.menu \
    >"$artifact_dir/ready-utility.json"
ready_utility_handle=$(
    json_value matches.0.handle <"$artifact_dir/ready-utility.json"
)
"$probe" click "$ready_utility_handle" \
    >"$artifact_dir/ready-utility-click.json"
"$probe" wait --app "$app_handle" id=panel.utility.content \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/ready-utility-panel.json"
"$probe" find --app "$app_handle" --id panel.utility.settings \
    >"$artifact_dir/ready-settings.json"
ready_settings_handle=$(
    json_value matches.0.handle <"$artifact_dir/ready-settings.json"
)
"$probe" click "$ready_settings_handle" \
    >"$artifact_dir/ready-settings-click.json"
"$probe" wait --app "$app_handle" id=panel.settings \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/ready-settings-panel.json"
"$probe" find --app "$app_handle" --id panel.settings.tab.account \
    >"$artifact_dir/ready-account-tab.json"
ready_account_handle=$(
    json_value matches.0.handle <"$artifact_dir/ready-account-tab.json"
)
"$probe" click "$ready_account_handle" \
    >"$artifact_dir/ready-account-click.json"
"$probe" find --app "$app_handle" \
    --id panel.settings.account.openDevices \
    >"$artifact_dir/ready-devices-tab.json"
ready_devices_handle=$(
    json_value matches.0.handle <"$artifact_dir/ready-devices-tab.json"
)
"$probe" click "$ready_devices_handle" \
    >"$artifact_dir/ready-devices-click.json"
"$probe" wait --app "$app_handle" --pid "$app_pid" id=surface.devices \
    --state showing --timeout-ms 5000 \
    >"$artifact_dir/ready-devices-view.json"

secondary_pid=
set +e
XDG_CONFIG_HOME="$config_dir" \
    KODOSI_DATA_ROOT="$data_root" \
    KODOSI_PRODUCTION_DATA_ROOT="$production_data_root" \
    QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 \
    "$app" "kodosi://session/$ready_session_id" \
    >"$artifact_dir/ready-deep-link-secondary.log" 2>&1 &
secondary_pid=$!
wait "$secondary_pid"
secondary_status=$?
set -e
if [[ "$secondary_status" -ne 0 ]]; then
    echo "Secondary deep-link process was not acknowledged." >&2
    exit 4
fi
if ! kill -0 "$app_pid" 2>/dev/null; then
    echo "The original Kodosi owner exited during deep-link forwarding." >&2
    exit 4
fi
if [[ "$secondary_pid" = "$app_pid" ]]; then
    echo "Deep-link probe did not launch a distinct secondary process." >&2
    exit 4
fi
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
