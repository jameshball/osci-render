#include "OutputChannels.h"

namespace motion {
// A transaction: the player is detached before bus layouts change or the
// hardware restarts, and an unavailable channel count is refused before
// anything is touched.
juce::Result configureOutputOnlyChannels(juce::StandalonePluginHolder& holder, int requestedChannels) {
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) {
        return juce::Result::fail("Output channels must be configured on the message thread.");
    }
    auto* device = holder.deviceManager.getCurrentAudioDevice();
    if (holder.processor == nullptr || device == nullptr) {
        return juce::Result::fail("No audio processor or output device is available.");
    }
    if (requestedChannels <= 0 || device->getOutputChannelNames().size() < requestedChannels) {
        return juce::Result::fail("The current audio device does not expose " + juce::String(requestedChannels) + " output channels.");
    }
    const auto previousLayout = holder.processor->getBusesLayout();
    const auto previousSetup = holder.deviceManager.getAudioDeviceSetup();
    const auto previousOutputs = device->getActiveOutputChannels();
    auto layout = previousLayout;
    if (layout.outputBuses.isEmpty()) {
        return juce::Result::fail("The processor has no output bus.");
    }
    for (auto& input : layout.inputBuses) {
        input = juce::AudioChannelSet::disabled();
    }
    for (auto& output : layout.outputBuses) {
        output = juce::AudioChannelSet::disabled();
    }
    layout.outputBuses.set(0, requestedChannels == 2 ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::discreteChannels(requestedChannels));
    if (!holder.processor->checkBusesLayoutSupported(layout)) {
        return juce::Result::fail("The processor does not support the requested output layout.");
    }
    auto setup = previousSetup;
    setup.useDefaultOutputChannels = false;
    // Preserve the user's physical channel mapping when it already fits.
    setup.outputChannels = device->getActiveOutputChannels();
    if (setup.outputChannels.countNumberOfSetBits() != requestedChannels) {
        setup.outputChannels.clear();
        setup.outputChannels.setRange(0, requestedChannels, true);
    }
    if (previousLayout == layout && previousOutputs.countNumberOfSetBits() == requestedChannels) {
        return juce::Result::ok();
    }
    holder.stopPlaying();
    const auto rollback = [&](juce::String reason) {
        holder.stopPlaying();
        const auto restoredLayout = holder.processor->setBusesLayout(previousLayout);
        const auto restoreError = holder.deviceManager.setAudioDeviceSetup(previousSetup, true);
        holder.startPlaying();
        const auto* restoredDevice = holder.deviceManager.getCurrentAudioDevice();
        const auto restored = restoredDevice != nullptr
            && restoredDevice->getActiveOutputChannels() == previousOutputs
            && holder.processor->getBusesLayout() == previousLayout;
        if (!restoredLayout || restoreError.isNotEmpty() || !restored) {
            reason += " Restoring the previous audio configuration also failed";
            if (restoreError.isNotEmpty()) {
                reason += ": " + restoreError;
            }
            reason += ". Check Audio Settings.";
        }
        return juce::Result::fail(reason);
    };
    if (!holder.processor->setBusesLayout(layout)) {
        return rollback("Couldn't switch to the chosen outputs.");
    }
    const auto error = holder.deviceManager.setAudioDeviceSetup(setup, true);
    if (error.isNotEmpty()) {
        return rollback("The audio device couldn't turn on the chosen outputs: " + error);
    }
    const auto matches = [&] {
        const auto* currentDevice = holder.deviceManager.getCurrentAudioDevice();
        return currentDevice != nullptr
            && currentDevice->getActiveOutputChannels().countNumberOfSetBits() == requestedChannels
            && holder.processor->getTotalNumOutputChannels() == requestedChannels
            && holder.processor->getTotalNumInputChannels() == 0;
    };
    if (!matches()) {
        return rollback("The audio device or processor did not activate the requested channel count.");
    }
    holder.startPlaying();
    // AudioProcessorPlayer may negotiate a layout when it reattaches.
    if (!matches()) {
        return rollback("Playback couldn't keep the chosen outputs.");
    }
    return juce::Result::ok();
}
}
