#pragma once

#include <JuceHeader.h>
#include <osci_gui/osci_gui.h>

class OfflineRenderOverlay final : public osci::ComponentOverlay {
public:
    OfflineRenderOverlay(std::unique_ptr<juce::Component> content,
                         juce::Point<int> preferredContentSize)
        : osci::ComponentOverlay(std::move(content),
                                 "Render Audio File to Video",
                                 preferredContentSize,
                                 true) {
        setDismissible(false);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OfflineRenderOverlay)
};
