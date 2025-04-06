#pragma once

// Helper function to determine if MIDI should be always enabled - e.g. for instrument plugins which use the daw MIDI input
inline bool isMidiAlwaysEnabled() {
#ifdef MIDI_ALWAYS_ENABLED
    return true;
#else
    return false;
#endif
}
