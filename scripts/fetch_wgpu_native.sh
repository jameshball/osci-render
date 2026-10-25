#!/usr/bin/env bash
# Fetches wgpu-native (WebGPU) for this machine into External/wgpu-native:
# include/webgpu/{webgpu,wgpu}.h and lib/ with its library: static, but on
# macOS one universal dynamic library from both architectures. A pinned
# release, checked against its SHA-256; nothing is fetched when that release
# is already there.
# osci-motion bakes hidden lines on the GPU through it.
#
#   scripts/fetch_wgpu_native.sh            # this machine's platform
#   OSCI_WGPU_PLATFORM=linux-aarch64 scripts/fetch_wgpu_native.sh
set -euo pipefail

VERSION="v29.0.1.1"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DEST="$ROOT/External/wgpu-native"
BASE="https://github.com/gfx-rs/wgpu-native/releases/download/$VERSION"

checksum() {
    case "$1" in
        wgpu-macos-aarch64-release.zip) echo a5797a37b1adf720bcd5dcffb291edbbd5b7b14be0a3874c28e6393a655a7a3e ;;
        wgpu-macos-x86_64-release.zip) echo 8e2f7378548ddd0e2cf21e7d864dda46e953f0af724855a33778b85ead206d41 ;;
        wgpu-linux-x86_64-release.zip) echo 95a4d90c071005a98d03eab348beaa6b07e16eb00d1dcdb9f8348f75eb97ec5a ;;
        wgpu-linux-aarch64-release.zip) echo 015fcdf1dbae82e614a783cc38017e5399ae0927a889fe9b69c9b664bc61b47a ;;
        wgpu-windows-x86_64-msvc-release.zip) echo 7e67d7445c42aeb85e30f88930fd8d7d83ee769e3390aeb1ada75ebf3cf78132 ;;
        wgpu-windows-aarch64-msvc-release.zip) echo 4a876421a8c1e5fe72f849b3722214280fe485cb1c56f77f8b0c82414be5b29f ;;
        *) echo "No checksum for $1" >&2; exit 1 ;;
    esac
}

platform="${OSCI_WGPU_PLATFORM:-}"
if [ -z "$platform" ]; then
    case "$(uname -s)" in
        Darwin) platform="macos" ;;
        Linux) [ "$(uname -m)" = "aarch64" ] || [ "$(uname -m)" = "arm64" ] && platform="linux-aarch64" || platform="linux-x86_64" ;;
        MINGW* | MSYS* | CYGWIN*) [ "${PROCESSOR_ARCHITECTURE:-}" = "ARM64" ] && platform="windows-aarch64" || platform="windows-x86_64" ;;
        *) echo "Unsupported platform: $(uname -s)" >&2; exit 1 ;;
    esac
fi

stamp="$DEST/.release"
if [ -f "$stamp" ] && [ "$(cat "$stamp")" = "$VERSION $platform" ]; then
    echo "wgpu-native $VERSION ($platform) already in $DEST"
    exit 0
fi

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Downloads and checks one release zip, then unpacks it into $work/<name>.
fetch() {
    local name="$1"
    echo "Fetching $name"
    curl -fsSL --retry 3 -o "$work/$name" "$BASE/$name"
    python3 - "$work/$name" "$(checksum "$name")" "$work/${name%.zip}" <<'EOF'
import hashlib, sys, zipfile
path, expected, into = sys.argv[1:4]
digest = hashlib.sha256(open(path, "rb").read()).hexdigest()
if digest != expected:
    sys.exit(f"{path}: SHA-256 {digest} is not the pinned {expected}")
zipfile.ZipFile(path).extractall(into)
EOF
}

rm -rf "$DEST"
mkdir -p "$DEST/lib"
case "$platform" in
    macos)
        fetch wgpu-macos-aarch64-release.zip
        fetch wgpu-macos-x86_64-release.zip
        cp -R "$work/wgpu-macos-aarch64-release/include" "$DEST/include"
        # A dynamic library on macOS: linked statically, its unwinding
        # (Rust's) would be a fourth kind beside C++'s, Objective-C's and
        # LuaJIT's, more than one image's compact unwind tables can hold. The
        # builds copy it beside the executable (into Frameworks in the app).
        # Joined, it needs signing again (ad hoc; release builds sign it as
        # they sign the app).
        lipo -create "$work/wgpu-macos-aarch64-release/lib/libwgpu_native.dylib" "$work/wgpu-macos-x86_64-release/lib/libwgpu_native.dylib" -output "$DEST/lib/libwgpu_native.dylib"
        codesign --force --sign - "$DEST/lib/libwgpu_native.dylib"
        ;;
    linux-x86_64 | linux-aarch64)
        fetch "wgpu-$platform-release.zip"
        cp -R "$work/wgpu-$platform-release/include" "$DEST/include"
        cp "$work/wgpu-$platform-release/lib/libwgpu_native.a" "$DEST/lib/"
        ;;
    windows-x86_64 | windows-aarch64)
        fetch "wgpu-$platform-msvc-release.zip"
        cp -R "$work/wgpu-$platform-msvc-release/include" "$DEST/include"
        cp "$work/wgpu-$platform-msvc-release/lib/wgpu_native.lib" "$DEST/lib/"
        ;;
    *) echo "Unknown platform $platform" >&2; exit 1 ;;
esac
echo "$VERSION $platform" > "$stamp"
echo "wgpu-native $VERSION ($platform) in $DEST"
