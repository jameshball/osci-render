#!/usr/bin/env bash
set -euo pipefail

# CI runtime, pinned by version and verified against Blender's checksums.
version="${1:?Blender version required}"
destination="${2:?Destination required}"
series="${version%.*}"
case "${3:-Linux}" in
    Linux) filename="blender-${version}-linux-x64.tar.xz" ;;
    Windows) filename="blender-${version}-windows-x64.zip" ;;
    *) echo "Expected Linux or Windows" >&2; exit 1 ;;
esac
base="https://download.blender.org/release/Blender${series}"
mkdir -p "$destination"
curl --fail --location --retry 3 "$base/$filename" --output "$destination/$filename"
curl --fail --location --retry 3 "$base/blender-${version}.sha256" --output "$destination/checksums.txt"
(
    cd "$destination"
    awk -v file="$filename" '$2 == file || $2 == "*" file' checksums.txt > selected.sha256
    test -s selected.sha256
    sha256sum --check selected.sha256
    if [[ "$filename" == *.zip ]]; then
        unzip -q "$filename"
    else
        tar -xf "$filename"
    fi
)
