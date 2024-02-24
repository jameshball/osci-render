#include "LuaComponent.h"
#include "PluginEditor.h"

LuaComponent::LuaComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor), slidersModel(sliders, p) {
	setText("Lua Settings");

	sliders.setModel(&slidersModel);
	sliders.setRowHeight(30);
	addAndMakeVisible(sliders);
}

LuaComponent::~LuaComponent() {
}

void LuaComponent::resized() {
	auto area = getLocalBounds().withTrimmedTop(20).reduced(20);
	sliders.setBounds(area);
}
