#!/usr/bin/env bash

set -euo pipefail

root=${1:?Usage: verify-installed-package-content.sh <root>}
cd "$(dirname "$0")/.."
root=$(realpath "$root")

test -x "$root/usr/bin/kodosi"
test -x "$root/usr/bin/kodosi-qt"
test -x "$root/usr/lib/kodosi/bin/kodosi"
readelf -h "$root/usr/lib/kodosi/bin/kodosi" | grep -F 'ELF64' >/dev/null
if ldd "$root/usr/lib/kodosi/bin/kodosi" | grep -Fq 'not found'; then
    echo "Bundled Kodosi CLI has unresolved ELF dependencies" >&2
    exit 1
fi
if find "$root" -type f -name 'kodosi-ui-probe' -print -quit | grep -q .; then
    echo "Developer-only UI probe was packaged" >&2
    exit 1
fi

cmp dependencies.lock.json \
    "$root/usr/share/doc/kodosi/provenance/dependencies.lock.json"
python3 scripts/verify-source-identity.py \
    --identity "$root/usr/share/doc/kodosi/provenance/source-identity.json" \
    --expected build/release/generated/source-identity.json
cmp ../kodosi-ghostty/LICENSE \
    "$root/usr/share/doc/kodosi/ghostty/LICENSE"
cmp ../kodosi-ghostty/LICENSE-GHOSTTY \
    "$root/usr/share/doc/kodosi/ghostty/LICENSE-GHOSTTY"
cmp ../kodosi-ghostty/THIRD_PARTY_NOTICES.md \
    "$root/usr/share/doc/kodosi/ghostty/THIRD_PARTY_NOTICES.md"
cmp ../kodosi-ghostty/ThirdPartyNotices/linux-vt-inventory.json \
    "$root/usr/share/doc/kodosi/ghostty/linux-vt-inventory.json"
python3 scripts/verify-rust-license-inventory.py \
    --tree "$root/usr/share/doc/kodosi/rust" \
    --expected-tree build/release/generated/rust-dependency-licenses
python3 scripts/verify-native-license-evidence.py \
    --installed-root "$root"

notice_names=(
    highway-Apache-2.0.txt
    highway-BSD-3-Clause.txt
    simdutf-MIT.txt
    simdutf-PyTorch-BSD-3-Clause.txt
    simdutf-Fuchsia-BSD-3-Clause.txt
    uucode.txt
    uucode-Bjoern-Hoehrmann.txt
    unicode-data.txt
    wuffs.txt
    zig-compiler-runtime.txt
    compiler-rt-LLVM-derived.txt
    zig-runtime-Apache-2.0-LLVM-exception.txt
    compiler-rt-musl-derived.txt
    zig-runtime-MIT-derived.txt
)
for notice in "${notice_names[@]}"; do
    cmp "../kodosi-ghostty/ThirdPartyNotices/licenses/$notice" \
        "$root/usr/share/doc/kodosi/ghostty/ThirdPartyNotices/licenses/$notice"
done

grep -Fq '698fe24f3c5137904128085a786a1ce777c6e153' \
    "$root/usr/share/doc/kodosi/NATIVE-DESKTOP-INTEGRATION-NOTICE.txt"
grep -Fq '77ebed9ed18de493296c5d4c2afc39b1d6d52e40' \
    "$root/usr/share/doc/kodosi/NATIVE-DESKTOP-INTEGRATION-NOTICE.txt"
grep -Fq '8af6897c0afc63037a8a3efee4162a380e3a4572' \
    "$root/usr/share/doc/kodosi/NATIVE-DESKTOP-INTEGRATION-NOTICE.txt"
