#include "MyListComponent.h"

MyListComponent::MyListComponent(DraggableListBox& lb, MyListBoxItemData& data, int rn)
    : DraggableListBoxItem(lb, data, rn)
{
    actionBtn.setButtonText("Action");
    actionBtn.onClick = [this]()
    {
        ((MyListBoxItemData&)modelData).doItemAction(rowNum);
    };
    addAndMakeVisible(actionBtn);

    deleteBtn.setButtonText("Delete");
    deleteBtn.onClick = [this]()
    {
        modelData.deleteItem(rowNum);
        listBox.updateContent();
    };
    addAndMakeVisible(deleteBtn);
}

MyListComponent::~MyListComponent()
{
    
}

void MyListComponent::paint (juce::Graphics& g)
{
    modelData.paintContents(rowNum, g, dataArea);
    DraggableListBoxItem::paint(g);
}

void MyListComponent::resized()
{
    dataArea = getLocalBounds();
    actionBtn.setBounds(dataArea.removeFromLeft(70).withSizeKeepingCentre(56, 24));
    deleteBtn.setBounds(dataArea.removeFromRight(70).withSizeKeepingCentre(56, 24));
}


juce::Component* MyListBoxModel::refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component *existingComponentToUpdate)
{
    std::unique_ptr<MyListComponent> item(dynamic_cast<MyListComponent*>(existingComponentToUpdate));
    if (juce::isPositiveAndBelow(rowNumber, modelData.getNumItems()))
    {
        item = std::make_unique<MyListComponent>(listBox, (MyListBoxItemData&)modelData, rowNumber);
    }
    return item.release();
}
