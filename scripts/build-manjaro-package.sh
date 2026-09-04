#!/usr/bin/env bash

set -euo pipefail

cd "$(dirname "$0")/.."

version=0.1.0
archive=${1:-"build/release/kodosi-${version}-linux-x86_64.tar.gz"}
archive=$(realpath "$archive")
output_dir=${2:-"build/release"}
mkdir -p "$output_dir"
output_dir=$(realpath "$output_dir")
image='archlinux@sha256:b860afd5823683f7ea389ba5f00d812f4fe55f6f286dea329d2abeefa535e309'
epoch=${SOURCE_DATE_EPOCH:-$(python3 scripts/source-date-epoch.py --source-root .)}
package=$(
    SOURCE_DATE_EPOCH="$epoch" \
        ./scripts/write-arch-package.sh "$archive" "$output_dir"
)
package=$(realpath "$package")
./scripts/verify-packaged-content.sh "$package"

docker run --rm \
    -v "$package:/work/kodosi.pkg.tar.zst:ro" \
    "$image" \
    bash -lc '
        set -euo pipefail
        pacman -Syu --needed --noconfirm \
            at-spi2-core cairo desktop-file-utils libdrm libglvnd \
            fontconfig freetype2 mesa gdk-pixbuf2 gtk3 harfbuzz pango \
            wayland libx11 libxcb xcb-util-cursor xcb-util-image \
            xcb-util-keysyms xcb-util-renderutil xcb-util-wm \
            libxkbcommon-x11 xdg-desktop-portal xdg-utils \
            >/dev/null
        pacman -U --noconfirm /work/kodosi.pkg.tar.zst >/dev/null
        test -x /usr/bin/kodosi-qt
        test -x /usr/bin/kodosi
    '

echo "Manjaro package verified: $(basename "$package")"
