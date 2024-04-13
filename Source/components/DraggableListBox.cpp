#include "DraggableListBox.h"

DraggableListBoxItemData::~DraggableListBoxItemData() {};

void DraggableListBoxItem::paint(juce::Graphics& g)
{
    if (insertAfter)
    {
        g.setColour(juce::Colour(0xff00ff00));
        g.fillRect(0, getHeight() - 4, getWidth(), 4);
    }
    else if (insertBefore)
    {
        g.setColour(juce::Colour(0xff00ff00));
        g.fillRect(0, 0, getWidth(), 4);
    }
}

void DraggableListBoxItem::mouseEnter(const juce::MouseEvent&)
{
    savedCursor = getMouseCursor();
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}

void DraggableListBoxItem::mouseExit(const juce::MouseEvent&)
{
    setMouseCursor(savedCursor);
}

void DraggableListBoxItem::mouseDrag(const juce::MouseEvent&)
{
    if (juce::DragAndDropContainer* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
    {
        container->startDragging("DraggableListBoxItem", this);
    }
}

void DraggableListBoxItem::updateInsertLines(const SourceDetails &dragSourceDetails)
{
    if (dragSourceDetails.localPosition.y < getHeight() / 2)
    {
        insertBefore = true;
        insertAfter = false;
    }
    else
    {
        insertAfter = true;
        insertBefore = false;
    }
    repaint();
}

void DraggableListBoxItem::hideInsertLines()
{
    insertBefore = false;
    insertAfter = false;
	repaint();
}

void DraggableListBoxItem::itemDragEnter(const SourceDetails& dragSourceDetails)
{
    updateInsertLines(dragSourceDetails);
}

void DraggableListBoxItem::itemDragMove(const SourceDetails& dragSourceDetails)
{
    updateInsertLines(dragSourceDetails);
}

void DraggableListBoxItem::itemDragExit(const SourceDetails& /*dragSourceDetails*/)
{
    hideInsertLines();
}

void DraggableListBoxItem::itemDropped(const juce::DragAndDropTarget::SourceDetails &dragSourceDetails)
{
    hideInsertLines();
    if (DraggableListBoxItem* item = dynamic_cast<DraggableListBoxItem*>(dragSourceDetails.sourceComponent.get()))
    {
        if (dragSourceDetails.localPosition.y < getHeight() / 2)
            modelData.moveBefore(item->rowNum, rowNum);
        else
            modelData.moveAfter(item->rowNum, rowNum);
        listBox.updateContent();
    }
}

juce::Component* DraggableListBoxModel::refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component *existingComponentToUpdate)
{
    std::unique_ptr<DraggableListBoxItem> item(dynamic_cast<DraggableListBoxItem*>(existingComponentToUpdate));
    if (juce::isPositiveAndBelow(rowNumber, modelData.getNumItems()))
    {
        item = std::make_unique<DraggableListBoxItem>(listBox, modelData, rowNumber);
    }
    return item.release();
}
