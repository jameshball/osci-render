/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2020 - Raw Material Software Limited

   JUCE is an open source library subject to commercial or open-source
   licensing.

   By using JUCE, you agree to the terms of both the JUCE 6 End-User License
   Agreement and JUCE Privacy Policy (both effective as of the 16th June 2020).

   End User License Agreement: www.juce.com/juce-6-licence
   Privacy Policy: www.juce.com/juce-privacy-policy

   Or: You may also use this code under the terms of the GPL v3 (see
   www.gnu.org/licenses).

   JUCE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

#include "VListBox.h"

namespace juce
{
namespace jc
{
class ListBox::RowComponent : public Component, public TooltipClient
{
public:
    RowComponent (ListBox& lb) : owner (lb) {}

    void paint (Graphics& g) override
    {
        if (auto* m = owner.getModel())
            m->paintListBoxItem (row, g, getWidth(), getHeight(), selected);
    }

    void update (const int newRow, const bool nowSelected)
    {
        if (row != newRow || selected != nowSelected)
        {
            repaint();
            row = newRow;
            selected = nowSelected;
        }

        if (auto* m = owner.getModel())
        {
            setMouseCursor (m->getMouseCursorForRow (row));

            customComponent.reset (m->refreshComponentForRow (newRow, nowSelected, customComponent.release()));

            if (customComponent != nullptr)
            {
                addAndMakeVisible (customComponent.get());
                customComponent->setBounds (getLocalBounds());
            }
        }
    }

    void performSelection (const MouseEvent& e, bool isMouseUp)
    {
        owner.selectRowsBasedOnModifierKeys (row, e.mods, isMouseUp);

        if (auto* m = owner.getModel())
            m->listBoxItemClicked (row, e);
    }

    bool isInDragToScrollViewport() const noexcept
    {
        if (auto* vp = owner.getViewport())
            return vp->isScrollOnDragEnabled() && (vp->canScrollVertically() || vp->canScrollHorizontally());

        return false;
    }

    void mouseDown (const MouseEvent& e) override
    {
        isDragging = false;
        isDraggingToScroll = false;
        selectRowOnMouseUp = false;

        if (isEnabled())
        {
            if (owner.selectOnMouseDown && ! (selected || isInDragToScrollViewport()))
                performSelection (e, false);
            else
                selectRowOnMouseUp = true;
        }
    }

    void mouseUp (const MouseEvent& e) override
    {
        if (isEnabled() && selectRowOnMouseUp && ! (isDragging || isDraggingToScroll))
            performSelection (e, true);
    }

    void mouseDoubleClick (const MouseEvent& e) override
    {
        if (isEnabled())
            if (auto* m = owner.getModel())
                m->listBoxItemDoubleClicked (row, e);
    }

    void mouseDrag (const MouseEvent& e) override
    {
        if (auto* m = owner.getModel())
        {
            if (isEnabled() && e.mouseWasDraggedSinceMouseDown() && ! isDragging)
            {
                SparseSet<int> rowsToDrag;

                if (owner.selectOnMouseDown || owner.isRowSelected (row))
                    rowsToDrag = owner.getSelectedRows();
                else
                    rowsToDrag.addRange (Range<int>::withStartAndLength (row, 1));

                if (rowsToDrag.size() > 0)
                {
                    auto dragDescription = m->getDragSourceDescription (rowsToDrag);

                    if (! (dragDescription.isVoid() || (dragDescription.isString() && dragDescription.toString().isEmpty())))
                    {
                        isDragging = true;
                        owner.startDragAndDrop (e, rowsToDrag, dragDescription, true);
                    }
                }
            }
        }

        if (! isDraggingToScroll)
            if (auto* vp = owner.getViewport())
                isDraggingToScroll = vp->isCurrentlyScrollingOnDrag();
    }

    void resized() override
    {
        if (customComponent != nullptr)
            customComponent->setBounds (getLocalBounds());
    }

    String getTooltip() override
    {
        if (auto* m = owner.getModel())
            return m->getTooltipForRow (row);

        return {};
    }

    ListBox& owner;
    std::unique_ptr<Component> customComponent;
    int row = -1;
    bool selected = false, isDragging = false, isDraggingToScroll = false, selectRowOnMouseUp = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RowComponent)
};

//==============================================================================
class ListBox::ListViewport : public Viewport
{
public:
    ListViewport (ListBox& lb) : owner (lb)
    {
        setWantsKeyboardFocus (false);

        auto content = new Component();
        setViewedComponent (content);
        content->setWantsKeyboardFocus (false);

        updateAllRows();
    }

    RowComponent* getComponentForRow (const int row) const noexcept { return rows[row % jmax (1, rows.size())]; }

    RowComponent* getComponentForRowIfOnscreen (const int row) const noexcept
    {
        return (row >= firstIndex && row < firstIndex + rows.size()) ? getComponentForRow (row) : nullptr;
    }

    void updateAllRows() {
        // THIS IS A MODIFICATION OF THE ORIGINAL CLASS
        // 
        // This function is called to update all rows, including
        // those offscreen, so that we never need to recalculate
        // the row components.
        //
        // This makes VListBoxes that don't have many components much
        // faster, but it would be slower for VListBoxes with many
        // components.
        rows.clearQuick(true);
        auto& content = *getViewedComponent();
        content.removeAllChildren();

        for (int i = 0; i < owner.totalItems; i++) {
            auto newRow = new RowComponent(owner);
            rows.add(newRow);
            content.addChildComponent(newRow);
            newRow->update(i, owner.isRowSelected(i));
        }
    }

    int getRowNumberOfComponent (Component* const rowComponent) const noexcept
    {
        const int index = getViewedComponent()->getIndexOfChildComponent (rowComponent);
        const int num = rows.size();

        for (int i = num; --i >= 0;)
            if (((firstIndex + i) % jmax (1, num)) == index)
                return firstIndex + i;

        return -1;
    }

    void visibleAreaChanged (const Rectangle<int>&) override
    {
        updateVisibleArea (true);

        if (auto* m = owner.getModel())
            m->listWasScrolled();
    }

    void updateVisibleArea (const bool makeSureItUpdatesContent)
    {
        hasUpdated = false;

        auto& content = *getViewedComponent();
        auto newX = content.getX();
        auto newY = content.getY();
        auto newW = jmax (owner.minimumRowWidth, getMaximumVisibleWidth());
        auto newH = owner.getContentHeight();

        if (newY + newH < getMaximumVisibleHeight() && newH > getMaximumVisibleHeight())
            newY = getMaximumVisibleHeight() - newH;

        content.setBounds (newX, newY, newW, newH);

        if (makeSureItUpdatesContent && ! hasUpdated)
            updateContents();
    }

    void updateContents() {
        hasUpdated = true;
        auto& content = *getViewedComponent();

        auto y = getViewPositionY();
        auto w = content.getWidth();

        if (w == 0 || owner.model == nullptr) {
            return;
        }

        if (owner.totalItems > 0) {
            firstIndex = owner.getRowForPosition(y);
            firstWholeIndex = firstIndex;

            if (owner.getPositionForRow(firstIndex) < y) {
                ++firstWholeIndex;
            }

            auto lastRow = jmin (owner.getRowForPosition(y + getMaximumVisibleHeight()), owner.totalItems - 1);

            lastWholeIndex = lastRow - 1;

            for (int row = 0; row < owner.totalItems; row++) {
                bool componentVisible = row >= firstIndex && row <= lastRow;
                if (auto* rowComp = getComponentForRow(row)) {
                    rowComp->setVisible(componentVisible);
                    if (componentVisible) {
                        rowComp->setBounds(0, owner.getPositionForRow(row), w, owner.getRowHeight(row));
                    }
                }
            }
        } else {
            rows.clear();
        }

        if (owner.headerComponent != nullptr) {
            auto width = jmax(owner.getWidth() - owner.outlineThickness * 2, content.getWidth());
            owner.headerComponent->setBounds(owner.outlineThickness + content.getX(), owner.outlineThickness, width, owner.headerComponent->getHeight());
        }
    }

    void selectRow (const int row, const bool dontScroll, const int lastSelectedRow, const int totalRows, const bool isMouseClick)
    {
        hasUpdated = false;

        if (row < firstWholeIndex && ! dontScroll)
        {
            setViewPosition (getViewPositionX(), owner.getPositionForRow (row));
        }
        else if (row >= lastWholeIndex && ! dontScroll)
        {
            const int rowsOnScreen = lastWholeIndex - firstWholeIndex;

            if (row >= lastSelectedRow + rowsOnScreen && rowsOnScreen < totalRows - 1 && ! isMouseClick)
            {
                // put row at the top of the screen, or as close as we can make it.  but this position is already constrained by
                // setViewPosition's internals.
                // auto y = jlimit (0, jmax (0, totalRows - rowsOnScreen), row) * rowH;
                setViewPosition (getViewPositionX(), owner.getPositionForRow (row));
            }
            else
            {
                auto p = owner.getPositionForRow (row);
                auto rh = owner.getRowHeight (row);
                setViewPosition (getViewPositionX(), jmax (0, p + rh - getMaximumVisibleHeight()));
            }
        }

        if (! hasUpdated)
            updateContents();
    }

    void scrollToEnsureRowIsOnscreen (const int row)
    {
        if (row < firstWholeIndex)
        {
            setViewPosition (getViewPositionX(), owner.getPositionForRow (row));
        }
        else if (row >= lastWholeIndex)
        {
            auto p = owner.getPositionForRow (row);
            auto rh = owner.getRowHeight (row);
            setViewPosition (getViewPositionX(), jmax (0, p + rh - getMaximumVisibleHeight()));
        }
    }

    void paint (Graphics& g) override
    {
        if (isOpaque())
            g.fillAll (owner.findColour (ListBox::backgroundColourId));
    }

    bool keyPressed (const KeyPress& key) override
    {
        if (Viewport::respondsToKey (key))
        {
            const int allowableMods = owner.multipleSelection ? ModifierKeys::shiftModifier : 0;

            if ((key.getModifiers().getRawFlags() & ~allowableMods) == 0)
            {
                // we want to avoid these keypresses going to the viewport, and instead allow
                // them to pass up to our listbox..
                return false;
            }
        }

        return Viewport::keyPressed (key);
    }

private:
    ListBox& owner;
    OwnedArray<RowComponent> rows;
    int firstIndex = 0, firstWholeIndex = 0, lastWholeIndex = 0;
    bool hasUpdated = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ListViewport)
};

//==============================================================================
struct ListBoxMouseMoveSelector : public MouseListener
{
    ListBoxMouseMoveSelector (ListBox& lb) : owner (lb) { owner.addMouseListener (this, true); }

    ~ListBoxMouseMoveSelector() override { owner.removeMouseListener (this); }

    void mouseMove (const MouseEvent& e) override
    {
        auto pos = e.getEventRelativeTo (&owner).position.toInt();
        owner.selectRow (owner.getRowContainingPosition (pos.x, pos.y), true);
    }

    void mouseExit (const MouseEvent& e) override { mouseMove (e); }

    ListBox& owner;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ListBoxMouseMoveSelector)
};

int ListBoxModel::getRowForPosition (int yPos)
{
    if (! hasVariableHeightRows())
        return yPos / getRowHeight (0);

    auto y = 0;
    auto numRows = getNumRows();

    for (int r = 0; r < numRows; ++r)
    {
        y += getRowHeight (r);

        if (yPos < y)
            return r;
    }

    return numRows;
}

int ListBoxModel::getPositionForRow (int rowNumber)
{
    if (! hasVariableHeightRows())
        return rowNumber * getRowHeight (0);

    auto y = 0;

    for (int r = 0; r < rowNumber; ++r)
        y += getRowHeight (r);

    return y;
} //==============================================================================
ListBox::ListBox (const String& name, ListBoxModel* const m) : Component (name), model (m)
{
    viewport.reset (new ListViewport (*this));
    addAndMakeVisible (viewport.get());

    ListBox::setWantsKeyboardFocus (true);
    ListBox::colourChanged();
}

ListBox::~ListBox()
{
    headerComponent.reset();
    viewport.reset();
}

void ListBox::setModel (ListBoxModel* const newModel)
{
    if (model != newModel)
    {
        model = newModel;
        repaint();
        updateContent();
    }
}

void ListBox::setMultipleSelectionEnabled (bool b) noexcept
{
    multipleSelection = b;
}
void ListBox::setClickingTogglesRowSelection (bool b) noexcept
{
    alwaysFlipSelection = b;
}
void ListBox::setRowSelectedOnMouseDown (bool b) noexcept
{
    selectOnMouseDown = b;
}

void ListBox::setMouseMoveSelectsRows (bool b)
{
    if (b)
    {
        if (mouseMoveSelector == nullptr)
            mouseMoveSelector.reset (new ListBoxMouseMoveSelector (*this));
    }
    else
    {
        mouseMoveSelector.reset();
    }
}

//==============================================================================
void ListBox::paint (Graphics& g)
{
    if (! hasDoneInitialUpdate)
        updateContent();

    g.fillAll (findColour (backgroundColourId));
}

void ListBox::paintOverChildren (Graphics& g)
{
    if (outlineThickness > 0)
    {
        g.setColour (findColour (outlineColourId));
        g.drawRect (getLocalBounds(), outlineThickness);
    }
}

void ListBox::resized()
{
    viewport->setBoundsInset (BorderSize<int> (outlineThickness + (headerComponent != nullptr ? headerComponent->getHeight() : 0),
                                               outlineThickness,
                                               outlineThickness,
                                               outlineThickness));

    // viewport->setSingleStepSizes (20, getRowHeight());

    viewport->updateVisibleArea (false);
}

void ListBox::visibilityChanged()
{
    viewport->updateVisibleArea (true);
}

Viewport* ListBox::getViewport() const noexcept
{
    return viewport.get();
}

//==============================================================================
void ListBox::updateContent() {
    hasDoneInitialUpdate = true;
    totalItems = (model != nullptr) ? model->getNumRows() : 0;

    bool selectionChanged = false;

    if (selected.size() > 0 && selected[selected.size() - 1] >= totalItems)
    {
        selected.removeRange ({ totalItems, std::numeric_limits<int>::max() });
        lastRowSelected = getSelectedRow (0);
        selectionChanged = true;
    }

    viewport->updateAllRows();
    viewport->updateVisibleArea (isVisible());
    viewport->resized();

    if (selectionChanged && model != nullptr)
        model->selectedRowsChanged (lastRowSelected);
}

//==============================================================================
void ListBox::selectRow (int row, bool dontScroll, bool deselectOthersFirst)
{
    selectRowInternal (row, dontScroll, deselectOthersFirst, false);
}

void ListBox::selectRowInternal (const int row, bool dontScroll, bool deselectOthersFirst, bool isMouseClick)
{
    if (! multipleSelection)
        deselectOthersFirst = true;

    if ((! isRowSelected (row)) || (deselectOthersFirst && getNumSelectedRows() > 1))
    {
        if (isPositiveAndBelow (row, totalItems))
        {
            if (deselectOthersFirst)
                selected.clear();

            selected.addRange ({ row, row + 1 });

            if (getHeight() == 0 || getWidth() == 0)
                dontScroll = true;

            viewport->selectRow (row, dontScroll, lastRowSelected, totalItems, isMouseClick);

            lastRowSelected = row;
            model->selectedRowsChanged (row);
        }
        else
        {
            if (deselectOthersFirst)
                deselectAllRows();
        }
    }
}

void ListBox::deselectRow (const int row)
{
    if (selected.contains (row))
    {
        selected.removeRange ({ row, row + 1 });

        if (row == lastRowSelected)
            lastRowSelected = getSelectedRow (0);

        viewport->updateContents();
        model->selectedRowsChanged (lastRowSelected);
    }
}

void ListBox::setSelectedRows (const SparseSet<int>& setOfRowsToBeSelected, const NotificationType sendNotificationEventToModel)
{
    selected = setOfRowsToBeSelected;
    selected.removeRange ({ totalItems, std::numeric_limits<int>::max() });

    if (! isRowSelected (lastRowSelected))
        lastRowSelected = getSelectedRow (0);

    viewport->updateContents();

    if (model != nullptr && sendNotificationEventToModel == sendNotification)
        model->selectedRowsChanged (lastRowSelected);
}

SparseSet<int> ListBox::getSelectedRows() const
{
    return selected;
}

void ListBox::selectRangeOfRows (int firstRow, int lastRow, bool dontScrollToShowThisRange)
{
    if (multipleSelection && (firstRow != lastRow))
    {
        const int numRows = totalItems - 1;
        firstRow = jlimit (0, jmax (0, numRows), firstRow);
        lastRow = jlimit (0, jmax (0, numRows), lastRow);

        selected.addRange ({ jmin (firstRow, lastRow), jmax (firstRow, lastRow) + 1 });

        selected.removeRange ({ lastRow, lastRow + 1 });
    }

    selectRowInternal (lastRow, dontScrollToShowThisRange, false, true);
}

void ListBox::flipRowSelection (const int row)
{
    if (isRowSelected (row))
        deselectRow (row);
    else
        selectRowInternal (row, false, false, true);
}

void ListBox::deselectAllRows()
{
    if (! selected.isEmpty())
    {
        selected.clear();
        lastRowSelected = -1;

        viewport->updateContents();

        if (model != nullptr)
            model->selectedRowsChanged (lastRowSelected);
    }
}

void ListBox::selectRowsBasedOnModifierKeys (const int row, ModifierKeys mods, const bool isMouseUpEvent)
{
    if (multipleSelection && (mods.isCommandDown() || alwaysFlipSelection))
    {
        flipRowSelection (row);
    }
    else if (multipleSelection && mods.isShiftDown() && lastRowSelected >= 0)
    {
        selectRangeOfRows (lastRowSelected, row);
    }
    else if ((! mods.isPopupMenu()) || ! isRowSelected (row))
    {
        selectRowInternal (row, false, ! (multipleSelection && (! isMouseUpEvent) && isRowSelected (row)), true);
    }
}

int ListBox::getNumSelectedRows() const
{
    return selected.size();
}

int ListBox::getSelectedRow (const int index) const
{
    return (isPositiveAndBelow (index, selected.size())) ? selected[index] : -1;
}

bool ListBox::isRowSelected (const int row) const
{
    return selected.contains (row);
}

int ListBox::getLastRowSelected() const
{
    return isRowSelected (lastRowSelected) ? lastRowSelected : -1;
}

//==============================================================================
int ListBox::getRowContainingPosition (const int x, const int y) const noexcept
{
    if (isPositiveAndBelow (x, getWidth()))
    {
        const int row = getRowForPosition (viewport->getViewPositionY() + y - viewport->getY());

        if (isPositiveAndBelow (row, totalItems))
            return row;
    }

    return -1;
}

int ListBox::getRowHeight (int row) const
{
    if (model == nullptr)
        return 0;

    return model->getRowHeight (row);
}

int ListBox::getInsertionIndexForPosition (const int x, const int y) const noexcept
{
    if (isPositiveAndBelow (x, getWidth()))
    {
        auto cursorY = y + viewport->getViewPositionY() - viewport->getY();
        auto row = getRowForPosition (cursorY);
        auto rowY = getPositionForRow (row);
        auto rowCentre = rowY + getRowHeight (row) / 2;

        if (rowCentre < cursorY)
            ++row;

        return jlimit (0, totalItems, row);
    }

    return -1;
}

Component* ListBox::getComponentForRowNumber (const int row) const noexcept
{
    if (auto* listRowComp = viewport->getComponentForRowIfOnscreen (row))
        return listRowComp->customComponent.get();

    return nullptr;
}

int ListBox::getRowNumberOfComponent (Component* const rowComponent) const noexcept
{
    return viewport->getRowNumberOfComponent (rowComponent);
}

Rectangle<int> ListBox::getRowPosition (int rowNumber, bool relativeToComponentTopLeft) const noexcept
{
    auto y = viewport->getY() + getPositionForRow (rowNumber);

    if (relativeToComponentTopLeft)
        y -= viewport->getViewPositionY();

    return { viewport->getX(), y, viewport->getViewedComponent()->getWidth(), getRowHeight (rowNumber) };
}

void ListBox::setVerticalPosition (const double proportion)
{
    auto offscreen = viewport->getViewedComponent()->getHeight() - viewport->getHeight();

    viewport->setViewPosition (viewport->getViewPositionX(), jmax (0, roundToInt (proportion * offscreen)));
}

double ListBox::getVerticalPosition() const
{
    auto offscreen = viewport->getViewedComponent()->getHeight() - viewport->getHeight();

    return offscreen > 0 ? viewport->getViewPositionY() / (double) offscreen : 0;
}

int ListBox::getVisibleRowWidth() const noexcept
{
    return viewport->getViewWidth();
}

void ListBox::scrollToEnsureRowIsOnscreen (const int row)
{
    viewport->scrollToEnsureRowIsOnscreen (row);
}

//==============================================================================
bool ListBox::keyPressed (const KeyPress& key)
{
    const bool multiple = multipleSelection && lastRowSelected >= 0 && key.getModifiers().isShiftDown();

    if (key.isKeyCode (KeyPress::upKey))
    {
        if (multiple)
            selectRangeOfRows (lastRowSelected, lastRowSelected - 1);
        else
            selectRow (jmax (0, lastRowSelected - 1));
    }
    else if (key.isKeyCode (KeyPress::downKey))
    {
        if (multiple)
            selectRangeOfRows (lastRowSelected, lastRowSelected + 1);
        else
            selectRow (jmin (totalItems - 1, jmax (0, lastRowSelected + 1)));
    }
    else if (key.isKeyCode (KeyPress::pageUpKey)) 
    {
        auto rowToSelect = jmax(0, lastRowSelected);
        auto pageHeight = viewport->getMaximumVisibleHeight();

        while (pageHeight > 0 && rowToSelect > 0)
        {
            pageHeight -= getRowHeight (rowToSelect);

            if (pageHeight > 0)
                rowToSelect--;
        }

        if (multiple)
            selectRangeOfRows (lastRowSelected, rowToSelect);
        else
            selectRow (rowToSelect);
    }
    else if (key.isKeyCode (KeyPress::pageDownKey))
    {
        auto rowToSelect = jmax(0, lastRowSelected);
        auto pageHeight= viewport->getMaximumVisibleHeight();

        while (pageHeight > 0 && rowToSelect < totalItems - 1)
        {
            pageHeight -= getRowHeight (rowToSelect);

            if (pageHeight > 0)
                rowToSelect++;
        }

        if (multiple)
            selectRangeOfRows (lastRowSelected, rowToSelect);
        else
            selectRow (rowToSelect);
    }
    else if (key.isKeyCode (KeyPress::homeKey))
    {
        if (multiple)
            selectRangeOfRows (lastRowSelected, 0);
        else
            selectRow (0);
    }
    else if (key.isKeyCode (KeyPress::endKey))
    {
        if (multiple)
            selectRangeOfRows (lastRowSelected, totalItems - 1);
        else
            selectRow (totalItems - 1);
    }
    else if (key.isKeyCode (KeyPress::returnKey) && isRowSelected (lastRowSelected))
    {
        if (model != nullptr)
            model->returnKeyPressed (lastRowSelected);
    }
    else if ((key.isKeyCode (KeyPress::deleteKey) || key.isKeyCode (KeyPress::backspaceKey)) && isRowSelected (lastRowSelected))
    {
        if (model != nullptr)
            model->deleteKeyPressed (lastRowSelected);
    }
    else if (multipleSelection && key == KeyPress ('a', ModifierKeys::commandModifier, 0))
    {
        selectRangeOfRows (0, std::numeric_limits<int>::max());
    }
    else
    {
        return false;
    }

    return true;
}

bool ListBox::keyStateChanged (const bool isKeyDown)
{
    return isKeyDown
           && (KeyPress::isKeyCurrentlyDown (KeyPress::upKey) || KeyPress::isKeyCurrentlyDown (KeyPress::pageUpKey)
               || KeyPress::isKeyCurrentlyDown (KeyPress::downKey) || KeyPress::isKeyCurrentlyDown (KeyPress::pageDownKey)
               || KeyPress::isKeyCurrentlyDown (KeyPress::homeKey) || KeyPress::isKeyCurrentlyDown (KeyPress::endKey)
               || KeyPress::isKeyCurrentlyDown (KeyPress::returnKey));
}

void ListBox::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    bool eventWasUsed = false;

    if (wheel.deltaX != 0.0f && getHorizontalScrollBar().isVisible())
    {
        eventWasUsed = true;
        getHorizontalScrollBar().mouseWheelMove (e, wheel);
    }

    if (wheel.deltaY != 0.0f && getVerticalScrollBar().isVisible())
    {
        eventWasUsed = true;
        getVerticalScrollBar().mouseWheelMove (e, wheel);
    }

    if (! eventWasUsed)
        Component::mouseWheelMove (e, wheel);
}

void ListBox::mouseUp (const MouseEvent& e)
{
    if (e.mouseWasClicked() && model != nullptr)
        model->backgroundClicked (e);
}

//==============================================================================

int ListBox::getNumRowsOnScreen() const noexcept
{
    // todo: not clear this function can work as previously intended

    auto start = getRowContainingPosition (0, viewport->getViewPositionY());
    auto maxPosition = viewport->getViewArea().getBottom();
    auto rowNumber = start;

    while (rowNumber < totalItems && getPositionForRow (rowNumber) < maxPosition)
        rowNumber++;

    return rowNumber - start;
}

void ListBox::setMinimumContentWidth (const int newMinimumWidth)
{
    minimumRowWidth = newMinimumWidth;
    updateContent();
}

int ListBox::getVisibleContentWidth() const noexcept
{
    return viewport->getMaximumVisibleWidth();
}

ScrollBar& ListBox::getVerticalScrollBar() const noexcept
{
    return viewport->getVerticalScrollBar();
}
ScrollBar& ListBox::getHorizontalScrollBar() const noexcept
{
    return viewport->getHorizontalScrollBar();
}

void ListBox::colourChanged()
{
    setOpaque (findColour (backgroundColourId).isOpaque());
    viewport->setOpaque (isOpaque());
    repaint();
}

void ListBox::parentHierarchyChanged()
{
    colourChanged();
}

void ListBox::setOutlineThickness (int newThickness)
{
    outlineThickness = newThickness;
    resized();
}

void ListBox::setHeaderComponent (std::unique_ptr<Component> newHeaderComponent)
{
    headerComponent = std::move (newHeaderComponent);
    addAndMakeVisible (headerComponent.get());
    ListBox::resized();
}

void ListBox::repaintRow (const int rowNumber) noexcept
{
    repaint (getRowPosition (rowNumber, true));
}

Image ListBox::createSnapshotOfRows (const SparseSet<int>& rows, int& imageX, int& imageY)
{
    Rectangle<int> imageArea;
    auto firstRow = getRowContainingPosition (0, viewport->getY());

    for (int i = getNumRowsOnScreen() + 2; --i >= 0;)
    {
        if (rows.contains (firstRow + i))
        {
            if (auto* rowComp = viewport->getComponentForRowIfOnscreen (firstRow + i))
            {
                auto pos = getLocalPoint (rowComp, Point<int>());

                imageArea = imageArea.getUnion ({ pos.x, pos.y, rowComp->getWidth(), rowComp->getHeight() });
            }
        }
    }

    imageArea = imageArea.getIntersection (getLocalBounds());
    imageX = imageArea.getX();
    imageY = imageArea.getY();

    auto listScale = Component::getApproximateScaleFactorForComponent (this);
    Image snapshot (
        Image::ARGB, roundToInt ((float) imageArea.getWidth() * listScale), roundToInt ((float) imageArea.getHeight() * listScale), true);

    for (int i = getNumRowsOnScreen() + 2; --i >= 0;)
    {
        if (rows.contains (firstRow + i))
        {
            if (auto* rowComp = viewport->getComponentForRowIfOnscreen (firstRow + i))
            {
                Graphics g (snapshot);
                g.setOrigin (getLocalPoint (rowComp, Point<int>()) - imageArea.getPosition());

                auto rowScale = Component::getApproximateScaleFactorForComponent (rowComp);

                if (g.reduceClipRegion (rowComp->getLocalBounds() * rowScale))
                {
                    g.beginTransparencyLayer (0.6f);
                    g.addTransform (AffineTransform::scale (rowScale));
                    rowComp->paintEntireComponent (g, false);
                    g.endTransparencyLayer();
                }
            }
        }
    }

    return snapshot;
}

void ListBox::startDragAndDrop (const MouseEvent& e,
                                const SparseSet<int>& rowsToDrag,
                                const var& dragDescription,
                                bool allowDraggingToOtherWindows)
{
    if (auto* dragContainer = DragAndDropContainer::findParentDragContainerFor (this))
    {
        int x, y;
        auto dragImage = createSnapshotOfRows (rowsToDrag, x, y);

        auto p = Point<int> (x, y) - e.getEventRelativeTo (this).position.toInt();
        dragContainer->startDragging (dragDescription, this, dragImage, allowDraggingToOtherWindows, &p, &e.source);
    }
    else
    {
        // to be able to do a drag-and-drop operation, the listbox needs to
        // be inside a component which is also a DragAndDropContainer.
        jassertfalse;
    }
}

int ListBox::getContentHeight() const
{
    if (model == nullptr || totalItems == 0)
        return 0;

    return getPositionForRow (totalItems - 1) + getRowHeight (totalItems - 1);
}

int ListBox::getRowForPosition (int y) const
{
    if (model == nullptr || y < 0)
        return 0;

    return model->getRowForPosition (y);
}

int ListBox::getPositionForRow (int row) const
{
    if (model == nullptr)
        return 0;

    jassert (row >= 0 && row < totalItems);

    return model->getPositionForRow (row);
}

//==============================================================================
Component* ListBoxModel::refreshComponentForRow (int, bool, Component* existingComponentToUpdate)
{
    ignoreUnused (existingComponentToUpdate);
    jassert (existingComponentToUpdate == nullptr); // indicates a failure in the code that recycles the components
    return nullptr;
}

void ListBoxModel::listBoxItemClicked (int, const MouseEvent&) {}
void ListBoxModel::listBoxItemDoubleClicked (int, const MouseEvent&) {}
void ListBoxModel::backgroundClicked (const MouseEvent&) {}
void ListBoxModel::selectedRowsChanged (int) {}
void ListBoxModel::deleteKeyPressed (int) {}
void ListBoxModel::returnKeyPressed (int) {}
void ListBoxModel::listWasScrolled() {}
var ListBoxModel::getDragSourceDescription (const SparseSet<int>&)
{
    return {};
}
String ListBoxModel::getTooltipForRow (int)
{
    return {};
}
MouseCursor ListBoxModel::getMouseCursorForRow (int)
{
    return MouseCursor::NormalCursor;
}

} // namespace jc
} // namespace juce