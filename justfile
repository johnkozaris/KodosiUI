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

release-integrity-test:
    python3 -m unittest tests/test_release_integrity.py
    python3 -m unittest tests/test_reproducible_archives.py

rust-license-test:
    python3 scripts/verify-pinned-checkouts.py \
        --dependencies dependencies.lock.json \
        --runtime-root ../Kodosi \
        --ghostty-package-root ../kodosi-ghostty
    rm -rf build/rust-license-test
    python3 scripts/generate-rust-license-inventory.py \
        --manifest ../Kodosi/rustProcess/Cargo.toml \
        --output-dir build/rust-license-test
    python3 scripts/verify-rust-license-inventory.py \
        --tree build/rust-license-test

native-license-test: bootstrap
    python3 scripts/verify-native-license-evidence.py
    python3 -m unittest tests/test_native_license_evidence.py

check: parity visual-parity release-integrity-test rust-license-test native-license-test lint test

package: parity bootstrap
    mkdir -p build/release
    git status --porcelain=v1 --untracked-files=no \
        > build/release/tracked-status.before
    SOURCE_DATE_EPOCH="$(python3 scripts/source-date-epoch.py --source-root .)" \
        CMAKE_PREFIX_PATH="{{ qt_dir }}" "{{ cmake }}" --preset release
    SOURCE_DATE_EPOCH="$(python3 scripts/source-date-epoch.py --source-root .)" \
        "{{ cmake }}" --build --preset release
    "{{ cmake }}" --build build/release --target test_prep/all
    QT_QPA_PLATFORM=offscreen QSG_RHI_BACKEND=software \
        "{{ ctest }}" --preset release
    cd build/release && \
        umask 022 && \
        SOURCE_DATE_EPOCH="$(python3 ../../scripts/source-date-epoch.py \
            --source-root ../..)" \
        "{{ cmake }}" --build . --target package
    SOURCE_DATE_EPOCH="$(python3 scripts/source-date-epoch.py --source-root .)" \
        ./scripts/normalize-cpack-packages.sh build/release
    git status --porcelain=v1 --untracked-files=no \
        > build/release/tracked-status.after
    if ! cmp -s build/release/tracked-status.before \
        build/release/tracked-status.after; then \
        echo "Package build modified tracked source files" >&2; \
        diff build/release/tracked-status.before \
            build/release/tracked-status.after || true; \
        exit 1; \
    fi
    ./scripts/verify-linux-package.sh build/release/kodosi_0.1.0_amd64.deb
    ./scripts/verify-packaged-content.sh build/release/kodosi-0.1.0-linux-x86_64.tar.gz

bundled-cli-test: package
    ./scripts/verify-packaged-content.sh build/release/kodosi_0.1.0_amd64.deb

package-manjaro: package
    ./scripts/build-manjaro-package.sh

package-repeatability: package package-repeatability-existing

package-repeatability-existing: bootstrap
    ./scripts/verify-reproducible-packages.sh

release-manifest: package-repeatability package-manjaro release-manifest-existing

release-manifest-existing:
    python3 scripts/generate-release-manifest.py \
        --source-root . \
        --runtime-root ../Kodosi \
        --ghostty-package-root ../kodosi-ghostty \
        --output-dir build/release \
        --artifact kodosi_0.1.0_amd64.deb \
        --artifact kodosi-0.1.0-linux-x86_64.tar.gz \
        --artifact kodosi-bin-0.1.0-1-x86_64.pkg.tar.zst
    python3 scripts/verify-release-manifest.py \
        --source-root . \
        --runtime-root ../Kodosi \
        --ghostty-package-root ../kodosi-ghostty \
        --manifest build/release/release-manifest.json \
        --output-dir build/release

sign-release-manifest fingerprint="":
    ./scripts/sign-release-manifest.sh \
        build/release/release-manifest.json \
        "{{ fingerprint }}"

verify-release-signature fingerprint="":
    ./scripts/verify-release-signature.sh \
        build/release/release-manifest.json \
        build/release/release-manifest.json.asc \
        "{{ fingerprint }}"

package-desktop-smoke: package
    ./scripts/verify-linux-desktops.sh
