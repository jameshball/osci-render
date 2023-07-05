#include "LuaListComponent.h"

LuaListComponent::LuaListComponent(OscirenderAudioProcessor& p, Effect& effect) {
	effectComponent = std::make_shared<EffectComponent>(0.0, 1.0, 0.01, effect);
	effectComponent->setCheckboxVisible(false);

	effectComponent->slider.onValueChange = [this, &effect, &p] {
		effect.setValue(effectComponent->slider.getValue());
		effect.apply(0, Vector2());
	};

	addAndMakeVisible(*effectComponent);
}

LuaListComponent::~LuaListComponent() {}

void LuaListComponent::resized() {
	effectComponent->setBounds(getLocalBounds());
}

void paintListBoxItem(int sliderNum, juce::Graphics& g, int width, int height, bool rowIsSelected) {}

int LuaListBoxModel::getNumRows() {
	return audioProcessor.luaEffects.size() + 1;
}

void LuaListBoxModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {}

juce::Component* LuaListBoxModel::refreshComponentForRow(int rowNum, bool isRowSelected, juce::Component *existingComponentToUpdate) {
	if (rowNum < getNumRows() - 1) {
		juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
		std::unique_ptr<LuaListComponent> item(dynamic_cast<LuaListComponent*>(existingComponentToUpdate));
		if (juce::isPositiveAndBelow(rowNum, getNumRows())) {
			item = std::make_unique<LuaListComponent>(audioProcessor, *audioProcessor.luaEffects[rowNum]);
		}
		return item.release();
	} else {
		juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
		std::unique_ptr<juce::TextButton> item(dynamic_cast<juce::TextButton*>(existingComponentToUpdate));
		item = std::make_unique<juce::TextButton>("+");
		item->onClick = [this]() {
			audioProcessor.addLuaSlider();
			listBox.updateContent();
		};
		return item.release();
	}
}
