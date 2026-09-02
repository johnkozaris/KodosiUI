#!/usr/bin/env bash

set -euo pipefail

cd "$(dirname "$0")/.."

package=${1:-build/release/kodosi_0.1.0_amd64.deb}
package=$(realpath "$package")
image='ubuntu@sha256:33ceb71981b602c1a7443a53469e4dba065f7503eab3078a2d7a57a2ab987517'
probe="$PWD/build/release/src/kodosi-ui-probe"

if [[ ! -x "$probe" ]]; then
    echo "Build the developer UI probe before desktop verification." >&2
    exit 2
fi
XDG_SESSION_TYPE=wayland WAYLAND_DISPLAY=wayland-probe-capability \
    "$probe" input-status |
    python3 -c '
import json, sys
status = json.load(sys.stdin)
assert status["selectedAdapter"] == "portal-sidecar"
assert status["portalSecurityAuthority"] is True
'

docker run --rm \
    -v "$package:/work/kodosi.deb:ro" \
    "$image" \
    bash -lc '
        set -euo pipefail
        mkdir -p /work/temp
        export TMPDIR=/work/temp
        apt-get update -qq
        DEBIAN_FRONTEND=noninteractive apt-get install -y \
            /work/kodosi.deb xvfb weston >/work/install.log

        weston_pid=
        Xvfb :99 -screen 0 1280x800x24 -nolock \
            -nolisten unix -listen tcp >/work/xvfb.log 2>&1 &
        xvfb_pid=$!
        cleanup() {
            status=$?
            trap - EXIT
            if [ "$status" -ne 0 ]; then
                cat /work/xvfb.log
                test ! -f /work/weston.log || cat /work/weston.log
            fi
            test -z "$weston_pid" || kill "$weston_pid" 2>/dev/null || true
            kill "$xvfb_pid" 2>/dev/null || true
            exit "$status"
        }
        trap cleanup EXIT

        for _ in $(seq 1 50); do
            if { printf "" >/dev/tcp/127.0.0.1/6099; } 2>/dev/null; then
                break
            fi
            sleep 0.1
        done
        DISPLAY=127.0.0.1:99 QT_QPA_PLATFORM=xcb \
            /usr/bin/kodosi-qt --smoke-test
        DISPLAY=127.0.0.1:99 QT_QPA_PLATFORM=xcb \
            /usr/bin/kodosi-qt --smoke-test-agent-intel
        DISPLAY=127.0.0.1:99 QT_QPA_PLATFORM=xcb \
            /usr/bin/kodosi-qt --smoke-test-attention

        export XDG_RUNTIME_DIR=/work/weston-runtime
        mkdir -p "$XDG_RUNTIME_DIR"
        chmod 700 "$XDG_RUNTIME_DIR"
        weston --backend=headless-backend.so \
            --socket=wayland-9 \
            --idle-time=0 >/work/weston.log 2>&1 &
        weston_pid=$!
        for _ in $(seq 1 50); do
            test ! -S "$XDG_RUNTIME_DIR/wayland-9" || break
            sleep 0.1
        done
        WAYLAND_DISPLAY=wayland-9 \
            QT_QPA_PLATFORM=wayland \
            /usr/bin/kodosi-qt --smoke-test
        WAYLAND_DISPLAY=wayland-9 \
            QT_QPA_PLATFORM=wayland \
            /usr/bin/kodosi-qt --smoke-test-agent-intel
        WAYLAND_DISPLAY=wayland-9 \
            QT_QPA_PLATFORM=wayland \
            /usr/bin/kodosi-qt --smoke-test-attention
    '

echo "Linux desktop backends verified under X11 and Wayland"
