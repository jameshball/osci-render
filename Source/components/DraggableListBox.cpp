#include "DraggableListBox.h"
#include <cmath>

DraggableListBoxItemData::~DraggableListBoxItemData() {};

void DraggableListBoxItem::paint(juce::Graphics& g)
{
    // Per-item insertion lines are suppressed in favour of a single overlay drawn by DraggableListBox.
    juce::ignoreUnused(g);
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
    auto* obj = new juce::DynamicObject();
    obj->setProperty("type", juce::var("DraggableListBoxItem"));
    obj->setProperty("row", juce::var(rowNum));
    container->startDragging(juce::var(obj), this);
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
    // Update the global overlay on the parent list box
    auto ptGlobal = localPointToGlobal(dragSourceDetails.localPosition);
    auto ptInLB = listBox.getLocalPoint(nullptr, ptGlobal);
    listBox.updateDropIndicatorAt(ptInLB);
}

void DraggableListBoxItem::itemDragMove(const SourceDetails& dragSourceDetails)
{
    updateInsertLines(dragSourceDetails);
    updateAutoScroll(dragSourceDetails);
    auto ptGlobal = localPointToGlobal(dragSourceDetails.localPosition);
    auto ptInLB = listBox.getLocalPoint(nullptr, ptGlobal);
    listBox.updateDropIndicatorAt(ptInLB);
}

void DraggableListBoxItem::itemDragExit(const SourceDetails& /*dragSourceDetails*/)
{
    hideInsertLines();
    stopAutoScroll();
    listBox.clearDropIndicator();
}

void DraggableListBoxItem::itemDropped(const juce::DragAndDropTarget::SourceDetails &dragSourceDetails)
{
    hideInsertLines();
    stopAutoScroll();
    listBox.clearDropIndicator();
    if (DraggableListBoxItem* item = dynamic_cast<DraggableListBoxItem*>(dragSourceDetails.sourceComponent.get()))
    {
        // Determine insertion index relative to whole list (not item local space)
        auto ptGlobal = localPointToGlobal(dragSourceDetails.localPosition);
        auto ptInLB = listBox.getLocalPoint(nullptr, ptGlobal);
        int insertIndex = listBox.getInsertionIndexForPosition(ptInLB.x, ptInLB.y);
        if (insertIndex < 0) insertIndex = 0; // allow header top

        if (auto* m = dynamic_cast<DraggableListBoxModel*>(listBox.getModel()))
        {
            m->moveByInsertIndex(item->rowNum, insertIndex);
        }
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

    // Update the global drop indicator position based on current mouse position (even if the mouse isn't moving)
    auto screenPos = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition();
    auto posInLB = listBox.getLocalPoint(nullptr, screenPos.toInt());
    listBox.updateDropIndicatorAt(posInLB);
    }
}

// ===================== DraggableListBox overlay indicator =====================

void DraggableListBox::updateDropIndicator(const SourceDetails& details)
{
    // localPosition is already in this component's coordinate space
    const auto pt = details.localPosition;
    int index = getInsertionIndexForPosition(pt.x, pt.y);
    if (index < 0) index = 0; // allow showing at very top (over header spacer)
    dropInsertIndex = index;
    showDropIndicator = true;
    repaint();
}

void DraggableListBox::clearDropIndicator()
{
    showDropIndicator = false;
    dropInsertIndex = -1;
    repaint();
}

void DraggableListBox::updateDropIndicatorAt(const juce::Point<int>& listLocalPos)
{
    int index = getInsertionIndexForPosition(listLocalPos.x, listLocalPos.y);
    if (index < 0) index = 0; // allow showing at very top (over header spacer)
    dropInsertIndex = index;
    showDropIndicator = true;
    repaint();
}

void DraggableListBox::paintOverChildren(juce::Graphics& g)
{
    VListBox::paintOverChildren(g);
    if (!showDropIndicator) return;

    const int numRows = getModel() != nullptr ? getModel()->getNumRows() : 0;
    if (dropInsertIndex < 0 || dropInsertIndex > numRows) return;

    auto* vp = getViewport();
    if (vp == nullptr) return;

    // Determine the y position between rows to draw the indicator line
    int y = 0;
    if (dropInsertIndex == 0)
    {
        // Top of first row (below header)
        if (numRows > 0)
            y = getRowPosition(0, true).getY();
        else
            y = 0;
    }
    else if (dropInsertIndex >= numRows)
    {
        auto lastRowBounds = getRowPosition(numRows - 1, true);
        y = lastRowBounds.getBottom();
    }
    else
    {
        auto prevBounds = getRowPosition(dropInsertIndex - 1, true);
        y = prevBounds.getBottom();
    }

    // Draw a prominent indicator spanning the visible row width
    const int x = 0;
    const int w = getVisibleRowWidth();
    const int thickness = 3;
    const juce::Colour colour = juce::Colours::lime.withAlpha(0.9f);

    const float yOffset = -2.5f; // Offset to center the line visually

    g.setColour(colour);
    g.fillRoundedRectangle(x, y - thickness / 2 + yOffset, w, thickness, 2.0f);
}

void DraggableListBox::itemDropped(const SourceDetails& details)
{
    // Background drop: compute insertion index and use model to move
    int insertIndex = -1;
    // localPosition is already relative to this list
    insertIndex = getInsertionIndexForPosition(details.localPosition.x, details.localPosition.y);
    if (insertIndex < 0) insertIndex = 0; // clamp to top when over header spacer

    if (auto* m = dynamic_cast<DraggableListBoxModel*>(getModel()))
    {
        int fromIndex = -1;
        const juce::var& desc = details.description;
        if (desc.isObject())
        {
            auto* obj = desc.getDynamicObject();
            if (obj != nullptr)
            {
                auto v = obj->getProperty("row");
                if (v.isInt()) fromIndex = (int)v;
            }
        }

        if (fromIndex >= 0 && insertIndex >= 0) {
            m->moveByInsertIndex(fromIndex, insertIndex);
        }
    }

    clearDropIndicator();
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
