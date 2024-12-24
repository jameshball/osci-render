#!/bin/bash

if [[ "$(uname)" != "Darwin" ]]; then
    echo "Not macos"
    exit 0
fi

# Path to the project.pbxproj file
PBXPROJ_FILE="$1"
PROJECT_NAME="$2"

if [[ ! -f "$PBXPROJ_FILE" ]]; then
    echo "Error: File '$PBXPROJ_FILE' not found!"
    exit 1
fi

# Temporary file for storing embed framework blocks and the final output
TEMP_EMBED_FILE="$(mktemp)"
TEMP_FILE="$(mktemp)"

# Step 1: Find all 'Embed Frameworks' blocks and their IDs
echo "Debug: Extracting Embed Frameworks blocks and IDs..."
awk '
    BEGIN { in_embed_block = 0; current_id = ""; embed_block = ""; }
    {
        # Look for the start of an Embed Frameworks block
        if ($0 ~ /^[[:space:]]*[A-Z0-9]+[[:space:]]*\/\* Embed Frameworks \*\/[[:space:]]*=/) {
            in_embed_block = 1;
            split($0, parts, " ");
            current_id = parts[1];
            embed_block = $0 "\n";  # Start of the block
        }

        # Add lines to the block if we are inside it
        if (in_embed_block) {
            embed_block = embed_block $0 "\n";
        }

        # Look for the end of the Embed Frameworks block
        if (in_embed_block && $0 ~ /^[[:space:]]*};/) {
            in_embed_block = 0;
            # Store the embed block and ID
            print current_id "\t" embed_block;
            embed_block = "";  # Reset the block for the next one
            current_id = "";
        }
    }
' "$PBXPROJ_FILE" > "$TEMP_EMBED_FILE"

# Step 2: Find the ID of the 'VST3 Manifest Helper' target
TARGET_ID=$(grep -E "^\s*[A-Z0-9]+ \/\* $PROJECT_NAME - VST3 Manifest Helper \*\/ = {" "$PBXPROJ_FILE" | grep -oE '^\s*[A-Z0-9]+' | tr -d '[:space:]')

if [[ -z "$TARGET_ID" ]]; then
    echo "Error: Could not find the 'VST3 Manifest Helper' target!"
    exit 1
fi

echo "Debug: Found target ID: $TARGET_ID"

# Step 3: Extract the buildPhases for the 'VST3 Manifest Helper' target
echo "Debug: Extracting buildPhases for the 'VST3 Manifest Helper' target..."
BUILD_PHASES=$(awk -v target_id="$TARGET_ID" -v project_name="$PROJECT_NAME" '
    BEGIN { found_target = 0; in_build_phases = 0; }
    {
        # Look for the target block
        if ($0 ~ target_id " /\\* " project_name " - VST3 Manifest Helper \\*/ = \\{") { found_target = 1; }

        # Capture the buildPhases section
        if (found_target && $0 ~ /buildPhases = \(/) { 
            in_build_phases = 1; 
        }
        
        if (in_build_phases) {
            print $0;
        }

        # Stop capturing buildPhases after the closing parenthesis
        if (in_build_phases && $0 ~ /^[[:space:]]*\);/) { 
            in_build_phases = 0;
        }

        # End target block
        if (found_target && $0 ~ /^[[:space:]]*\};/) { 
            found_target = 0; 
        }
    }
' "$PBXPROJ_FILE" | grep -oE '^\s*[A-Z0-9]+' | tr -d "[:blank:]")

# Debug: Show the captured buildPhases
echo "Debug: Build Phases for 'VST3 Manifest Helper':"
echo "$BUILD_PHASES"

# Step 4: Extract the Embed Framework ID that matches the build phase ID
echo "Debug: Matching Embed Frameworks with Build Phases..."
EMBED_ID=""
while read -r build_phase_id; do
    # For each build phase ID, check if it exists in TEMP_EMBED_FILE
    # Extract the matching Embed Framework ID from TEMP_EMBED_FILE
    EMBED_ID=$(grep "$build_phase_id" "$TEMP_EMBED_FILE")

    # If a matching Embed ID is found, exit the loop
    if [[ -n "$EMBED_ID" ]]; then
        EMBED_ID=$build_phase_id
        echo "Debug: Found matching Embed ID: $EMBED_ID"
        break
    fi
done <<< "$BUILD_PHASES"

if [[ -z "$EMBED_ID" ]]; then
    echo "Error: Could not find a matching Embed Frameworks ID in the build phases!"
    exit 1
fi

# Step 5: Remove the 'Embed Frameworks' block and references to it
echo "Debug: Removing the Embed Frameworks block with ID $EMBED_ID..."
awk -v embed_id="$EMBED_ID" '
{
    # Skip the Embed Frameworks block
    if ($0 ~ embed_id " \\*/ = {") {in_embed_block=1}
    if (in_embed_block && $0 ~ "^[[:space:]]*};") {in_embed_block=0; next}
    if (in_embed_block) next

    # Remove references to the Embed Frameworks block in the build phases
    if ($0 ~ "buildPhases = \\(") {in_build_phases=1}
    if (in_build_phases && $0 ~ embed_id ",") {next}
    if (in_build_phases && $0 ~ "^[[:space:]]*\\);") {in_build_phases=0}

    # Print the remaining lines
    print
}' "$PBXPROJ_FILE" > "$TEMP_FILE"

# Replace the original file with the modified file
mv "$TEMP_FILE" "$PBXPROJ_FILE"

echo "Successfully removed the Embed Frameworks block for 'VST3 Manifest Helper'."
