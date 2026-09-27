#pragma once

#include <JuceHeader.h>
#include <osci_gui/osci_gui.h>
#include "../model/TextSettings.h"

class MotionTextSourcePanel final : public juce::Component {
public:
    MotionTextSourcePanel(const juce::String& initial, std::size_t instances, motion::TextSettings initialSettings) : original(initial), originalSettings(initialSettings), settings(initialSettings) {
        setName("Text source editor");
        text.setName("Source text");
        text.setMultiLine(true);
        text.setReturnKeyStartsNewLine(true);
        text.setFont(settings.font(16));
        updateTextLayout();
        text.setColour(juce::TextEditor::backgroundColourId, osci::Colours::veryDark());
        text.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        text.setText(initial, false);
        text.onTextChange = [this] { refresh(); };
        help.setFont(juce::FontOptions(13));
        help.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        help.setText("Shared source: " + juce::String(static_cast<juce::uint64>(instances)) + (instances == 1 ? " clip" : " clips") + " will update. Clip animation stays unchanged.", juce::dontSendNotification);
        help.setJustificationType(juce::Justification::centredLeft);
        status.setFont(juce::FontOptions(12));
        status.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        families = juce::Font::findAllTypefaceNames();
        families.sort(true);
        family.setName("Text font family");
        family.addItem("Default", 1);
        for (int i = 0; i < families.size(); ++i) { family.addItem(families[i], i + 2); }
        if (settings.family.isNotEmpty() && !families.contains(settings.family)) {
            families.add(settings.family);
            family.addItem(settings.family + " (not installed)", families.size() + 1);
            missingFamily = settings.family;
        }
        family.setSelectedId(settings.family.isEmpty() ? 1 : families.indexOf(settings.family) + 2, juce::dontSendNotification);
        style.setName("Text font style");
        style.addItemList({"Regular", "Bold", "Italic", "Bold italic"}, 1);
        style.setSelectedId(settings.style + 1, juce::dontSendNotification);
        alignment.setName("Text alignment");
        alignment.addItemList({"Left", "Centre", "Right"}, 1);
        alignment.setSelectedId(settings.alignment + 1, juce::dontSendNotification);
        lineSpacing.setName("Text line spacing");
        tracking.setName("Text tracking");
        lineSpacing.setRange(0.5, 4.0, 0.05);
        tracking.setRange(-0.2, 1.0, 0.01);
        lineSpacing.setValue(settings.lineSpacing, juce::dontSendNotification);
        tracking.setValue(settings.tracking, juce::dontSendNotification);
        for (auto* slider : {&lineSpacing, &tracking}) {
            slider->setSliderStyle(juce::Slider::IncDecButtons);
            slider->setTextBoxStyle(juce::Slider::TextBoxLeft, false, 70, 26);
            slider->setTextValueSuffix(" em");
            slider->textFromValueFunction = [](double value) { return juce::String(std::abs(value) < 0.00001 ? 0.0 : value, 2); };
            slider->onValueChange = [this] { settingsChanged(); };
        }
        for (auto* combo : {&family, &style, &alignment}) {
            combo->setColour(juce::ComboBox::backgroundColourId, osci::Colours::veryDark());
            combo->setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
            combo->onChange = [this] { settingsChanged(); };
        }
        const std::array<juce::String, 5> captions {"Font", "Style", "Alignment", "Line spacing", "Tracking"};
        for (std::size_t i = 0; i < labels.size(); ++i) {
            labels[i].setText(captions[i], juce::dontSendNotification);
            labels[i].setFont(juce::FontOptions(13));
            labels[i].setColour(juce::Label::textColourId, osci::Colours::textMuted());
            addAndMakeVisible(labels[i]);
        }
        apply.setButtonText("Apply text");
        apply.onClick = [this] { if (apply.isEnabled() && onApply) { onApply(text.getText(), settings); } };
        for (auto* component : std::initializer_list<juce::Component*>{&text, &help, &status, &apply, &family, &style, &alignment, &lineSpacing, &tracking}) { addAndMakeVisible(component); }
        refresh();
    }
    std::function<void(juce::String, motion::TextSettings)> onApply;
    void resized() override {
        auto area = getLocalBounds().reduced(12);
        help.setBounds(area.removeFromTop(42));
        auto footer = area.removeFromBottom(36);
        apply.setBounds(footer.removeFromRight(108).reduced(0, 4));
        status.setBounds(footer);
        auto fontRow = area.removeFromTop(32);
        const auto columnWidth = fontRow.getWidth() / 2;
        auto left = fontRow.removeFromLeft(columnWidth);
        labels[0].setBounds(left.removeFromLeft(48));
        family.setBounds(left.reduced(2, 3));
        labels[1].setBounds(fontRow.removeFromLeft(48));
        style.setBounds(fontRow.reduced(2, 3));
        auto layoutRow = area.removeFromTop(52);
        const auto third = layoutRow.getWidth() / 3;
        const std::array<juce::Component*, 3> controls {&alignment, &lineSpacing, &tracking};
        for (std::size_t i = 0; i < controls.size(); ++i) {
            auto cell = layoutRow.removeFromLeft(third).reduced(2, 0);
            labels[i + 2].setBounds(cell.removeFromTop(20));
            controls[i]->setBounds(cell.reduced(0, 3));
        }
        text.setBounds(area.reduced(0, 6));
    }
private:
    void updateTextLayout() {
        const auto horizontal = settings.alignment == 1 ? juce::Justification::horizontallyCentred : settings.alignment == 2 ? juce::Justification::right : juce::Justification::left;
        text.setJustification(juce::Justification::top | horizontal);
        text.setLineSpacing(static_cast<float>(std::max(1.0, settings.lineSpacing)));
    }
    void settingsChanged() {
        const auto index = family.getSelectedId() - 2;
        settings.family = index >= 0 && index < families.size() ? families[index] : juce::String();
        settings.style = style.getSelectedId() - 1;
        settings.alignment = alignment.getSelectedId() - 1;
        settings.lineSpacing = std::round(lineSpacing.getValue() * 20.0) / 20.0;
        settings.tracking = std::round(tracking.getValue() * 100.0) / 100.0;
        text.applyFontToAllText(settings.font(16));
        updateTextLayout();
        refresh();
    }
    void refresh() {
        const auto content = text.getText();
        const bool valid = content.isNotEmpty() && content.length() <= 16384;
        apply.setEnabled(valid && (content != original || settings != originalSettings));
        status.setText(content.length() > 16384 ? "Maximum 16,384 characters" : (settings.family.isNotEmpty() && settings.family == missingFamily ? "Font not installed; system fallback used" : juce::String(content.length()) + " characters"), juce::dontSendNotification);
    }
    const juce::String original;
    const motion::TextSettings originalSettings;
    motion::TextSettings settings;
    juce::StringArray families;
    juce::String missingFamily;
    juce::ComboBox family, style, alignment;
    juce::Slider lineSpacing, tracking;
    std::array<juce::Label, 5> labels;
    juce::TextEditor text;
    juce::Label help, status;
    juce::TextButton apply;
};
