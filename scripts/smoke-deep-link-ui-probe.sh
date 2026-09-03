#!/usr/bin/env bash

set -euo pipefail

cd "$(dirname "$0")/.."

probe=${KODOSI_UI_PROBE:-"$PWD/build/dev/src/kodosi-ui-probe"}
app=${KODOSI_QT_APP:-"$PWD/build/dev/src/kodosi-qt"}
artifact_dir=${KODOSI_UI_PROBE_ARTIFACT_DIR:-"$PWD/build/ui-probe-deep-link"}
root="$artifact_dir/run.$$"
config_dir="$root/config"
state_dir="$root/state"
data_root="$root/data"
production_data_root="$root/production-data"
mkdir -p "$config_dir" "$state_dir" "$data_root" "$production_data_root"
rm -f "$artifact_dir/owner.log" "$artifact_dir/status-owner.log"
app_pid=

cleanup() {
    status=$?
    trap - EXIT INT TERM
    stop_app
    if [[ "$status" -ne 0 && -f "$artifact_dir/owner.log" ]]; then
        cat "$artifact_dir/owner.log" >&2
    fi
    rm -rf -- "$root"
    exit "$status"
}

stop_app() {
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
    [[ -z "$app_pid" ]] || wait "$app_pid" 2>/dev/null || true
    app_pid=
    app_handle=
}
trap cleanup EXIT INT TERM

if [[ ! -x "$probe" || ! -x "$app" ]]; then
    echo "Build kodosi-ui-probe and kodosi-qt first." >&2
    exit 2
fi
if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
    echo "The deep-link probe requires a graphical desktop session." >&2
    exit 3
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

app_handle_for_pid() {
    python3 -c '
import json, sys
pid = int(sys.argv[1])
matches = [
    app["handle"]
    for app in json.load(sys.stdin)["applications"]
    if app.get("processId") == pid
    and (app.get("name") == "Kodosi" or app.get("id") == "Kodosi")
]
print(matches[0] if len(matches) == 1 else "")
' "$1"
}

XDG_CONFIG_HOME="$config_dir" \
XDG_STATE_HOME="$state_dir" \
KODOSI_DATA_ROOT="$data_root" \
KODOSI_PRODUCTION_DATA_ROOT="$production_data_root" \
QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 \
"$app" --window-size 820x560 "kodosi://session/%GG" \
    >"$artifact_dir/status-owner.log" 2>&1 &
app_pid=$!

app_handle=
for _ in $(seq 1 100); do
    if "$probe" apps >"$artifact_dir/status-apps.json" 2>/dev/null; then
        app_handle=$(
            app_handle_for_pid "$app_pid" \
                <"$artifact_dir/status-apps.json" || true
        )
    fi
    if [[ -n "$app_handle" ]] \
        && "$probe" wait --app "$app_handle" --pid "$app_pid" \
            id=deepLink.status --state showing --timeout-ms 500 \
            >"$artifact_dir/status-banner.json" 2>/dev/null; then
        break
    fi
    kill -0 "$app_pid"
    sleep 0.1
done
if [[ -z "$app_handle" ]]; then
    echo "The compact status probe did not become accessible." >&2
    exit 4
fi

"$probe" find --app "$app_handle" --pid "$app_pid" \
    --id window.main >"$artifact_dir/status-window.json"
"$probe" find --app "$app_handle" --pid "$app_pid" \
    --id deepLink.status.label >"$artifact_dir/status-label.json"
"$probe" find --app "$app_handle" --pid "$app_pid" \
    --id deepLink.status.dismiss >"$artifact_dir/status-dismiss.json"
python3 -c '
import json, sys
window = json.load(open(sys.argv[1]))["matches"][0]["bounds"]
banner = json.load(open(sys.argv[2]))["element"]["bounds"]
label = json.load(open(sys.argv[3]))["matches"][0]["bounds"]
dismiss_item = json.load(open(sys.argv[4]))["matches"][0]
dismiss = dismiss_item["bounds"]
if window["width"] > 820 or window["height"] > 560:
    raise SystemExit("deep-link status probe did not use a compact window")
if banner["x"] < window["x"] or banner["y"] < window["y"]:
    raise SystemExit("deep-link status escaped the compact window origin")
if (
    banner["x"] + banner["width"] > window["x"] + window["width"]
    or banner["y"] + banner["height"] > window["y"] + window["height"]
):
    raise SystemExit("deep-link status exceeds the 820x560 window")
if label["width"] <= 0 or label["x"] + label["width"] > dismiss["x"]:
    raise SystemExit("deep-link status label overlaps its dismiss action")
if dismiss["x"] + dismiss["width"] > banner["x"] + banner["width"]:
    raise SystemExit("deep-link status dismiss action escaped the banner")
if dismiss_item.get("name") != "Dismiss link status":
    raise SystemExit("deep-link status dismiss action has the wrong accessible name")
' "$artifact_dir/status-window.json" \
    "$artifact_dir/status-banner.json" \
    "$artifact_dir/status-label.json" \
    "$artifact_dir/status-dismiss.json"
status_dismiss_handle=$(
    json_value matches.0.handle <"$artifact_dir/status-dismiss.json"
)
"$probe" click "$status_dismiss_handle" \
    >"$artifact_dir/status-dismiss-click.json"
sleep 0.2
set +e
"$probe" find --app "$app_handle" --pid "$app_pid" \
    --id deepLink.status >"$artifact_dir/status-after-dismiss.json" 2>/dev/null
status_find=$?
set -e
if [[ "$status_find" -eq 0 ]] && python3 -c '
import json, sys
raise SystemExit(0 if json.load(sys.stdin).get("matches") else 1)
' <"$artifact_dir/status-after-dismiss.json"; then
    echo "The deep-link status remained accessible after dismissal." >&2
    exit 4
fi
stop_app

XDG_CONFIG_HOME="$config_dir" \
XDG_STATE_HOME="$state_dir" \
KODOSI_DATA_ROOT="$data_root" \
KODOSI_PRODUCTION_DATA_ROOT="$production_data_root" \
QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 \
"$app" >"$artifact_dir/owner.log" 2>&1 &
app_pid=$!

app_handle=
for _ in $(seq 1 100); do
    if "$probe" apps >"$artifact_dir/apps.json" 2>/dev/null; then
        app_handle=$(
            app_handle_for_pid "$app_pid" <"$artifact_dir/apps.json" || true
        )
    fi
    if [[ -n "$app_handle" ]] \
        && "$probe" wait --app "$app_handle" --pid "$app_pid" \
            id=window.main --state showing --timeout-ms 500 \
            >"$artifact_dir/window.json" 2>/dev/null; then
        break
    fi
    kill -0 "$app_pid"
    sleep 0.1
done
if [[ -z "$app_handle" ]]; then
    echo "The isolated real app did not become accessible." >&2
    exit 4
fi

"$probe" find --app "$app_handle" --pid "$app_pid" \
    --id sidebar.session.new >"$artifact_dir/new.json"
new_handle=$(json_value matches.0.handle <"$artifact_dir/new.json")
"$probe" click "$new_handle" >"$artifact_dir/new-click.json"
"$probe" wait --app "$app_handle" --pid "$app_pid" \
    id=session.create.name --state showing --timeout-ms 5000 \
    >"$artifact_dir/create.json"

"$probe" find --app "$app_handle" --pid "$app_pid" \
    --id session.create.name >"$artifact_dir/name.json"
name_handle=$(json_value matches.0.handle <"$artifact_dir/name.json")
"$probe" set-text "$name_handle" --text "Deep link proof" \
    >"$artifact_dir/name-set.json"
"$probe" find --app "$app_handle" --pid "$app_pid" \
    --id session.create.directory >"$artifact_dir/directory.json"
directory_handle=$(
    json_value matches.0.handle <"$artifact_dir/directory.json"
)
"$probe" set-text "$directory_handle" --text "$PWD" \
    >"$artifact_dir/directory-set.json"
"$probe" find --app "$app_handle" --pid "$app_pid" \
    --id session.create.submit >"$artifact_dir/submit.json"
submit_handle=$(json_value matches.0.handle <"$artifact_dir/submit.json")
"$probe" click "$submit_handle" >"$artifact_dir/submit-click.json"

session_id=
for _ in $(seq 1 150); do
    if "$probe" find --app "$app_handle" --pid "$app_pid" \
        --name "Deep link proof" >"$artifact_dir/session.json" 2>/dev/null; then
        session_id=$(
            python3 -c '
import json, sys
matches = [
    item for item in json.load(sys.stdin)["matches"]
    if item.get("id", "").startswith("sidebar.session.")
    and ".actions." not in item.get("id", "")
]
print(
    matches[0]["id"].removeprefix("sidebar.session.")
    if len(matches) == 1 else ""
)
' <"$artifact_dir/session.json"
        )
    fi
    [[ -n "$session_id" ]] && break
    kill -0 "$app_pid"
    sleep 0.2
done
if [[ -z "$session_id" ]]; then
    echo "The real runtime session did not appear." >&2
    exit 4
fi

"$probe" find --app "$app_handle" --pid "$app_pid" \
    --id header.tab.devices >"$artifact_dir/devices.json"
devices_handle=$(json_value matches.0.handle <"$artifact_dir/devices.json")
"$probe" click "$devices_handle" >"$artifact_dir/devices-click.json"
"$probe" wait --app "$app_handle" --pid "$app_pid" \
    id=surface.devices --state showing --timeout-ms 5000 \
    >"$artifact_dir/devices-view.json"

set +e
XDG_CONFIG_HOME="$config_dir" \
XDG_STATE_HOME="$state_dir" \
KODOSI_DATA_ROOT="$data_root" \
KODOSI_PRODUCTION_DATA_ROOT="$production_data_root" \
QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 \
"$app" "kodosi://session/$session_id" \
    >"$artifact_dir/secondary.log" 2>&1 &
secondary_pid=$!
wait "$secondary_pid"
secondary_status=$?
set -e
if [[ "$secondary_status" -ne 0 ]]; then
    echo "The secondary process did not receive an acknowledgement." >&2
    exit 4
fi
kill -0 "$app_pid"
test "$secondary_pid" != "$app_pid"

"$probe" wait --app "$app_handle" --pid "$app_pid" \
    name="Deep link proof terminal" --state showing --timeout-ms 30000 \
    >"$artifact_dir/navigated-terminal.json"
"$probe" apps >"$artifact_dir/apps-after.json"
python3 -c '
import json, sys
owner = int(sys.argv[1])
secondary = int(sys.argv[2])
pids = {app.get("processId") for app in json.load(sys.stdin)["applications"]}
if owner not in pids:
    raise SystemExit("the original PID no longer owns the app")
if secondary in pids:
    raise SystemExit("the secondary PID constructed a second app")
' "$app_pid" "$secondary_pid" <"$artifact_dir/apps-after.json"

echo "kodosi deep-link real-app probe passed (owner PID $app_pid)"
