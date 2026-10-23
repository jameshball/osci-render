#include "KeyPositions.h"

#if JUCE_MAC
 #include <ApplicationServices/ApplicationServices.h>
#elif JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#endif

namespace motion::ui {

namespace {
#if JUCE_WINDOWS
// Scan codes are positions; the layout maps them to keys and characters.
UINT virtualKeyAt(KeyPosition position) {
    const auto scanCode = [position]() -> UINT {
        switch (position) {
            case KeyPosition::w: return 0x11;
            case KeyPosition::a: return 0x1e;
            case KeyPosition::s: return 0x1f;
            case KeyPosition::d: return 0x20;
            case KeyPosition::q: return 0x10;
            case KeyPosition::e: return 0x12;
        }
        return 0x11;
    }();
    return MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK);
}
#endif
juce::juce_wchar usLetter(KeyPosition position) {
    switch (position) {
        case KeyPosition::w: return 'W';
        case KeyPosition::a: return 'A';
        case KeyPosition::s: return 'S';
        case KeyPosition::d: return 'D';
        case KeyPosition::q: return 'Q';
        case KeyPosition::e: return 'E';
    }
    return 0;
}
}

namespace {
// What the key at `position` types on the current layout, as typed (not
// upper-cased); 0 when that can't be asked or isn't one character.
juce::juce_wchar typedAt(KeyPosition position) {
    juce::juce_wchar typed = 0;
#if JUCE_MAC
    // The virtual key codes are positions (kVK_ANSI_W is where W sits on a US board).
    const auto code = [position]() -> CGKeyCode {
        switch (position) {
            case KeyPosition::w: return 13;
            case KeyPosition::a: return 0;
            case KeyPosition::s: return 1;
            case KeyPosition::d: return 2;
            case KeyPosition::q: return 12;
            case KeyPosition::e: return 14;
        }
        return 13;
    }();
    // An event made but never posted asks the current layout what the key types.
    auto event = CGEventCreateKeyboardEvent(nullptr, code, true);
    if (event != nullptr) {
        UniChar text[4] {};
        UniCharCount length = 0;
        CGEventKeyboardGetUnicodeString(event, 4, &length, text);
        CFRelease(event);
        if (length == 1) { typed = static_cast<juce::juce_wchar>(text[0]); }
    }
#elif JUCE_WINDOWS
    typed = static_cast<juce::juce_wchar>(MapVirtualKeyW(virtualKeyAt(position), MAPVK_VK_TO_CHAR) & 0x7fff);
#else
    juce::ignoreUnused(position);
#endif
    return typed;
}
}

juce::juce_wchar keyAtPosition(KeyPosition position) {
    const auto typed = typedAt(position);
    // A letter to show; anything else (or another platform) shows the US letter.
    return juce::CharacterFunctions::isLetter(typed) ? juce::CharacterFunctions::toUpperCase(typed) : usLetter(position);
}

bool isKeyPositionDown(KeyPosition position) {
#if JUCE_WINDOWS
    // Windows says which key is down by its virtual key, whatever it types.
    return (GetAsyncKeyState(static_cast<int>(virtualKeyAt(position))) & 0x8000) != 0;
#else
    // JUCE tracks keys held by what they type: the layout's letter as typed,
    // or the US letter where the layout's can't be had.
    const auto typed = typedAt(position);
    const auto letter = juce::CharacterFunctions::isLetter(typed) ? typed : usLetter(position);
    return juce::KeyPress::isKeyCurrentlyDown(static_cast<int>(letter)) || juce::KeyPress::isKeyCurrentlyDown(static_cast<int>(juce::CharacterFunctions::toUpperCase(letter)));
#endif
}

} // namespace motion::ui
