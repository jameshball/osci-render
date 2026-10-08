#pragma once

#include "MotionStyle.h"
#include "BakeSettingsPanel.h"
#include <osci_scripting/osci_scripting.h>

// Writes a Lua source inside the Scene, like the text and drawing editors:
// the code fills the page edge to edge, a problem shows on its line and in a
// strip under the code (a click goes to it), and the bake settings sit at
// the foot. Applying (the header's button or Cmd+Return) bakes and closes.
class MotionLuaSourceEditor final : public juce::Component, private juce::CodeDocument::Listener {
public:
    MotionLuaSourceEditor(const juce::String& code, const juce::String& title, motion::BakeSettings settings, bool editing, const juce::String& preparationError)
        : model("motion-source-draft", "Lua source", code), editor(model, editorOptions()), baking(settings), original(code), originalSettings(settings), sourceName(title) {
        setName("Lua source editor");
        setWantsKeyboardFocus(true);
        cancelButton.setButtonText("Cancel");
        cancelButton.onClick = [this] { if (onCancel) { onCancel(); } };
        addAndMakeVisible(cancelButton);
        auto& bake = baking.bakeButton();
        bake.setButtonText(editing ? "Apply" : "Add");
        bake.setTitle("Bake source");
        bake.setTooltip(editing ? "Bake the source with these changes (Cmd+Return)" : "Bake the source and add it (Cmd+Return)");
        motion::style::makePrimary(bake);
        addAndMakeVisible(bake);
        baking.onBake = [this](motion::BakeSettings values) { if (onDone) { onDone(values, model.getCode()); } };
        addAndMakeVisible(baking);
        // The code sits on the Scene's dark page. The shared editor's frame
        // and button row fall outside the clip, leaving only the code.
        auto& codeView = editor.getEditor();
        codeView.setFont(motion::style::mono());
        codeView.setColour(juce::CodeEditorComponent::backgroundColourId, osci::Colours::veryDark());
        codeView.setColour(juce::CodeEditorComponent::lineNumberBackgroundId, osci::Colours::veryDark().darker(.25f));
        codeView.setColour(juce::CodeEditorComponent::lineNumberTextId, osci::Colours::textMuted().withAlpha(.45f));
        codeView.setColour(juce::CodeEditorComponent::highlightColourId, osci::Colours::accentColor().withAlpha(.3f));
        codeView.setColour(juce::ScrollBar::thumbColourId, juce::Colours::white.withAlpha(.16f));
        codeView.setColour(juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
        codeFrame.addAndMakeVisible(editor);
        addAndMakeVisible(codeFrame);
        problem.setName("Source preparation error");
        problem.setFont(motion::style::body());
        problem.setColour(juce::Label::textColourId, osci::Colours::text());
        problem.setBorderSize({});
        problem.setText(preparationError, juce::dontSendNotification);
        problem.setTooltip(preparationError);
        problem.setMouseCursor(juce::MouseCursor::PointingHandCursor);
        problem.addMouseListener(this, false);
        addChildComponent(problem);
        problem.setVisible(preparationError.isNotEmpty());
        // A runtime error names its line ("Line 12: attempt to..."); mark it
        // in the code as a syntax error would be.
        problemLine = errorLine(preparationError);
        if (problemLine > 0) { static_cast<ErrorListener&>(model).onError(problemLine, preparationError.fromFirstOccurrenceOf(":", false, false).trim()); }
        codeView.getDocument().addListener(this);
    }
    ~MotionLuaSourceEditor() override { editor.getEditor().getDocument().removeListener(this); }

    std::function<void(motion::BakeSettings, const juce::String&)> onDone;
    std::function<void()> onCancel;
    void focusCode() { editor.getEditor().grabKeyboardFocus(); }

    void resized() override {
        titleArea = motion::style::sceneEditor::layoutHeader(getLocalBounds(), baking.bakeButton(), cancelButton);
        auto page = motion::style::sceneEditor::page(getLocalBounds());
        footer = page.removeFromBottom(MotionBakeSettingsPanel::preferredHeight + motion::style::padding);
        baking.setBounds(footer.reduced(motion::style::padding * 2, 0).withTrimmedBottom(motion::style::padding));
        strip = problem.isVisible() ? page.removeFromBottom(problemHeight) : juce::Rectangle<int>();
        problem.setBounds(strip.withTrimmedLeft(24).withTrimmedRight(motion::style::padding));
        codeFrame.setBounds(page);
        // The shared editor draws a frame and a button row around its code;
        // place it so only the code shows, filling the page.
        constexpr int inset = 5, header = 28;
        editor.setBounds(-inset, -header, page.getWidth() + 2 * inset, page.getHeight() + header + inset);
    }

    void paint(juce::Graphics& g) override {
        motion::style::sceneEditor::paint(g, getLocalBounds(), titleArea, sourceName + ".lua");
        // The bake settings: a rule above, on the page's colour.
        g.setColour(osci::Colours::surface());
        g.fillRect(footer);
        g.setColour(juce::Colours::white.withAlpha(.06f));
        g.fillRect(footer.removeFromTop(1));
        if (problem.isVisible()) {
            // Once the code changes the problem is the last bake's, dimmed.
            g.setColour(motion::style::error().withAlpha(stale ? .04f : .1f));
            g.fillRect(strip);
            g.setColour(motion::style::error().withAlpha(stale ? .45f : 1.0f));
            g.fillRect(strip.withHeight(1));
            g.fillEllipse(juce::Rectangle<float>(7, 7).withCentre({static_cast<float>(strip.getX()) + 11.0f, static_cast<float>(strip.getCentreY())}));
        }
    }

    void mouseUp(const juce::MouseEvent& event) override {
        // The problem strip goes to the line it names.
        if (event.eventComponent != &problem || problemLine <= 0) { return; }
        auto& codeView = editor.getEditor();
        const juce::CodeDocument::Position position(codeView.getDocument(), problemLine - 1, 0);
        codeView.moveCaretTo(position, false);
        codeView.scrollToKeepCaretOnScreen();
        codeView.grabKeyboardFocus();
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
    void codeDocumentTextInserted(const juce::String&, int) override { markStale(); }
    void codeDocumentTextDeleted(int, int) override { markStale(); }
    void markStale() {
        if (stale || !problem.isVisible()) { return; }
        stale = true;
        problem.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        problem.setText(problem.getText() + "  (last bake)", juce::dontSendNotification);
        repaint();
    }
    static constexpr int problemHeight = 28;
    // "Line 12: ..." from a bake, or "...]:12: ..." from Lua itself.
    static int errorLine(const juce::String& message) {
        if (message.startsWith("Line ")) { return message.substring(5).getIntValue(); }
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
    const juce::String sourceName;
    juce::Rectangle<int> titleArea, footer, strip;
    juce::Component codeFrame;
    juce::Label problem;
    int problemLine = 0;
    bool stale = false;
    juce::TextButton cancelButton;
};
