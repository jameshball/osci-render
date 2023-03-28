#include "MyListComponent.h"

MyListComponent::MyListComponent(DraggableListBox& lb, MyListBoxItemData& data, int rn) : DraggableListBoxItem(lb, data, rn) {
    addAndMakeVisible(slider);
    addAndMakeVisible(label);

    label.setText(data.getText(rn), juce::dontSendNotification);
    label.attachToComponent(&slider, true);

    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 90, slider.getTextBoxHeight());
	slider.setRange(0.0, 1.0, 0.01);
    slider.setValue(data.getValue(rn), juce::dontSendNotification);
	slider.onValueChange = [this] {
        ((MyListBoxItemData&)modelData).setValue(rowNum, slider.getValue());
	};
}

MyListComponent::~MyListComponent() {}

void MyListComponent::paint (juce::Graphics& g) {
    modelData.paintContents(rowNum, g, dataArea);
    DraggableListBoxItem::paint(g);
}

void MyListComponent::resized() {
    auto sliderLeft = 100;
    slider.setBounds(sliderLeft, 0, getWidth() - 110, getHeight());
}


juce::Component* MyListBoxModel::refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component *existingComponentToUpdate) {
    std::unique_ptr<MyListComponent> item(dynamic_cast<MyListComponent*>(existingComponentToUpdate));
    if (juce::isPositiveAndBelow(rowNumber, modelData.getNumItems()))
    {
        item = std::make_unique<MyListComponent>(listBox, (MyListBoxItemData&)modelData, rowNumber);
    }
    return item.release();
}
