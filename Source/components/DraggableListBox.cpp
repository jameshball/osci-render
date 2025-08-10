#include "DraggableListBox.h"
#include <cmath>

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
    updateAutoScroll(dragSourceDetails);
}

void DraggableListBoxItem::itemDragMove(const SourceDetails& dragSourceDetails)
{
    updateInsertLines(dragSourceDetails);
    updateAutoScroll(dragSourceDetails);
}

void DraggableListBoxItem::itemDragExit(const SourceDetails& /*dragSourceDetails*/)
{
    hideInsertLines();
    stopAutoScroll();
}

void DraggableListBoxItem::itemDropped(const juce::DragAndDropTarget::SourceDetails &dragSourceDetails)
{
    hideInsertLines();
    stopAutoScroll();
    if (DraggableListBoxItem* item = dynamic_cast<DraggableListBoxItem*>(dragSourceDetails.sourceComponent.get()))
    {
        if (dragSourceDetails.localPosition.y < getHeight() / 2)
            modelData.moveBefore(item->rowNum, rowNum);
        else
            modelData.moveAfter(item->rowNum, rowNum);
        listBox.updateContent();
    }
}

void DraggableListBoxItem::updateAutoScroll(const SourceDetails& dragSourceDetails)
{
    // Determine pointer position within the list's viewport
    if (auto* vp = listBox.getViewport())
    {
        // Convert the drag position to the viewport's local coordinates
        auto ptInThis = dragSourceDetails.localPosition;
        auto ptInLB = localPointToGlobal(ptInThis);
        ptInLB = listBox.getLocalPoint(nullptr, ptInLB); // convert from screen to listbox

        auto viewBounds = vp->getLocalBounds();
        auto viewTop = viewBounds.getY();
        auto viewBottom = viewBounds.getBottom();

        // Position of pointer within the viewport component
        auto ptInVP = vp->getLocalPoint(&listBox, ptInLB);
        const int yInVP = ptInVP.y;

        const int edgeZone = juce::jmax(12, viewBounds.getHeight() / 6); // scrolling zone near edges
        double velocity = 0.0;

        if (yInVP < viewTop + edgeZone)
        {
            // Near top: scroll up. Speed increases closer to edge.
            const double t = juce::jlimit(0.0, 1.0, (double)(edgeZone - (yInVP - viewTop)) / (double)edgeZone);
            velocity = - (1.0 + 14.0 * t * t); // pixels per tick (quadratic ramp), negative = up
        }
        else if (yInVP > viewBottom - edgeZone)
        {
            // Near bottom: scroll down.
            const double d = (double)(yInVP - (viewBottom - edgeZone));
            const double t = juce::jlimit(0.0, 1.0, d / (double)edgeZone);
            velocity = (1.0 + 14.0 * t * t); // pixels per tick, positive = down
        }

        scrollPixelsPerTick = velocity;

    if (std::abs(scrollPixelsPerTick) > 0.0)
        {
            if (!autoScrollActive)
            {
                autoScrollActive = true;
                startTimerHz(60); // smooth-ish scrolling tied to UI thread
            }
        }
        else
        {
            stopAutoScroll();
        }
    }
}

void DraggableListBoxItem::stopAutoScroll()
{
    autoScrollActive = false;
    stopTimer();
    scrollPixelsPerTick = 0.0;
}

void DraggableListBoxItem::timerCallback()
{
    if (!autoScrollActive || scrollPixelsPerTick == 0.0)
        return;

    if (auto* vp = listBox.getViewport())
    {
        auto current = vp->getViewPosition();
        const int contentH = vp->getViewedComponent() != nullptr ? vp->getViewedComponent()->getHeight() : listBox.getHeight();
        const int maxY = juce::jmax(0, contentH - vp->getHeight());
        int newY = juce::jlimit(0, maxY, current.y + (int)std::lround(scrollPixelsPerTick));
        if (newY != current.y)
        {
            vp->setViewPosition(current.x, newY);
        }
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
