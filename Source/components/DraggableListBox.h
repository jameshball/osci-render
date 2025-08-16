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

// DraggableListBox extends VListBox to both initiate drags and act as a target, so
// it can paint a clean, consistent drop indicator between row components.
class DraggableListBox : public VListBox,
                         public juce::DragAndDropContainer,
                         public juce::DragAndDropTarget
{
public:
    using VListBox::VListBox;

    // DragAndDropTarget
    bool isInterestedInDragSource(const SourceDetails&) override { return true; }
    void itemDragEnter(const SourceDetails& details) override { updateDropIndicator(details); }
    void itemDragMove(const SourceDetails& details) override { updateDropIndicator(details); }
    void itemDragExit(const SourceDetails&) override { clearDropIndicator(); }
    void itemDropped(const SourceDetails& details) override;
    bool shouldDrawDragImageWhenOver() override { return true; }

    // Paint a global drop indicator between rows
    void paintOverChildren(juce::Graphics& g) override;

    // Allow children to drive indicator positioning
    void updateDropIndicatorAt(const juce::Point<int>& listLocalPos);
    void clearDropIndicator();

private:
    void updateDropIndicator(const SourceDetails& details);

    bool showDropIndicator = false;
    int dropInsertIndex = -1; // index to insert before; may be getNumRows() for end
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

    // Convenience: move an item using an insertion index (before position). Handles index shifting.
    void moveByInsertIndex(int fromIndex, int insertIndex)
    {
        const int count = modelData.getNumItems();
        if (count <= 0) return;
        insertIndex = juce::jlimit(0, count, insertIndex);
        int toIndex = insertIndex;
        if (toIndex > fromIndex) toIndex -= 1;

        if (count == 1 || fromIndex == toIndex) return;

        if (toIndex <= 0)
            modelData.moveBefore(fromIndex, 0);
        else if (toIndex >= count)
            modelData.moveAfter(fromIndex, count - 1);
        else
            modelData.moveBefore(fromIndex, toIndex);

        listBox.updateContent();
    }

protected:
    // Draggable model has a reference to its owner ListBox, so it can tell it to update after DnD.
    DraggableListBox &listBox;

    // It also has a reference to the model data, which it uses to get the current items count,
    // and which it passes to the DraggableListBoxItem objects it creates/updates.
    DraggableListBoxItemData& modelData;
};
