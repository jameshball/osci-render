#include "LuaListComponent.h"

LuaListComponent::LuaListComponent(OscirenderAudioProcessor& p, osci::Effect& effect) {
	effectComponent = std::make_shared<EffectComponent>(effect);

	effectComponent->slider.onValueChange = [this, &effect, &p] {
		effect.setValue(effectComponent->slider.getValue());
	};

	addAndMakeVisible(*effectComponent);
}

LuaListComponent::~LuaListComponent() {}

void LuaListComponent::resized() {
	effectComponent->setBounds(getLocalBounds());
}

void paintListBoxItem(int sliderNum, juce::Graphics& g, int width, int height, bool rowIsSelected) {}

int LuaListBoxModel::getNumRows() {
	return audioProcessor.luaEffects.size();
}

void LuaListBoxModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {}

juce::Component* LuaListBoxModel::refreshComponentForRow(int rowNum, bool isRowSelected, juce::Component *existingComponentToUpdate) {
    // TODO: We should REALLY be locking here but it causes a deadlock :( works fine without.....
	// juce::SpinLock::ScopedLockType lock1(audioProcessor.effectsLock);
	std::unique_ptr<LuaListComponent> item(dynamic_cast<LuaListComponent*>(existingComponentToUpdate));
	if (juce::isPositiveAndBelow(rowNum, getNumRows())) {
		item = std::make_unique<LuaListComponent>(audioProcessor, *audioProcessor.luaEffects[rowNum]);
	}
	return item.release();
}
