#pragma once

#include <JuceHeader.h>

namespace osci {

inline bool isJucewrightAutomationLaunch() {
#if DEBUG && JUCE_MODULE_AVAILABLE_jucewright
    return juce::SystemStats::getEnvironmentVariable("JUCEWRIGHT_AUTOMATION", {}).isNotEmpty();
#else
    return false;
#endif
}

} // namespace osci
