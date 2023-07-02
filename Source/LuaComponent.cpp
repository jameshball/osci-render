#include "LuaComponent.h"
#include "PluginEditor.h"

LuaComponent::LuaComponent(OscirenderAudioProcessor& p, OscirenderAudioProcessorEditor& editor) : audioProcessor(p), pluginEditor(editor) {
	setText(".lua File Settings");
}

LuaComponent::~LuaComponent() {
}

void LuaComponent::resized() {
}
