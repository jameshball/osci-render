#include "ModulationState.h"

std::atomic<bool> ModulationState::anyDragActive{false};
juce::String ModulationState::highlightedParamId;
juce::String ModulationState::rangeParamId;
std::atomic<float> ModulationState::rangeDepth{0.0f};
std::atomic<bool> ModulationState::rangeBipolar{false};
