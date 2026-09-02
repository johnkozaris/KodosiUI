set shell := ["bash", "-euo", "pipefail", "-c"]

tools_dir := justfile_directory() + "/.tools"
cmake := tools_dir + "/cmake-4.4.3/bin/cmake"
ctest := tools_dir + "/cmake-4.4.3/bin/ctest"
qt_dir := tools_dir + "/Qt/6.11.2/gcc_64"

bootstrap:
    ./scripts/bootstrap-tools.sh

parity:
    python3 scripts/verify-parity.py

visual-parity:
    python3 scripts/verify-visual-parity.py

configure: bootstrap
    CMAKE_PREFIX_PATH="{{ qt_dir }}" "{{ cmake }}" --preset dev

build: configure
    "{{ cmake }}" --build --preset dev

ui-probe-build: configure
    "{{ cmake }}" --build build/dev --target kodosi-ui-probe

ui-probe *args: ui-probe-build
    build/dev/src/kodosi-ui-probe {{ args }}

ui-probe-smoke: build ui-probe-build
    ./scripts/smoke-ui-probe.sh

ui-probe-input-status: ui-probe-build
    build/dev/src/kodosi-ui-probe input-status

ui-probe-input-start *args: ui-probe-build
    build/dev/src/kodosi-ui-probe input-start {{ args }}

ui-probe-input-stop *args: ui-probe-build
    build/dev/src/kodosi-ui-probe input-stop {{ args }}

lint: configure
    "{{ cmake }}" --build build/dev --target all_qmllint

test: build
    "{{ cmake }}" --build build/dev --target test_prep/all
    "{{ ctest }}" --preset dev

check: parity visual-parity lint test

package: parity bootstrap
    CMAKE_PREFIX_PATH="{{ qt_dir }}" "{{ cmake }}" --preset release
    "{{ cmake }}" --build --preset release
    "{{ cmake }}" --build build/release --target test_prep/all
    QT_QPA_PLATFORM=offscreen QSG_RHI_BACKEND=software \
        "{{ ctest }}" --preset release
    cd build/release && "{{ cmake }}" --build . --target package
    ./scripts/verify-linux-package.sh build/release/kodosi_0.1.0_amd64.deb

package-manjaro: package
    ./scripts/build-manjaro-package.sh

package-desktop-smoke: package
    ./scripts/verify-linux-desktops.sh
