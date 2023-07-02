#include "MyListComponent.h"

MyListComponent::MyListComponent(DraggableListBox& lb, MyListBoxItemData& data, int rn, std::shared_ptr<EffectComponent> effectComponent) : DraggableListBoxItem(lb, data, rn), effectComponent(effectComponent) {
	addAndMakeVisible(*effectComponent);

	effectComponent->slider.setValue(data.getValue(rn), juce::dontSendNotification);
	effectComponent->slider.onValueChange = [this] {
        ((MyListBoxItemData&)modelData).setValue(rowNum, this->effectComponent->slider.getValue());
	};

    // check if effect is in audioProcessor enabled effects
	bool isSelected = false;
	for (auto effect : *data.audioProcessor.enabledEffects) {
		if (effect->getId() == data.getId(rn)) {
			isSelected = true;
			break;
		}
	}
	effectComponent->selected.setToggleState(isSelected, juce::dontSendNotification);
	effectComponent->selected.onClick = [this] {
		((MyListBoxItemData&)modelData).setSelected(rowNum, this->effectComponent->selected.getToggleState());
	};
}

MyListComponent::~MyListComponent() {}

void MyListComponent::paint(juce::Graphics& g) {
	DraggableListBoxItem::paint(g);
	auto bounds = getLocalBounds();
	bounds.removeFromLeft(20);
	// draw drag and drop handle using circles
	g.setColour(juce::Colours::white);
	double size = 4;
	double leftPad = 4;
	double spacing = 7;
	double topPad = 7;
	g.fillEllipse(leftPad, topPad, size, size);
	g.fillEllipse(leftPad, topPad + spacing, size, size);
	g.fillEllipse(leftPad, topPad + 2 * spacing, size, size);
	g.fillEllipse(leftPad + spacing, topPad, size, size);
	g.fillEllipse(leftPad + spacing, topPad + spacing, size, size);
	g.fillEllipse(leftPad + spacing, topPad + 2 * spacing, size, size);
}

void MyListComponent::resized() {
	auto area = getLocalBounds();
	area.removeFromLeft(20);
	effectComponent->setBounds(area);
}

juce::Component* MyListBoxModel::refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component *existingComponentToUpdate) {
    std::unique_ptr<MyListComponent> item(dynamic_cast<MyListComponent*>(existingComponentToUpdate));
    if (juce::isPositiveAndBelow(rowNumber, modelData.getNumItems())) {
		auto data = (MyListBoxItemData&)modelData;
		std::shared_ptr<EffectComponent> effectComponent = std::make_shared<EffectComponent>(0, 1, 0.01, 0, data.getText(rowNumber), data.getId(rowNumber));
        item = std::make_unique<MyListComponent>(listBox, (MyListBoxItemData&)modelData, rowNumber, effectComponent);
    }
    return item.release();
}
