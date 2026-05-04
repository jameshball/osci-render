#pragma once

#include <JuceHeader.h>
#include <osci_gui/osci_gui.h>

class SyphonInputOverlay final : public osci::ComponentOverlay {
public:
    SyphonInputOverlay(std::unique_ptr<juce::Component> content,
                       juce::Point<int> preferredContentSize)
        : osci::ComponentOverlay(std::move(content),
                                 juce::String::createStringFromData(BinaryData::close_svg, BinaryData::close_svgSize),
                                 "Select Syphon/Spout Input",
                                 preferredContentSize,
                                 true) {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SyphonInputOverlay)
};
