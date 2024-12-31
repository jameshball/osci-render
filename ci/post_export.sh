#!/bin/bash

if [[ "$(uname)" != "Darwin" ]]; then
    echo "Not macos"
    exit 0
fi

PROJECT_ROOT="$1"
PBXPROJ_FILE="$2"
PROJECT_NAME="$3"

sh -c "$PROJECT_ROOT/ci/remove_embed_frameworks.sh \"$PBXPROJ_FILE\" \"$PROJECT_NAME\" \"VST3 Manifest Helper"\"
sh -c "$PROJECT_ROOT/ci/remove_embed_frameworks.sh \"$PBXPROJ_FILE\" \"$PROJECT_NAME\" \"Shared Code"\"