#pragma once

#include <JuceHeader.h>
#include <osci_gui/osci_gui.h>

class RecordingSettingsOverlay final : public osci::ComponentOverlay {
public:
    RecordingSettingsOverlay(juce::Component& content,
                             juce::Point<int> preferredContentSize)
        : osci::ComponentOverlay(content,
                                 "Recording Settings",
                                 preferredContentSize,
                                 true) {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecordingSettingsOverlay)
};
