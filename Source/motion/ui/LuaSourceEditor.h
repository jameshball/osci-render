#pragma once

#include "MotionStyle.h"
#include "BakeSettingsPanel.h"
#include <osci_scripting/osci_scripting.h>

// Writes a Lua source inside the Scene, like the text and drawing editors:
// the code fills the page and its bake settings sit in a card beside it.
// Baking (the header's button or Cmd+Return) prepares it and closes.
class MotionLuaSourceEditor final : public juce::Component {
public:
    MotionLuaSourceEditor(const juce::String& code, const juce::String& title, motion::BakeSettings settings, bool editing, const juce::String& preparationError)
        : model("motion-source-draft", "Lua source", code), editor(model, editorOptions()), baking(settings), original(code), originalSettings(settings) {
        setName("Lua source editor");
        setWantsKeyboardFocus(true);
        name.setText(title, juce::dontSendNotification);
        name.setFont(motion::style::title());
        name.setBorderSize({0, 6, 0, 0});
        addAndMakeVisible(name);
        cancelButton.setButtonText("Cancel");
        cancelButton.setColour(juce::TextButton::buttonColourId, motion::style::raised());
        cancelButton.onClick = [this] { if (onCancel) { onCancel(); } };
        addAndMakeVisible(cancelButton);
        auto& bake = baking.bakeButton();
        bake.setButtonText(editing ? "Bake" : "Add");
        bake.setTitle("Bake source");
        bake.setTooltip("Prepare the source (Cmd+Return)");
        bake.setColour(juce::TextButton::buttonColourId, motion::style::accent().withAlpha(.5f));
        addAndMakeVisible(bake);
        baking.setEmbedded(true);
        motion::style::styleFields(baking);
        baking.onBake = [this](motion::BakeSettings values) { if (onDone) { onDone(values, model.getCode()); } };
        addAndMakeVisible(baking);
        // The code sits on Motion's dark field, without the shared editor's
        // button row (its frame clips it away).
        auto& codeView = editor.getEditor();
        codeView.setFont(motion::style::mono());
        codeView.setColour(juce::CodeEditorComponent::backgroundColourId, motion::style::field());
        codeView.setColour(juce::CodeEditorComponent::lineNumberBackgroundId, motion::style::field());
        codeView.setColour(juce::CodeEditorComponent::lineNumberTextId, motion::style::muted().withAlpha(.6f));
        codeView.setColour(juce::CodeEditorComponent::highlightColourId, motion::style::accent().withAlpha(.3f));
        codeFrame.addAndMakeVisible(editor);
        addAndMakeVisible(codeFrame);
        failure.setName("Source preparation error");
        failure.setFont(motion::style::caption());
        failure.setColour(juce::Label::textColourId, motion::style::danger());
        failure.setJustificationType(juce::Justification::topLeft);
        failure.setText(preparationError, juce::dontSendNotification);
        failure.setTooltip(preparationError);
        addAndMakeVisible(failure);
        // A runtime error names its line ("...:12: attempt to..."); mark it in
        // the code as a syntax error would be.
        const auto line = errorLine(preparationError);
        if (line > 0) { static_cast<ErrorListener&>(model).onError(line, preparationError.fromFirstOccurrenceOf(juce::String(line) + ":", false, false).trim()); }
    }

    std::function<void(motion::BakeSettings, const juce::String&)> onDone;
    std::function<void()> onCancel;
    void focusCode() { editor.getEditor().grabKeyboardFocus(); }

    static constexpr int headerHeight = 30;

    void resized() override {
        auto header = getLocalBounds().removeFromTop(headerHeight).reduced(5, 3);
        baking.bakeButton().setBounds(header.removeFromRight(64));
        header.removeFromRight(motion::style::gap);
        cancelButton.setBounds(header.removeFromRight(64));
        header.removeFromRight(motion::style::padding);
        name.setBounds(header);
        auto page = getLocalBounds().withTrimmedTop(headerHeight + 1).reduced(8);
        // The code takes the full width; the bake settings are a card below.
        card = page.removeFromBottom(MotionBakeSettingsPanel::embeddedHeight + 2 * 10);
        baking.setBounds(card.reduced(12, 10));
        page.removeFromBottom(8);
        const auto failed = failure.getText().isNotEmpty();
        failure.setBounds(failed ? page.removeFromBottom(40).withTrimmedTop(6) : juce::Rectangle<int>());
        codeFrame.setBounds(page);
        constexpr int buttonRow = 24;
        editor.setBounds(0, -buttonRow, page.getWidth(), page.getHeight() + buttonRow);
    }

    void paint(juce::Graphics& g) override {
        osci::PanelHeader::paintBackground(g, getLocalBounds().removeFromTop(headerHeight).toFloat(), motion::style::background());
        g.setColour(motion::style::panel());
        g.fillRect(getLocalBounds().withTrimmedTop(headerHeight).removeFromTop(1));
        g.setColour(motion::style::background());
        g.fillRect(getLocalBounds().withTrimmedTop(headerHeight + 1));
        const auto bounds = card.toFloat();
        g.setColour(motion::style::panel());
        g.fillRoundedRectangle(bounds, motion::style::panelRadius + 1);
        g.setColour(juce::Colours::white.withAlpha(.08f));
        g.drawRoundedRectangle(bounds.reduced(.5f), motion::style::panelRadius + 1, 1.0f);
    }

    bool keyPressed(const juce::KeyPress& key) override {
        const auto command = key.getModifiers().isCommandDown();
        if (command && key.getKeyCode() == juce::KeyPress::returnKey) {
            baking.bakeButton().triggerClick();
            return true;
        }
        // Escape leaves untouched code; edits need Cancel, so work is never lost.
        if (key == juce::KeyPress::escapeKey) {
            const auto& now = baking.currentSettings();
            const auto sameSettings = now.duration == originalSettings.duration && now.frameRate == originalSettings.frameRate && now.bpm == originalSettings.bpm
                && now.pointsPerFrame == originalSettings.pointsPerFrame && now.seed == originalSettings.seed;
            if (model.getCode() == original && sameSettings && onCancel) { onCancel(); }
            return true;
        }
        // Other shortcuts would act on the timeline behind the editor.
        return command && key.getKeyCode() != 'S' && key.getKeyCode() != 'Z' && key.getKeyCode() != 'C' && key.getKeyCode() != 'V' && key.getKeyCode() != 'X' && key.getKeyCode() != 'A';
    }

private:
    static int errorLine(const juce::String& message) {
        const auto at = message.indexOf("]:");
        if (at < 0) { return 0; }
        return message.substring(at + 2).getIntValue();
    }
    static osci::LuaScriptEditorComponent::Options editorOptions() {
        osci::LuaScriptEditorComponent::Options options;
        options.showTitle = false;
        options.showConsole = false;
        return options;
    }
    osci::LuaScriptEditorModel model;
    osci::LuaScriptEditorComponent editor;
    MotionBakeSettingsPanel baking;
    const juce::String original;
    const motion::BakeSettings originalSettings;
    juce::Rectangle<int> card;
    juce::Component codeFrame;
    juce::Label name, failure;
    juce::TextButton cancelButton;
};
