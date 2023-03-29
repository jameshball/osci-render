#include "MyListComponent.h"

MyListComponent::MyListComponent(DraggableListBox& lb, MyListBoxItemData& data, int rn) : DraggableListBoxItem(lb, data, rn) {
    addAndMakeVisible(slider);
    addAndMakeVisible(selected);

    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, slider.getTextBoxHeight());
	slider.setRange(0.0, 1.0, 0.01);
    slider.setValue(data.getValue(rn), juce::dontSendNotification);
	slider.onValueChange = [this] {
        ((MyListBoxItemData&)modelData).setValue(rowNum, slider.getValue());
	};

    // check if effect is in audioProcessor enabled effects
	bool isSelected = false;
	for (auto effect : *data.audioProcessor.enabledEffects) {
		if (effect->getId() == data.getId(rn)) {
			isSelected = true;
			break;
		}
	}
	selected.setToggleState(isSelected, juce::dontSendNotification);
	selected.onClick = [this] {
		((MyListBoxItemData&)modelData).setSelected(rowNum, selected.getToggleState());
	};
}

MyListComponent::~MyListComponent() {}

void MyListComponent::paint (juce::Graphics& g) {
    modelData.paintContents(rowNum, g, dataArea);
    DraggableListBoxItem::paint(g);
}

void MyListComponent::resized() {
    auto sliderLeft = 150;
    slider.setBounds(sliderLeft, 0, getWidth() - sliderLeft - 10, getHeight());
	selected.setBounds(2, 0, 25, getHeight());
}


juce::Component* MyListBoxModel::refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component *existingComponentToUpdate) {
    std::unique_ptr<MyListComponent> item(dynamic_cast<MyListComponent*>(existingComponentToUpdate));
    if (juce::isPositiveAndBelow(rowNumber, modelData.getNumItems()))
    {
        item = std::make_unique<MyListComponent>(listBox, (MyListBoxItemData&)modelData, rowNumber);
    }
    return item.release();
}
