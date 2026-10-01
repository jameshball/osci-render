#pragma once

#include <JuceHeader.h>
#include <osci_gui/osci_gui.h>
#include <osci_standalone/osci_standalone.h>

// Reads JUCE's existing device measurements on the message thread. No extra
// timers, allocations or instrumentation are added to the audio callback.
class MotionPlaybackHealth final : public juce::TextButton, private juce::Timer {
public:
    MotionPlaybackHealth() {
        setName("Playback health");
        setButtonText("No audio");
        setWantsKeyboardFocus(false);
        diagnostics.setName("Playback diagnostics");
        addChildComponent(diagnostics);
        startTimerHz(2);
    }
    std::function<bool()> isPreparing;
    juce::String summary() const { return description; }

private:
    void timerCallback() override {
        auto* standalone = juce::StandalonePluginHolder::getInstance();
        auto* device = standalone != nullptr ? standalone->deviceManager.getCurrentAudioDevice() : nullptr;
        auto data = std::make_unique<juce::DynamicObject>();
        data->setProperty("observed_ms", juce::Time::getMillisecondCounterHiRes());
        data->setProperty("available", device != nullptr);
        data->setProperty("preparing", isPreparing && isPreparing());
        if (device == nullptr) {
            setButtonText("No audio");
            removeColour(juce::TextButton::textColourOffId);
            description = "No active standalone audio device. Open Audio settings to choose an output.";
        } else {
            const auto load = standalone->deviceManager.getCpuUsage();
            const auto xruns = standalone->deviceManager.getXRunCount();
            const auto rate = device->getCurrentSampleRate();
            const auto block = device->getCurrentBufferSizeSamples();
            const auto percent = juce::String(load * 100, 1);
            setButtonText("DSP " + juce::String(juce::roundToInt(load * 100)) + "%");
            // Orange while the callback is heavy or has just dropped out.
            const auto heavy = load > 0.8 || xruns > lastXruns;
            lastXruns = xruns;
            if (heavy) { setColour(juce::TextButton::textColourOffId, juce::Colour(0xffff8a5c)); } else { removeColour(juce::TextButton::textColourOffId); }
            description = "Audio callback load: " + percent + "%\nDevice: " + device->getName()
                + "\nSample rate: " + juce::String(rate, 0) + " Hz\nBuffer: " + juce::String(block) + " samples"
                + "\nReported underruns/overruns: " + juce::String(xruns)
                + "\n\nLoad is a smoothed callback measurement, not total system CPU. "
                  "Underruns/overruns include what the audio device reports and blocks that missed their deadline since the device counters were reset. "
                  "A low average does not rule out brief overloads."
                + (xruns > 0 && block < 256 ? "\n\nDropouts at a " + juce::String(block) + "-sample buffer: try 256 or 512 samples in Audio > Settings." : juce::String());
            data->setProperty("device", device->getName());
            data->setProperty("cpu_load", load);
            data->setProperty("xruns", xruns);
            data->setProperty("sample_rate", rate);
            data->setProperty("block_size", block);
        }
        setTooltip(description);
        diagnostics.setText(juce::JSON::toString(juce::var(data.release()), true), juce::dontSendNotification);
    }
    juce::Label diagnostics;
    int lastXruns = 0;
    juce::String description = "Waiting for audio-device measurements.";
};
