#pragma once

#include "FileSelectionController.h"

class OscirenderAudioProcessor;
class VoiceManager;

FileSelectionController::Dependencies createFileSelectionDependencies(
    OscirenderAudioProcessor& processor, VoiceManager& voices,
    std::function<void(int, juce::String, juce::String)> errorCallback);
