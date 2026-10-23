#include "GrabCursor.h"

#if !JUCE_MAC
// Other platforms have no closed hand: the open one stays.
juce::MouseCursor motion::grabbingCursor() {
    return juce::MouseCursor::DraggingHandCursor;
}
#endif
