#pragma once

// Helper function to determine if MIDI should be always enabled
// This is used in both the processor and UI components
inline bool isMidiAlwaysEnabled() {
#ifdef MIDI_ALWAYS_ENABLED
    return true;
#else
    return false;
#endif
}

// Helper function to determine if the MIDI toggle should be shown in the UI
inline bool shouldShowMidiToggle() {
#ifdef MIDI_ALWAYS_ENABLED
    return false; // Hide the toggle in the MIDI-specific version
#else
    return true;  // Show the toggle in the regular version
#endif
}