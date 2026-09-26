#pragma once

#include <JuceHeader.h>
#include <osci_gui/osci_gui.h>

class MotionTextSourcePanel final : public juce::Component {
public:
    MotionTextSourcePanel(const juce::String& initial, int instances) : original(initial) {
        setName("Text source editor");
        text.setName("Source text");
        text.setMultiLine(true);
        text.setReturnKeyStartsNewLine(true);
        text.setFont(juce::FontOptions(16));
        text.setColour(juce::TextEditor::backgroundColourId, osci::Colours::veryDark());
        text.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        text.setText(initial, false);
        text.onTextChange = [this] { refresh(); };
        help.setFont(juce::FontOptions(13));
        help.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        help.setText("Shared source: " + juce::String(instances) + (instances == 1 ? " clip" : " clips") + " will update. Clip animation stays unchanged.", juce::dontSendNotification);
        help.setJustificationType(juce::Justification::centredLeft);
        status.setFont(juce::FontOptions(12));
        status.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        apply.setButtonText("Apply text");
        apply.onClick = [this] { if (apply.isEnabled() && onApply) { onApply(text.getText()); } };
        for (auto* component : std::initializer_list<juce::Component*>{&text, &help, &status, &apply}) { addAndMakeVisible(component); }
        refresh();
    }
    std::function<void(juce::String)> onApply;
    void resized() override {
        auto area = getLocalBounds().reduced(12);
        help.setBounds(area.removeFromTop(42));
        auto footer = area.removeFromBottom(36);
        apply.setBounds(footer.removeFromRight(108).reduced(0, 4));
        status.setBounds(footer);
        text.setBounds(area.reduced(0, 6));
    }
private:
    void refresh() {
        const auto content = text.getText();
        const bool valid = content.isNotEmpty() && content.length() <= 16384;
        apply.setEnabled(valid && content != original);
        status.setText(content.length() > 16384 ? "Maximum 16,384 characters" : juce::String(content.length()) + " characters", juce::dontSendNotification);
    }
    const juce::String original;
    juce::TextEditor text;
    juce::Label help, status;
    juce::TextButton apply;
};
