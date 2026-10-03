#pragma once

#include "MotionStyle.h"

#include "BakeSettingsPanel.h"
#include <osci_scripting/osci_scripting.h>

class MotionLuaSourcePanel final : public juce::Component {
public:
    MotionLuaSourcePanel(const juce::String& initial, motion::BakeSettings settings, std::size_t instances, const juce::String& error)
        : model("motion-source-draft", "Lua source", initial), editor(model, editorOptions()), baking(settings) {
        setName("Lua source editor");
        editor.getEditor().setFont(motion::style::mono());
        help.setText("Shared source: " + juce::String(static_cast<juce::uint64>(instances)) + (instances == 1 ? " clip" : " clips")
            + " will update after a successful bake. Clip animation stays unchanged.", juce::dontSendNotification);
        help.setFont(motion::style::body());
        help.setColour(juce::Label::textColourId, osci::Colours::textMuted());
        help.setJustificationType(juce::Justification::centredLeft);
        failure.setName("Source preparation error");
        failure.setFont(motion::style::body());
        failure.setColour(juce::Label::textColourId, osci::Colours::danger());
        failure.setText(error, juce::dontSendNotification);
        failure.setTooltip(error);
        failure.setJustificationType(juce::Justification::topLeft);
        baking.onBake = [this](motion::BakeSettings values) {
            if (onBake) { onBake(values, model.getCode()); }
        };
        for (auto* component : std::initializer_list<juce::Component*>{&help, &editor, &baking, &failure}) { addAndMakeVisible(component); }
    }
    std::function<void(motion::BakeSettings, juce::String)> onBake;
    void resized() override {
        auto area = getLocalBounds().reduced(8);
        help.setBounds(area.removeFromTop(40));
        failure.setBounds(area.removeFromBottom(failure.getText().isEmpty() ? 0 : 64).reduced(6, 4));
        baking.setBounds(area.removeFromRight(330));
        area.removeFromRight(6);
        editor.setBounds(area);
    }
private:
    static osci::LuaScriptEditorComponent::Options editorOptions() {
        osci::LuaScriptEditorComponent::Options options;
        options.showTitle = false;
        options.showConsole = false;
        return options;
    }
    osci::LuaScriptEditorModel model;
    osci::LuaScriptEditorComponent editor;
    MotionBakeSettingsPanel baking;
    juce::Label help, failure;
};
