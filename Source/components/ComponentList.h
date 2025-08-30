#pragma once
#include <JuceHeader.h>
#include "VListBox.h"

// This class is a wrapper for a component that allows it to be used in a ListBox
// Why is this needed?!?!?!?!
class ComponentWrapper : public juce::Component {
 public:
    ComponentWrapper(std::shared_ptr<juce::Component> component) : component(component) {
        addAndMakeVisible(component.get());
    }

    ~ComponentWrapper() override {}

    void resized() override {
        component->setBounds(getLocalBounds());
    }

private:
    std::shared_ptr<juce::Component> component;
};

class ComponentListModel : public VListBoxModel
{
public:
    ComponentListModel(int rowHeight) : rowHeight(rowHeight) {}
    ~ComponentListModel() override {}

    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    juce::Component* refreshComponentForRow(int sliderNum, bool isRowSelected, juce::Component *existingComponentToUpdate) override;
    int getRowHeight(int rowNumber) override {
        return rowHeight;
    }
    
    void addComponent(std::shared_ptr<juce::Component> component) {
        components.push_back(component);
    }

private:
    std::vector<std::shared_ptr<juce::Component>> components;
    int rowHeight;
};
