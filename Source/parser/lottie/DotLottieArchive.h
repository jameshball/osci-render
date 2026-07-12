#pragma once

#include <JuceHeader.h>

namespace osci::lottie {
juce::String extractAnimationJsonFromDotLottie(const juce::MemoryBlock& archiveData);
}
