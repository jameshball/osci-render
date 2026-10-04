#pragma once

#include "MotionIcons.h"
#include "ScrubField.h"
#include "../model/TextSettings.h"

// Edits a text source inside the Scene, like the drawing editor: the words in
// their font on a dark page, a floating type bar, and the Scope showing the
// beam as you type. Per-character animation lives in the clip's Properties.
class MotionTextSourceEditor final : public juce::Component {
public:
    MotionTextSourceEditor(const juce::String& initial, const juce::String& title, motion::TextSettings initialSettings, const juce::String& preparationError)
        : settings(initialSettings), original(initial), originalSettings(initialSettings) {
        setName("Text editor");
        setWantsKeyboardFocus(true);
        name.setText(title, juce::dontSendNotification);
        name.setFont(motion::style::title());
        name.setBorderSize({0, 6, 0, 0});
        addAndMakeVisible(name);
        cancelButton.setButtonText("Cancel");
        cancelButton.setColour(juce::TextButton::buttonColourId, motion::style::raised());
        cancelButton.onClick = [this] { if (onCancel) { onCancel(); } };
        doneButton.setName("Apply text");
        doneButton.setTitle("Apply text");
        doneButton.setButtonText("Save");
        doneButton.setTooltip("Save the text");
        doneButton.setColour(juce::TextButton::buttonColourId, motion::style::accent().withAlpha(.5f));
        doneButton.onClick = [this] { finish(); };
        for (auto* button : {&cancelButton, &doneButton}) { addAndMakeVisible(button); }

        families = juce::Font::findAllTypefaceNames();
        families.sort(true);
        family.setName("Text font family");
        family.setTooltip("Font");
        family.addItem("Default font", 1);
        for (int i = 0; i < families.size(); ++i) { family.addItem(families[i], i + 2); }
        if (settings.family.isNotEmpty() && !families.contains(settings.family)) {
            families.add(settings.family);
            family.addItem(settings.family + " (not installed)", families.size() + 1);
        }
        family.setSelectedId(settings.family.isEmpty() ? 1 : families.indexOf(settings.family) + 2, juce::dontSendNotification);
        family.setColour(juce::ComboBox::backgroundColourId, motion::style::field());
        family.setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        family.onChange = [this] {
            const auto index = family.getSelectedId() - 2;
            settings.family = index >= 0 && index < families.size() ? families[index] : juce::String();
            changed();
        };
        bold.setTooltip("Bold");
        italic.setTooltip("Italic");
        for (auto [button, flag] : {std::pair {&bold, motion::TextSettings::bold}, std::pair {&italic, motion::TextSettings::italic}}) {
            button->setClickingTogglesState(true);
            button->onClick = [this, button, flag] {
                settings.style = button->getToggleState() ? (settings.style | flag) : (settings.style & ~flag);
                changed();
            };
        }
        for (auto [button, value, tip] : {std::tuple {&alignLeft, 0, "Align left"}, std::tuple {&alignCentre, 1, "Centre"}, std::tuple {&alignRight, 2, "Align right"}}) {
            button->setTooltip(tip);
            button->setRadioGroupId(71);
            button->setClickingTogglesState(true);
            button->onClick = [this, value] { settings.alignment = value; changed(); };
        }
        for (auto [field, spec, tip] : {std::tuple {&lineSpacing, &lineSpacingSpec, "Line spacing (em)"}, std::tuple {&tracking, &trackingSpec, "Tracking: extra space between letters (em)"}}) {
            field->setSpec(*spec);
            field->setTooltip(tip);
            field->onChange = [this](double) { readFields(); };
            field->onCommit = [this](double) { readFields(); };
            addAndMakeVisible(*field);
        }
        lineSpacing.setName("Text line spacing");
        tracking.setName("Text tracking");
        for (auto* caption : {&lineCaption, &trackingCaption}) {
            caption->setFont(motion::style::caption());
            caption->setColour(juce::Label::textColourId, motion::style::muted());
            caption->setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(*caption);
        }
        for (auto* button : std::initializer_list<juce::Component*> {&family, &bold, &italic, &alignLeft, &alignCentre, &alignRight}) { addAndMakeVisible(button); }

        text.setName("Source text");
        text.setMultiLine(true);
        text.setReturnKeyStartsNewLine(true);
        text.setScrollbarsShown(true);
        text.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        text.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        text.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
        text.setColour(juce::TextEditor::textColourId, motion::style::text());
        text.setText(initial, false);
        text.onTextChange = [this] { changed(); };
        addAndMakeVisible(text);
        status.setName("Text status");
        status.setFont(motion::style::caption());
        status.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(status);
        error = preparationError.contains("geometry budget") ? "Too detailed to prepare: try a simpler font or shorter text." : preparationError;
        refreshControls();
        changed(false);
    }

    std::function<void(const juce::String&, const motion::TextSettings&)> onDone;
    std::function<void()> onCancel, onChanged;
    juce::String currentText() const { return text.getText(); }
    const motion::TextSettings& currentSettings() const { return settings; }
    void focusText() { text.grabKeyboardFocus(); text.moveCaretToEnd(); }

    static constexpr int headerHeight = 30;

    void resized() override {
        auto header = getLocalBounds().removeFromTop(headerHeight).reduced(5, 3);
        doneButton.setBounds(header.removeFromRight(64));
        header.removeFromRight(motion::style::gap);
        cancelButton.setBounds(header.removeFromRight(64));
        header.removeFromRight(motion::style::padding);
        name.setBounds(header);
        const auto page = getLocalBounds().withTrimmedTop(headerHeight + 1);
        // The type bar floats 8 px in, like the Scene's tool strip: the font,
        // its style, alignment, then spacing. A narrow Scene folds it onto
        // two lines.
        constexpr int cell = motion::icons::ToolStrip::cell, inset = motion::icons::ToolStrip::inset, groupGap = motion::icons::ToolStrip::groupGap;
        constexpr int fieldsWidth = 34 + 58 + 8 + 56 + 58, styleWidth = 2 * cell, alignWidth = 3 * cell;
        constexpr int oneLine = 2 * inset + 150 + groupGap + styleWidth + groupGap + alignWidth + groupGap + fieldsWidth;
        const auto room = page.getWidth() - 16;
        twoLines = room < oneLine;
        separators.clear();
        const auto separate = [&](juce::Rectangle<int>& line) {
            separators.push_back(juce::Rectangle<float>(static_cast<float>(line.getX() + groupGap / 2), static_cast<float>(line.getY() + 4), 1.0f, static_cast<float>(cell - 8)));
            line.removeFromLeft(groupGap);
        };
        const auto spacing = [&](juce::Rectangle<int>& line) {
            lineCaption.setBounds(line.removeFromLeft(34).withTrimmedRight(4));
            lineSpacing.setBounds(line.removeFromLeft(58).reduced(0, 3));
            line.removeFromLeft(8);
            trackingCaption.setBounds(line.removeFromLeft(56).withTrimmedRight(4));
            tracking.setBounds(line.removeFromLeft(58).reduced(0, 3));
        };
        const auto width = twoLines ? std::min(room, 2 * inset + alignWidth + groupGap + fieldsWidth) : std::min(room, oneLine + 90);
        bar = {page.getX() + 8, page.getY() + 8, width, (twoLines ? 2 : 1) * cell + 2 * inset};
        auto first = bar.reduced(inset).removeFromTop(cell);
        family.setBounds(first.removeFromLeft(first.getWidth() - groupGap - styleWidth - (twoLines ? 0 : groupGap + alignWidth + groupGap + fieldsWidth)).reduced(2, 2));
        separate(first);
        bold.setBounds(first.removeFromLeft(cell));
        italic.setBounds(first.removeFromLeft(cell));
        auto second = twoLines ? bar.reduced(inset).removeFromBottom(cell) : first;
        if (!twoLines) { separate(second); }
        for (auto* button : {&alignLeft, &alignCentre, &alignRight}) { button->setBounds(second.removeFromLeft(cell)); }
        separate(second);
        spacing(second);
        auto writing = page.withTrimmedTop(bar.getBottom() - page.getY() + 8).reduced(16, 0);
        status.setBounds(writing.removeFromBottom(24).withTrimmedRight(-8));
        text.setBounds(writing.withTrimmedTop(8));
    }

    void paint(juce::Graphics& g) override {
        osci::PanelHeader::paintBackground(g, getLocalBounds().removeFromTop(headerHeight).toFloat(), motion::style::background());
        g.setColour(motion::style::panel());
        g.fillRect(getLocalBounds().withTrimmedTop(headerHeight).removeFromTop(1));
        g.setColour(motion::style::background());
        g.fillRect(getLocalBounds().withTrimmedTop(headerHeight + 1));
        // A floating panel like the popovers, so its fields read as fields.
        const auto bounds = bar.toFloat();
        g.setColour(motion::style::panel());
        g.fillRoundedRectangle(bounds, motion::style::panelRadius + 1);
        g.setColour(juce::Colours::white.withAlpha(.08f));
        g.drawRoundedRectangle(bounds.reduced(.5f), motion::style::panelRadius + 1, 1.0f);
        g.setColour(juce::Colours::white.withAlpha(.1f));
        for (const auto& separator : separators) { g.fillRect(separator); }
    }

    bool keyPressed(const juce::KeyPress& key) override {
        const auto command = key.getModifiers().isCommandDown();
        if (command && key.getKeyCode() == juce::KeyPress::returnKey) { finish(); return true; }
        // Escape leaves an untouched text; edits need Cancel, so typing is never lost.
        if (key == juce::KeyPress::escapeKey) {
            if (text.getText() == original && settings == originalSettings && onCancel) { onCancel(); }
            return true;
        }
        // Other shortcuts would act on the timeline behind the editor.
        return command && key.getKeyCode() != 'S' && key.getKeyCode() != 'Z' && key.getKeyCode() != 'C' && key.getKeyCode() != 'V' && key.getKeyCode() != 'X' && key.getKeyCode() != 'A';
    }

private:
    void readFields() {
        settings.lineSpacing = std::round(std::clamp(lineSpacing.getValue(), lineSpacingSpec.minimum, lineSpacingSpec.maximum) * 20.0) / 20.0;
        settings.tracking = std::round(std::clamp(tracking.getValue(), trackingSpec.minimum, trackingSpec.maximum) * 100.0) / 100.0;
        changed();
    }
    void refreshControls() {
        bold.setToggleState((settings.style & motion::TextSettings::bold) != 0, juce::dontSendNotification);
        italic.setToggleState((settings.style & motion::TextSettings::italic) != 0, juce::dontSendNotification);
        alignLeft.setToggleState(settings.alignment == 0, juce::dontSendNotification);
        alignCentre.setToggleState(settings.alignment == 1, juce::dontSendNotification);
        alignRight.setToggleState(settings.alignment == 2, juce::dontSendNotification);
        lineSpacing.setValue(settings.lineSpacing);
        tracking.setValue(std::abs(settings.tracking) < 1.0e-9 ? 0.0 : settings.tracking);
    }
    // The page shows the words in their font, aligned as they will be drawn.
    void changed(bool notify = true) {
        const auto horizontal = settings.alignment == 1 ? juce::Justification::horizontallyCentred : settings.alignment == 2 ? juce::Justification::right : juce::Justification::left;
        text.setJustification(juce::Justification::top | horizontal);
        text.setLineSpacing(static_cast<float>(std::max(1.0, settings.lineSpacing)));
        text.applyFontToAllText(settings.font(28));
        const auto content = text.getText();
        const auto tooLong = content.length() > 16384;
        doneButton.setEnabled(content.trim().isNotEmpty() && !tooLong && (content != original || settings != originalSettings));
        const auto message = tooLong ? juce::String("Maximum 16,384 characters") : error;
        status.setColour(juce::Label::textColourId, message.isNotEmpty() ? motion::style::danger() : motion::style::muted());
        status.setText(message.isNotEmpty() ? message : juce::String(content.length()) + (content.length() == 1 ? " character" : " characters"), juce::dontSendNotification);
        if (notify) {
            error.clear();
            if (onChanged) { onChanged(); }
        }
    }
    void finish() {
        if (!doneButton.isEnabled()) {
            if (text.getText() == original && settings == originalSettings && onCancel) { onCancel(); }
            return;
        }
        if (onDone) { onDone(text.getText(), settings); }
    }

    static constexpr motion::PropertySpec lineSpacingSpec {"text.lineSpacing", "Line spacing", "Text", "", 0.5, 4.0, 1.2, .05, 2, ""};
    static constexpr motion::PropertySpec trackingSpec {"text.tracking", "Tracking", "Text", "", -0.2, 1.0, 0.0, .01, 2, ""};

    motion::TextSettings settings;
    const juce::String original;
    const motion::TextSettings originalSettings;
    juce::String error;
    juce::StringArray families;
    juce::Rectangle<int> bar;
    std::vector<juce::Rectangle<float>> separators;
    bool twoLines = false;
    juce::Label name, status, lineCaption {{}, "Line"}, trackingCaption {{}, "Tracking"};
    juce::TextButton cancelButton, doneButton;
    juce::ComboBox family;
    using Tool = motion::icons::Button;
    Tool bold {"Bold", motion::icons::Icon::bold}, italic {"Italic", motion::icons::Icon::italic};
    Tool alignLeft {"Align left", motion::icons::Icon::alignLeft}, alignCentre {"Align centre", motion::icons::Icon::alignCentre}, alignRight {"Align right", motion::icons::Icon::alignRight};
    MotionScrubField lineSpacing, tracking;
    juce::TextEditor text;
};
