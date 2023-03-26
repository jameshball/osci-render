#pragma once
#include "DraggableListBox.h"
#include <JuceHeader.h>

// Application-specific data container
struct MyListBoxItemData : public DraggableListBoxItemData
{
    std::vector<juce::String> data;

    int getNumItems() override
    {
        return data.size();
    }

    void deleteItem(int indexOfItemToDelete) override
    {
		data.erase(data.begin() + indexOfItemToDelete);
    }

    void addItemAtEnd() override
    {
        data.push_back(juce::String("Yahoo"));
    }

    void paintContents(int rowNum, juce::Graphics& g, juce::Rectangle<int> bounds) override
    {
        g.fillAll(juce::Colours::lightgrey);
        g.setColour(juce::Colours::black);
        g.drawRect(bounds);
        if (rowNum < data.size()) {
            g.drawText(data[rowNum], bounds, juce::Justification::centred);
        }
    }

	void moveBefore(int indexOfItemToMove, int indexOfItemToPlaceBefore) override {
		juce::String temp = data[indexOfItemToMove];

        if (indexOfItemToMove < indexOfItemToPlaceBefore) {
            move(data, indexOfItemToMove, indexOfItemToPlaceBefore - 1);
        } else {
            move(data, indexOfItemToMove, indexOfItemToPlaceBefore);
        }
	}

    void moveAfter(int indexOfItemToMove, int indexOfItemToPlaceAfter) override {
        juce::String temp = data[indexOfItemToMove];
        
        if (indexOfItemToMove <= indexOfItemToPlaceAfter) {
            move(data, indexOfItemToMove, indexOfItemToPlaceAfter);
        } else {
            move(data, indexOfItemToMove, indexOfItemToPlaceAfter + 1);
        }
    }

    template <typename t> void move(std::vector<t>& v, size_t oldIndex, size_t newIndex)
    {
        if (oldIndex > newIndex) {
            std::rotate(v.rend() - oldIndex - 1, v.rend() - oldIndex, v.rend() - newIndex);
        } else {
            std::rotate(v.begin() + oldIndex, v.begin() + oldIndex + 1, v.begin() + newIndex + 1);
        }
    }

    // This is an example of an operation on a single list item.
    void doItemAction(int itemIndex)
    {
        DBG(data[itemIndex]);
    }
};

// Custom list-item Component (which includes item-delete button)
class MyListComponent : public DraggableListBoxItem
{
public:
    MyListComponent(DraggableListBox& lb, MyListBoxItemData& data, int rn);
    ~MyListComponent();

    void paint(juce::Graphics&) override;
    void resized() override;

protected:
    juce::Rectangle<int> dataArea;
    juce::TextButton actionBtn, deleteBtn;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MyListComponent)
};

// Customized DraggableListBoxModel overrides refreshComponentForRow() to ensure that every
// list-item Component is a MyListComponent.
class MyListBoxModel : public DraggableListBoxModel
{
public:
    MyListBoxModel(DraggableListBox& lb, DraggableListBoxItemData& md)
        : DraggableListBoxModel(lb, md) {}

    juce::Component* refreshComponentForRow(int, bool, juce::Component*) override;
};
