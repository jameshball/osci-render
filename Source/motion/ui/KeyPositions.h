#pragma once

#include <JuceHeader.h>

namespace motion::ui {

// Keys by where they sit on the keyboard, named for what a US QWERTY board
// prints there, as games bind WASD: the four stay a cross of keys whatever
// the layout, so French AZERTY users press Z, Q, S and D.
enum class KeyPosition { w, a, s, d, q, e };

// What the key at that position types on the current layout, upper case
// ("Z" for KeyPosition::w on AZERTY), for showing in hints.
juce::juce_wchar keyAtPosition(KeyPosition position);

// Keys at several positions as they read on this layout: "WASD", "ZQSD".
inline juce::String keysAtPositions(std::initializer_list<KeyPosition> positions) {
    juce::String keys;
    for (const auto position : positions) { keys << juce::String::charToString(keyAtPosition(position)); }
    return keys;
}

// Whether the key at that position is held. Message thread only.
bool isKeyPositionDown(KeyPosition position);

} // namespace motion::ui
