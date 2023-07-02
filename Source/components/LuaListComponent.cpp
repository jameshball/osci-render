#include "LuaListComponent.h"

LuaListComponent::LuaListComponent(int sliderNum) {
	juce::String sliderName = "";

	sliderNum++;
	while (sliderNum > 0) {
		int mod = (sliderNum - 1) % 26;
		sliderName = (char)(mod + 'A') + sliderName;
		sliderNum = (sliderNum - mod) / 26;
	}
	
	effectComponent = std::make_shared<EffectComponent>(0.0, 1.0, 0.01, 0, "Lua " + sliderName, "lua" + sliderName);
	effectComponent->setHideCheckbox(true);
	addAndMakeVisible(*effectComponent);
}

LuaListComponent::~LuaListComponent() {}

void LuaListComponent::resized() {
	effectComponent->setBounds(getLocalBounds());
}

void paintListBoxItem(int sliderNum, juce::Graphics& g, int width, int height, bool rowIsSelected) {}

int LuaListBoxModel::getNumRows() {
	return numSliders + 1;
}

void LuaListBoxModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {}

juce::Component* LuaListBoxModel::refreshComponentForRow(int sliderNum, bool isRowSelected, juce::Component *existingComponentToUpdate) {
	if (sliderNum < getNumRows() - 1) {
		std::unique_ptr<LuaListComponent> item(dynamic_cast<LuaListComponent*>(existingComponentToUpdate));
		if (juce::isPositiveAndBelow(sliderNum, getNumRows())) {
			item = std::make_unique<LuaListComponent>(sliderNum);
		}
		return item.release();
	} else {
		std::unique_ptr<juce::TextButton> item(dynamic_cast<juce::TextButton*>(existingComponentToUpdate));
		item = std::make_unique<juce::TextButton>("Add");
		item->onClick = [this]() {
			numSliders++;
			listBox.updateContent();
		};
		return item.release();
	}
}
