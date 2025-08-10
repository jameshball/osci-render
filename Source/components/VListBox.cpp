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

class VListBox::RowComponent : public juce::Component, public TooltipClient
{
public:
    RowComponent (VListBox& lb) : owner (lb) {}

    void paint (juce::Graphics& g) override
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

    void performSelection (const juce::MouseEvent& e, bool isMouseUp)
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

    void mouseDown (const juce::MouseEvent& e) override
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

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (isEnabled() && selectRowOnMouseUp && ! (isDragging || isDraggingToScroll))
            performSelection (e, true);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (isEnabled())
            if (auto* m = owner.getModel())
                m->listBoxItemDoubleClicked (row, e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (auto* m = owner.getModel())
        {
            if (isEnabled() && e.mouseWasDraggedSinceMouseDown() && ! isDragging)
            {
                juce::SparseSet<int> rowsToDrag;

                if (owner.selectOnMouseDown || owner.isRowSelected (row))
                    rowsToDrag = owner.getSelectedRows();
                else
                    rowsToDrag.addRange (juce::Range<int>::withStartAndLength (row, 1));

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

    juce::String getTooltip() override
    {
        if (auto* m = owner.getModel())
            return m->getTooltipForRow (row);

        return {};
    }

    VListBox& owner;
    std::unique_ptr<juce::Component> customComponent;
    int row = -1;
    bool selected = false, isDragging = false, isDraggingToScroll = false, selectRowOnMouseUp = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RowComponent)
};

//==============================================================================
class VListBox::ListViewport : public juce::Viewport
{
public:
    ListViewport (VListBox& lb) : owner (lb)
    {
        setWantsKeyboardFocus (false);

        auto content = new juce::Component();
        setViewedComponent (content);
        content->setWantsKeyboardFocus (false);

        updateAllRows();
    }

    RowComponent* getComponentForRow (const int row) const noexcept { return rows[row % juce::jmax (1, rows.size())]; }

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

    int getRowNumberOfComponent (juce::Component* const rowComponent) const noexcept
    {
        const int index = getViewedComponent()->getIndexOfChildComponent (rowComponent);
        const int num = rows.size();

        for (int i = num; --i >= 0;)
            if (((firstIndex + i) % juce::jmax (1, num)) == index)
                return firstIndex + i;

        return -1;
    }

    void visibleAreaChanged (const juce::Rectangle<int>&) override
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
        auto newW = juce::jmax (owner.minimumRowWidth, getMaximumVisibleWidth());
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

            auto lastRow = juce::jmin (owner.getRowForPosition(y + getMaximumVisibleHeight()), owner.totalItems - 1);

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
            auto width = juce::jmax(owner.getWidth() - owner.outlineThickness * 2, content.getWidth());
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
                // auto y = juce::jlimit (0, juce::jmax (0, totalRows - rowsOnScreen), row) * rowH;
                setViewPosition (getViewPositionX(), owner.getPositionForRow (row));
            }
            else
            {
                auto p = owner.getPositionForRow (row);
                auto rh = owner.getRowHeight (row);
                setViewPosition (getViewPositionX(), juce::jmax (0, p + rh - getMaximumVisibleHeight()));
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
            setViewPosition (getViewPositionX(), juce::jmax (0, p + rh - getMaximumVisibleHeight()));
        }
    }

    void paint (juce::Graphics& g) override
    {
        if (isOpaque())
            g.fillAll (owner.findColour (VListBox::backgroundColourId));
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (juce::Viewport::respondsToKey (key))
        {
            const int allowableMods = owner.multipleSelection ? juce::ModifierKeys::shiftModifier : 0;

            if ((key.getModifiers().getRawFlags() & ~allowableMods) == 0)
            {
                // we want to avoid these keypresses going to the viewport, and instead allow
                // them to pass up to our listbox..
                return false;
            }
        }

        return juce::Viewport::keyPressed (key);
    }

private:
    VListBox& owner;
    juce::OwnedArray<RowComponent> rows;
    int firstIndex = 0, firstWholeIndex = 0, lastWholeIndex = 0;
    bool hasUpdated = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ListViewport)
};

//==============================================================================
struct ListBoxMouseMoveSelector : public juce::MouseListener
{
    ListBoxMouseMoveSelector (VListBox& lb) : owner (lb) { owner.addMouseListener (this, true); }

    ~ListBoxMouseMoveSelector() override { owner.removeMouseListener (this); }

    void mouseMove (const juce::MouseEvent& e) override
    {
        auto pos = e.getEventRelativeTo (&owner).position.toInt();
        owner.selectRow (owner.getRowContainingPosition (pos.x, pos.y), true);
    }

    void mouseExit (const juce::MouseEvent& e) override { mouseMove (e); }

    VListBox& owner;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ListBoxMouseMoveSelector)
};

int VListBoxModel::getRowForPosition (int yPos)
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

int VListBoxModel::getPositionForRow (int rowNumber)
{
    if (! hasVariableHeightRows())
        return rowNumber * getRowHeight (0);

    auto y = 0;

    for (int r = 0; r < rowNumber; ++r)
        y += getRowHeight (r);

    return y;
} //==============================================================================
VListBox::VListBox (const juce::String& name, VListBoxModel* const m) : juce::Component (name), model (m)
{
    viewport.reset (new ListViewport (*this));
    addAndMakeVisible (viewport.get());

    VListBox::setWantsKeyboardFocus (true);
    VListBox::colourChanged();
}

VListBox::~VListBox()
{
    headerComponent.reset();
    viewport.reset();
}

void VListBox::setModel (VListBoxModel* const newModel)
{
    if (model != newModel)
    {
        model = newModel;
        repaint();
        updateContent();
    }
}

void VListBox::setMultipleSelectionEnabled (bool b) noexcept
{
    multipleSelection = b;
}
void VListBox::setClickingTogglesRowSelection (bool b) noexcept
{
    alwaysFlipSelection = b;
}
void VListBox::setRowSelectedOnMouseDown (bool b) noexcept
{
    selectOnMouseDown = b;
}

void VListBox::setMouseMoveSelectsRows (bool b)
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
void VListBox::paint (juce::Graphics& g)
{
    if (! hasDoneInitialUpdate)
        updateContent();

    g.fillAll (findColour (backgroundColourId));
}

void VListBox::paintOverChildren (juce::Graphics& g)
{
    if (outlineThickness > 0)
    {
        g.setColour (findColour (outlineColourId));
        g.drawRect (getLocalBounds(), outlineThickness);
    }
}

void VListBox::resized()
{
    viewport->setBoundsInset (juce::BorderSize<int> (outlineThickness + (headerComponent != nullptr ? headerComponent->getHeight() : 0),
                                               outlineThickness,
                                               outlineThickness,
                                               outlineThickness));

    // viewport->setSingleStepSizes (20, getRowHeight());

    viewport->updateVisibleArea (false);
}

void VListBox::visibilityChanged()
{
    viewport->updateVisibleArea (true);
}

juce::Viewport* VListBox::getViewport() const noexcept
{
    return viewport.get();
}

//==============================================================================
void VListBox::updateContent() {
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
void VListBox::selectRow (int row, bool dontScroll, bool deselectOthersFirst)
{
    selectRowInternal (row, dontScroll, deselectOthersFirst, false);
}

void VListBox::selectRowInternal (const int row, bool dontScroll, bool deselectOthersFirst, bool isMouseClick)
{
    if (! multipleSelection)
        deselectOthersFirst = true;

    if ((! isRowSelected (row)) || (deselectOthersFirst && getNumSelectedRows() > 1))
    {
        if (juce::isPositiveAndBelow (row, totalItems))
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

void VListBox::deselectRow (const int row)
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

void VListBox::setSelectedRows (const juce::SparseSet<int>& setOfRowsToBeSelected, const juce::NotificationType sendNotificationEventToModel)
{
    selected = setOfRowsToBeSelected;
    selected.removeRange ({ totalItems, std::numeric_limits<int>::max() });

    if (! isRowSelected (lastRowSelected))
        lastRowSelected = getSelectedRow (0);

    viewport->updateContents();

    if (model != nullptr && sendNotificationEventToModel == juce::sendNotification)
        model->selectedRowsChanged (lastRowSelected);
}

juce::SparseSet<int> VListBox::getSelectedRows() const
{
    return selected;
}

void VListBox::selectRangeOfRows (int firstRow, int lastRow, bool dontScrollToShowThisRange)
{
    if (multipleSelection && (firstRow != lastRow))
    {
        const int numRows = totalItems - 1;
        firstRow = juce::jlimit (0, juce::jmax (0, numRows), firstRow);
        lastRow = juce::jlimit (0, juce::jmax (0, numRows), lastRow);

        selected.addRange ({ juce::jmin (firstRow, lastRow), juce::jmax (firstRow, lastRow) + 1 });

        selected.removeRange ({ lastRow, lastRow + 1 });
    }

    selectRowInternal (lastRow, dontScrollToShowThisRange, false, true);
}

void VListBox::flipRowSelection (const int row)
{
    if (isRowSelected (row))
        deselectRow (row);
    else
        selectRowInternal (row, false, false, true);
}

void VListBox::deselectAllRows()
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

void VListBox::selectRowsBasedOnModifierKeys (const int row, juce::ModifierKeys mods, const bool isMouseUpEvent)
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

int VListBox::getNumSelectedRows() const
{
    return selected.size();
}

int VListBox::getSelectedRow (const int index) const
{
    return (juce::isPositiveAndBelow (index, selected.size())) ? selected[index] : -1;
}

bool VListBox::isRowSelected (const int row) const
{
    return selected.contains (row);
}

int VListBox::getLastRowSelected() const
{
    return isRowSelected (lastRowSelected) ? lastRowSelected : -1;
}

//==============================================================================
int VListBox::getRowContainingPosition (const int x, const int y) const noexcept
{
    if (juce::isPositiveAndBelow (x, getWidth()))
    {
        const int row = getRowForPosition (viewport->getViewPositionY() + y - viewport->getY());

        if (juce::isPositiveAndBelow (row, totalItems))
            return row;
    }

    return -1;
}

int VListBox::getRowHeight (int row) const
{
    if (model == nullptr)
        return 0;

    return model->getRowHeight (row);
}

int VListBox::getInsertionIndexForPosition (const int x, const int y) const noexcept
{
    if (juce::isPositiveAndBelow (x, getWidth()))
    {
        auto cursorY = y + viewport->getViewPositionY() - viewport->getY();
        auto row = getRowForPosition (cursorY);
        auto rowY = getPositionForRow (row);
        auto rowCentre = rowY + getRowHeight (row) / 2;

        if (rowCentre < cursorY)
            ++row;

        return juce::jlimit (0, totalItems, row);
    }

    return -1;
}

juce::Component* VListBox::getComponentForRowNumber (const int row) const noexcept
{
    if (auto* listRowComp = viewport->getComponentForRowIfOnscreen (row))
        return listRowComp->customComponent.get();

    return nullptr;
}

int VListBox::getRowNumberOfComponent (juce::Component* const rowComponent) const noexcept
{
    return viewport->getRowNumberOfComponent (rowComponent);
}

juce::Rectangle<int> VListBox::getRowPosition (int rowNumber, bool relativeToComponentTopLeft) const noexcept
{
    auto y = viewport->getY() + getPositionForRow (rowNumber);

    if (relativeToComponentTopLeft)
        y -= viewport->getViewPositionY();

    return { viewport->getX(), y, viewport->getViewedComponent()->getWidth(), getRowHeight (rowNumber) };
}

void VListBox::setVerticalPosition (const double proportion)
{
    auto offscreen = viewport->getViewedComponent()->getHeight() - viewport->getHeight();

    viewport->setViewPosition (viewport->getViewPositionX(), juce::jmax (0, juce::roundToInt (proportion * offscreen)));
}

double VListBox::getVerticalPosition() const
{
    auto offscreen = viewport->getViewedComponent()->getHeight() - viewport->getHeight();

    return offscreen > 0 ? viewport->getViewPositionY() / (double) offscreen : 0;
}

int VListBox::getVisibleRowWidth() const noexcept
{
    return viewport->getViewWidth();
}

void VListBox::scrollToEnsureRowIsOnscreen (const int row)
{
    viewport->scrollToEnsureRowIsOnscreen (row);
}

//==============================================================================
bool VListBox::keyPressed (const juce::KeyPress& key)
{
    const bool multiple = multipleSelection && lastRowSelected >= 0 && key.getModifiers().isShiftDown();

    if (key.isKeyCode (juce::KeyPress::upKey))
    {
        if (multiple)
            selectRangeOfRows (lastRowSelected, lastRowSelected - 1);
        else
            selectRow (juce::jmax (0, lastRowSelected - 1));
    }
    else if (key.isKeyCode (juce::KeyPress::downKey))
    {
        if (multiple)
            selectRangeOfRows (lastRowSelected, lastRowSelected + 1);
        else
            selectRow (juce::jmin (totalItems - 1, juce::jmax (0, lastRowSelected + 1)));
    }
    else if (key.isKeyCode (juce::KeyPress::pageUpKey)) 
    {
        auto rowToSelect = juce::jmax(0, lastRowSelected);
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
    else if (key.isKeyCode (juce::KeyPress::pageDownKey))
    {
        auto rowToSelect = juce::jmax(0, lastRowSelected);
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
    else if (key.isKeyCode (juce::KeyPress::homeKey))
    {
        if (multiple)
            selectRangeOfRows (lastRowSelected, 0);
        else
            selectRow (0);
    }
    else if (key.isKeyCode (juce::KeyPress::endKey))
    {
        if (multiple)
            selectRangeOfRows (lastRowSelected, totalItems - 1);
        else
            selectRow (totalItems - 1);
    }
    else if (key.isKeyCode (juce::KeyPress::returnKey) && isRowSelected (lastRowSelected))
    {
        if (model != nullptr)
            model->returnKeyPressed (lastRowSelected);
    }
    else if ((key.isKeyCode (juce::KeyPress::deleteKey) || key.isKeyCode (juce::KeyPress::backspaceKey)) && isRowSelected (lastRowSelected))
    {
        if (model != nullptr)
            model->deleteKeyPressed (lastRowSelected);
    }
    else if (multipleSelection && key == juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0))
    {
        selectRangeOfRows (0, std::numeric_limits<int>::max());
    }
    else
    {
        return false;
    }

    return true;
}

bool VListBox::keyStateChanged (const bool isKeyDown)
{
    return isKeyDown
           && (juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::upKey) || juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::pageUpKey)
               || juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::downKey) || juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::pageDownKey)
               || juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::homeKey) || juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::endKey)
               || juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::returnKey));
}

void VListBox::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
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
        juce::Component::mouseWheelMove (e, wheel);
}

void VListBox::mouseUp (const juce::MouseEvent& e)
{
    if (e.mouseWasClicked() && model != nullptr)
        model->backgroundClicked (e);
}

//==============================================================================

int VListBox::getNumRowsOnScreen() const noexcept
{
    // todo: not clear this function can work as previously intended

    auto start = getRowContainingPosition (0, viewport->getViewPositionY());
    auto maxPosition = viewport->getViewArea().getBottom();
    auto rowNumber = start;

    while (rowNumber < totalItems && getPositionForRow (rowNumber) < maxPosition)
        rowNumber++;

    return rowNumber - start;
}

void VListBox::setMinimumContentWidth (const int newMinimumWidth)
{
    minimumRowWidth = newMinimumWidth;
    updateContent();
}

int VListBox::getVisibleContentWidth() const noexcept
{
    return viewport->getMaximumVisibleWidth();
}

juce::ScrollBar& VListBox::getVerticalScrollBar() const noexcept
{
    return viewport->getVerticalScrollBar();
}
juce::ScrollBar& VListBox::getHorizontalScrollBar() const noexcept
{
    return viewport->getHorizontalScrollBar();
}

void VListBox::colourChanged()
{
    setOpaque (findColour (backgroundColourId).isOpaque());
    viewport->setOpaque (isOpaque());
    repaint();
}

void VListBox::parentHierarchyChanged()
{
    colourChanged();
}

void VListBox::setOutlineThickness (int newThickness)
{
    outlineThickness = newThickness;
    resized();
}

void VListBox::setHeaderComponent (std::unique_ptr<juce::Component> newHeaderComponent)
{
    headerComponent = std::move (newHeaderComponent);
    addAndMakeVisible (headerComponent.get());
    VListBox::resized();
}

void VListBox::repaintRow (const int rowNumber) noexcept
{
    repaint (getRowPosition (rowNumber, true));
}

juce::Image VListBox::createSnapshotOfRows (const juce::SparseSet<int>& rows, int& imageX, int& imageY)
{
    juce::Rectangle<int> imageArea;
    auto firstRow = getRowContainingPosition (0, viewport->getY());

    for (int i = getNumRowsOnScreen() + 2; --i >= 0;)
    {
        if (rows.contains (firstRow + i))
        {
            if (auto* rowComp = viewport->getComponentForRowIfOnscreen (firstRow + i))
            {
                auto pos = getLocalPoint (rowComp, juce::Point<int>());

                imageArea = imageArea.getUnion ({ pos.x, pos.y, rowComp->getWidth(), rowComp->getHeight() });
            }
        }
    }

    imageArea = imageArea.getIntersection (getLocalBounds());
    imageX = imageArea.getX();
    imageY = imageArea.getY();

    auto listScale = juce::Component::getApproximateScaleFactorForComponent (this);
    juce::Image snapshot (
        juce::Image::ARGB, juce::roundToInt ((float) imageArea.getWidth() * listScale), juce::roundToInt ((float) imageArea.getHeight() * listScale), true);

    for (int i = getNumRowsOnScreen() + 2; --i >= 0;)
    {
        if (rows.contains (firstRow + i))
        {
            if (auto* rowComp = viewport->getComponentForRowIfOnscreen (firstRow + i))
            {
                juce::Graphics g (snapshot);
                g.setOrigin (getLocalPoint (rowComp, juce::Point<int>()) - imageArea.getPosition());

                auto rowScale = juce::Component::getApproximateScaleFactorForComponent (rowComp);

                if (g.reduceClipRegion (rowComp->getLocalBounds() * rowScale))
                {
                    g.beginTransparencyLayer (0.6f);
                    g.addTransform (juce::AffineTransform::scale (rowScale));
                    rowComp->paintEntireComponent (g, false);
                    g.endTransparencyLayer();
                }
            }
        }
    }

    return snapshot;
}

void VListBox::startDragAndDrop (const juce::MouseEvent& e,
                                const juce::SparseSet<int>& rowsToDrag,
                                const juce::var& dragDescription,
                                bool allowDraggingToOtherWindows)
{
    if (auto* dragContainer = juce::DragAndDropContainer::findParentDragContainerFor (this))
    {
        int x, y;
        auto dragImage = createSnapshotOfRows (rowsToDrag, x, y);

        auto p = juce::Point<int> (x, y) - e.getEventRelativeTo (this).position.toInt();
        dragContainer->startDragging (dragDescription, this, dragImage, allowDraggingToOtherWindows, &p, &e.source);
    }
    else
    {
        // to be able to do a drag-and-drop operation, the listbox needs to
        // be inside a component which is also a juce::DragAndDropContainer.
        jassertfalse;
    }
}

int VListBox::getContentHeight() const
{
    if (model == nullptr || totalItems == 0)
        return 0;

    return getPositionForRow (totalItems - 1) + getRowHeight (totalItems - 1);
}

int VListBox::getRowForPosition (int y) const
{
    if (model == nullptr || y < 0)
        return 0;

    return model->getRowForPosition (y);
}

int VListBox::getPositionForRow (int row) const
{
    if (model == nullptr)
        return 0;

    jassert (row >= 0 && row < totalItems);

    return model->getPositionForRow (row);
}

//==============================================================================
juce::Component* VListBoxModel::refreshComponentForRow (int, bool, juce::Component* existingComponentToUpdate)
{
    ignoreUnused (existingComponentToUpdate);
    jassert (existingComponentToUpdate == nullptr); // indicates a failure in the code that recycles the components
    return nullptr;
}

void VListBoxModel::listBoxItemClicked (int, const juce::MouseEvent&) {}
void VListBoxModel::listBoxItemDoubleClicked (int, const juce::MouseEvent&) {}
void VListBoxModel::backgroundClicked (const juce::MouseEvent&) {}
void VListBoxModel::selectedRowsChanged (int) {}
void VListBoxModel::deleteKeyPressed (int) {}
void VListBoxModel::returnKeyPressed (int) {}
void VListBoxModel::listWasScrolled() {}
juce::var VListBoxModel::getDragSourceDescription (const juce::SparseSet<int>&)
{
    return {};
}
juce::String VListBoxModel::getTooltipForRow (int)
{
    return {};
}
juce::MouseCursor VListBoxModel::getMouseCursorForRow (int)
{
    return juce::MouseCursor::NormalCursor;
}