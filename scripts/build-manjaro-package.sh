#!/usr/bin/env bash

set -euo pipefail

cd "$(dirname "$0")/.."

version=0.1.0
archive=${1:-"build/release/kodosi-${version}-linux-x86_64.tar.gz"}
archive=$(realpath "$archive")
output_dir=${2:-"build/release"}
output_dir=$(realpath "$output_dir")
archive_name=$(basename "$archive")
archive_sha=$(sha256sum "$archive" | awk '{ print $1 }')
image='archlinux@sha256:b860afd5823683f7ea389ba5f00d812f4fe55f6f286dea329d2abeefa535e309'
stage="$PWD/build/manjaro-package"
rm -rf "$stage"
mkdir -p "$stage"
trap 'rm -rf "$stage"' EXIT

cp "$archive" "$stage/$archive_name"
sed \
    -e "s/@VERSION@/$version/g" \
    -e "s/@ARCHIVE@/$archive_name/g" \
    -e "s/@SHA256@/$archive_sha/g" \
    packaging/manjaro/PKGBUILD.in >"$stage/PKGBUILD"

docker run --rm \
    -e "HOST_UID=$(id -u)" \
    -v "$stage:/build" \
    "$image" \
    bash -lc '
        set -euo pipefail
        mkdir -p /build/temp
        export TMPDIR=/build/temp
        pacman -Syu --needed --noconfirm \
            base-devel at-spi2-core brotli cairo dbus libdrm libglvnd \
            fontconfig freetype2 mesa gdk-pixbuf2 krb5 gtk3 harfbuzz pango \
            wayland libx11 libxcb xcb-util-cursor xcb-util-image \
            xcb-util-keysyms xcb-util-renderutil xcb-util-wm \
            libxkbcommon-x11 xdg-desktop-portal xdg-utils zstd zlib \
            >/build/pacman.log
        useradd --create-home --uid "$HOST_UID" builder
        chown -R builder:builder /build
        su builder -c "cd /build && makepkg --noconfirm --clean"
        pacman -U --noconfirm /build/kodosi-bin-[0-9]*.pkg.tar.zst >/build/install.log
        QT_QPA_PLATFORM=offscreen QT_QPA_PLATFORMTHEME= \
            QSG_RHI_BACKEND=software /usr/bin/kodosi-qt --smoke-test
    '

package=$(find "$stage" -maxdepth 1 -name 'kodosi-bin-[0-9]*.pkg.tar.zst' -print -quit)
test -n "$package"
cp "$package" "$output_dir/"
echo "Manjaro package verified: $(basename "$package")"
