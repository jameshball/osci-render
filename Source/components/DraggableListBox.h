#pragma once
#include "JuceHeader.h"
#include "VListBox.h"

// Your item-data container must inherit from this, and override at least the first
// four member functions.
struct DraggableListBoxItemData
{
    virtual ~DraggableListBoxItemData() = 0;
    
    virtual int getNumItems() = 0;

    virtual void moveBefore(int indexOfItemToMove, int indexOfItemToPlaceBefore) = 0;
    virtual void moveAfter(int indexOfItemToMove, int indexOfItemToPlaceAfter) = 0;

    // If you need a dynamic list, override these functions as well.
    virtual void deleteItem(int /*indexOfItemToDelete*/) {};
    virtual void addItemAtEnd() {};
};

// DraggableListBox is basically just a VListBox, that inherits from DragAndDropContainer.
// Declare your list box using this type.
class DraggableListBox : public VListBox, public juce::DragAndDropContainer
{
};

// Everything below this point should be generic.
class DraggableListBoxItem : public juce::Component, public juce::DragAndDropTarget, public juce::Timer
{
public:
    DraggableListBoxItem(DraggableListBox& lb, DraggableListBoxItemData& data, int rn)
        : rowNum(rn), modelData(data), listBox(lb)  {}

    // Component
    void paint(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;

    // DragAndDropTarget
    bool isInterestedInDragSource(const SourceDetails&) override { return true; }
    void itemDragEnter(const SourceDetails&) override;
    void itemDragMove(const SourceDetails&) override;
    void itemDragExit(const SourceDetails&) override;
    void itemDropped(const SourceDetails&) override;
    bool shouldDrawDragImageWhenOver() override { return true; }

    // DraggableListBoxItem
protected:
    void updateInsertLines(const SourceDetails &dragSourceDetails);
    void hideInsertLines();
    void updateAutoScroll(const SourceDetails& dragSourceDetails);
    void stopAutoScroll();
    void timerCallback() override;

    int rowNum;
    DraggableListBoxItemData& modelData;
    DraggableListBox& listBox;

    juce::MouseCursor savedCursor;
    bool insertAfter = false;
    bool insertBefore = false;

    // Auto-scroll state while dragging near viewport edges
    double scrollPixelsPerTick = 0.0; // positive = scroll down, negative = up
    bool autoScrollActive = false;
};

class DraggableListBoxModel : public VListBoxModel
{
public:
    DraggableListBoxModel(DraggableListBox& lb, DraggableListBoxItemData& md)
        : listBox(lb), modelData(md) {}

    int getNumRows() override { return modelData.getNumItems(); }
    void paintListBoxItem(int, juce::Graphics &, int, int, bool) override {}

    juce::Component* refreshComponentForRow(int, bool, juce::Component*) override;

protected:
    // Draggable model has a reference to its owner ListBox, so it can tell it to update after DnD.
    DraggableListBox &listBox;

    // It also has a reference to the model data, which it uses to get the current items count,
    // and which it passes to the DraggableListBoxItem objects it creates/updates.
    DraggableListBoxItemData& modelData;
};
