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

#pragma once

#include "JuceHeader.h"


//==============================================================================
/**
    A subclass of this is used to drive a VListBox.

    @see VListBox

    @tags{GUI}
*/
class VListBoxModel {
public:
    //==============================================================================
    /** Destructor. */
    virtual ~VListBoxModel() = default;

    //==============================================================================
    /** This has to return the number of items in the list.
        @see VListBox::getNumRows()
    */
    virtual int getNumRows() = 0;

    /** This method must be implemented to draw a row of the list.
        Note that the rowNumber value may be greater than the number of rows in your
        list, so be careful that you don't assume it's less than getNumRows().
    */
    virtual void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) = 0;

    /** This is used to create or update a custom component to go in a row of the list.

        Any row may contain a custom component, or can just be drawn with the paintListBoxItem() method
        and handle mouse clicks with listBoxItemClicked().

        This method will be called whenever a custom component might need to be updated - e.g.
        when the list is changed, or VListBox::updateContent() is called.

        If you don't need a custom component for the specified row, then return nullptr.
        (Bear in mind that even if you're not creating a new component, you may still need to
        delete existingComponentToUpdate if it's non-null).

        If you do want a custom component, and the existingComponentToUpdate is null, then
        this method must create a suitable new component and return it.

        If the existingComponentToUpdate is non-null, it will be a pointer to a component previously created
        by this method. In this case, the method must either update it to make sure it's correctly representing
        the given row (which may be different from the one that the component was created for), or it can
        delete this component and return a new one.

        The component that your method returns will be deleted by the VListBox when it is no longer needed.

        Bear in mind that if you put a custom component inside the row but still want the
        listbox to automatically handle clicking, selection, etc, then you'll need to make sure
        your custom component doesn't intercept all the mouse events that land on it, e.g by
        using Component::setInterceptsMouseClicks().
    */
    virtual juce::Component* refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component* existingComponentToUpdate);

    /** This can be overridden to react to the user clicking on a row.
        @see listBoxItemDoubleClicked
    */
    virtual void listBoxItemClicked(int row, const juce::MouseEvent&);

    /** This can be overridden to react to the user double-clicking on a row.
        @see listBoxItemClicked
    */
    virtual void listBoxItemDoubleClicked(int row, const juce::MouseEvent&);

    /** This can be overridden to react to the user clicking on a part of the list where
        there are no rows.
        @see listBoxItemClicked
    */
    virtual void backgroundClicked(const juce::MouseEvent&);

    /** Override this to be informed when rows are selected or deselected.

        This will be called whenever a row is selected or deselected. If a range of
        rows is selected all at once, this will just be called once for that event.

        @param lastRowSelected      the last row that the user selected. If no
                                    rows are currently selected, this may be -1.
    */
    virtual void selectedRowsChanged(int lastRowSelected);

    /** Override this to be informed when the delete key is pressed.

        If no rows are selected when they press the key, this won't be called.

        @param lastRowSelected   the last row that had been selected when they pressed the
                                    key - if there are multiple selections, this might not be
                                    very useful
    */
    virtual void deleteKeyPressed(int lastRowSelected);

    /** Override this to be informed when the return key is pressed.

        If no rows are selected when they press the key, this won't be called.

        @param lastRowSelected   the last row that had been selected when they pressed the
                                    key - if there are multiple selections, this might not be
                                    very useful
    */
    virtual void returnKeyPressed(int lastRowSelected);

    /** Override this to be informed when the list is scrolled.

        This might be caused by the user moving the scrollbar, or by programmatic changes
        to the list position.
    */
    virtual void listWasScrolled();

    /** To allow rows from your list to be dragged-and-dropped, implement this method.

        If this returns a non-null variant then when the user drags a row, the listbox will
        try to find a DragAndDropContainer in its parent hierarchy, and will use it to trigger
        a drag-and-drop operation, using this string as the source description, with the listbox
        itself as the source component.

        @see DragAndDropContainer::startDragging
    */
    virtual juce::var getDragSourceDescription(const juce::SparseSet<int>& rowsToDescribe);

    /** You can override this to provide tool tips for specific rows.
        @see TooltipClient
    */
    virtual juce::String getTooltipForRow(int row);

    /** You can override this to return a custom mouse cursor for each row. */
    virtual juce::MouseCursor getMouseCursorForRow(int row);

    /**
     * Override this to return the row height for a given row.
     */
    virtual int getRowHeight(int row) { return 22; }

    /**
     * Override this if your list may have variable row heights.
     *
     * Performance is slightly improved if this returns false, but getRowHeight
     * must then return the same number for all rows.
     */
    virtual bool hasVariableHeightRows() const { return false; }

    /**
     * You can override this to improve performance with very long lists.
     *
     * If you have many variable height rows you may be able to improve
     * performance by directly calculating the row for a given y position.
     *
     * If the y position is greater than the number of rows return the number
     * of rows.  yPos will never be less than zero.
     */
    virtual int getRowForPosition(int yPos);

    /**
     * You can override this to improve performance with very long lists.
     *
     * If you have a large number (e.g. thousands of) variable height rows you
     * may be able to improve performance by overriding this function and
     * directly calculating the y position of a given row.
     */
    virtual int getPositionForRow(int rowNumber);
};

//==============================================================================
/**
    A list of items that can be scrolled vertically.

    To create a list, you'll need to create a subclass of VListBoxModel. This can
    either paint each row of the list and respond to events via callbacks, or for
    more specialised tasks, it can supply a custom component to fill each row.

    @see ComboBox, TableListBox

    @tags{GUI}
*/
class JUCE_API VListBox : public juce::Component, public juce::SettableTooltipClient {
public:
    //==============================================================================
    /** Creates a VListBox.

        The model pointer passed-in can be null, in which case you can set it later
        with setModel().
    */
    VListBox(const juce::String& componentName = juce::String(), VListBoxModel* model = nullptr);

    /** Destructor. */
    ~VListBox() override;

    //==============================================================================
    /** Changes the current data model to display. */
    void setModel(VListBoxModel* newModel);

    /** Returns the current list model. */
    VListBoxModel* getModel() const noexcept { return model; }

    //==============================================================================
    /** Causes the list to refresh its content.

        Call this when the number of rows in the list changes, or if you want it
        to call refreshComponentForRow() on all the row components.

        This must only be called from the main message thread.
    */
    void updateContent();

    //==============================================================================
    /** Turns on multiple-selection of rows.

        By default this is disabled.

        When your row component gets clicked you'll need to call the
        selectRowsBasedOnModifierKeys() method to tell the list that it's been
        clicked and to get it to do the appropriate selection based on whether
        the ctrl/shift keys are held down.
    */
    void setMultipleSelectionEnabled(bool shouldBeEnabled) noexcept;

    /** If enabled, this makes the listbox flip the selection status of
        each row that the user clicks, without affecting other selected rows.

        (This only has an effect if multiple selection is also enabled).
        If not enabled, you can still get the same row-flipping behaviour by holding
        down CMD or CTRL when clicking.
    */
    void setClickingTogglesRowSelection(bool flipRowSelection) noexcept;

    /** Sets whether a row should be selected when the mouse is pressed or released.
        By default this is true, but you may want to turn it off.
    */
    void setRowSelectedOnMouseDown(bool isSelectedOnMouseDown) noexcept;

    /** Makes the list react to mouse moves by selecting the row that the mouse if over.

        This function is here primarily for the ComboBox class to use, but might be
        useful for some other purpose too.
    */
    void setMouseMoveSelectsRows(bool shouldSelect);

    //==============================================================================
    /** Selects a row.

        If the row is already selected, this won't do anything.

        @param rowNumber                the row to select
        @param dontScrollToShowThisRow  if true, the list's position won't change; if false and
                                        the selected row is off-screen, it'll scroll to make
                                        sure that row is on-screen
        @param deselectOthersFirst      if true and there are multiple selections, these will
                                        first be deselected before this item is selected
        @see isRowSelected, selectRowsBasedOnModifierKeys, flipRowSelection, deselectRow,
                deselectAllRows, selectRangeOfRows
    */
    void selectRow(int rowNumber, bool dontScrollToShowThisRow = false, bool deselectOthersFirst = true);

    /** Selects a set of rows.

        This will add these rows to the current selection, so you might need to
        clear the current selection first with deselectAllRows()

        @param firstRow                       the first row to select (inclusive)
        @param lastRow                        the last row to select (inclusive)
        @param dontScrollToShowThisRange      if true, the list's position won't change; if false and
                                                the selected range is off-screen, it'll scroll to make
                                                sure that the range of rows is on-screen
    */
    void selectRangeOfRows(int firstRow, int lastRow, bool dontScrollToShowThisRange = false);

    /** Deselects a row.
        If it's not currently selected, this will do nothing.
        @see selectRow, deselectAllRows
    */
    void deselectRow(int rowNumber);

    /** Deselects any currently selected rows.
        @see deselectRow
    */
    void deselectAllRows();

    /** Selects or deselects a row.
        If the row's currently selected, this deselects it, and vice-versa.
    */
    void flipRowSelection(int rowNumber);

    /** Returns a sparse set indicating the rows that are currently selected.
        @see setSelectedRows
    */
    juce::SparseSet<int> getSelectedRows() const;

    /** Sets the rows that should be selected, based on an explicit set of ranges.

        If sendNotificationEventToModel is true, the VListBoxModel::selectedRowsChanged()
        method will be called. If it's false, no notification will be sent to the model.

        @see getSelectedRows
    */
    void setSelectedRows(const juce::SparseSet<int>& setOfRowsToBeSelected, juce::NotificationType sendNotificationEventToModel = juce::sendNotification);

    /** Checks whether a row is selected.
     */
    bool isRowSelected(int rowNumber) const;

    /** Returns the number of rows that are currently selected.
        @see getSelectedRow, isRowSelected, getLastRowSelected
    */
    int getNumSelectedRows() const;

    /** Returns the row number of a selected row.

        This will return the row number of the Nth selected row. The row numbers returned will
        be sorted in order from low to high.

        @param index    the index of the selected row to return, (from 0 to getNumSelectedRows() - 1)
        @returns        the row number, or -1 if the index was out of range or if there aren't any rows
                        selected
        @see getNumSelectedRows, isRowSelected, getLastRowSelected
    */
    int getSelectedRow(int index = 0) const;

    /** Returns the last row that the user selected.

        This isn't the same as the highest row number that is currently selected - if the user
        had multiply-selected rows 10, 5 and then 6 in that order, this would return 6.

        If nothing is selected, it will return -1.
    */
    int getLastRowSelected() const;

    /** Multiply-selects rows based on the modifier keys.

        If no modifier keys are down, this will select the given row and
        deselect any others.

        If the ctrl (or command on the Mac) key is down, it'll flip the
        state of the selected row.

        If the shift key is down, it'll select up to the given row from the
        last row selected.

        @see selectRow
    */
    void selectRowsBasedOnModifierKeys(int rowThatWasClickedOn, juce::ModifierKeys modifiers, bool isMouseUpEvent);

    //==============================================================================
    /** Scrolls the list to a particular position.

        The proportion is between 0 and 1.0, so 0 scrolls to the top of the list,
        1.0 scrolls to the bottom.

        If the total number of rows all fit onto the screen at once, then this
        method won't do anything.

        @see getVerticalPosition
    */
    void setVerticalPosition(double newProportion);

    /** Returns the current vertical position as a proportion of the total.

        This can be used in conjunction with setVerticalPosition() to save and restore
        the list's position. It returns a value in the range 0 to 1.

        @see setVerticalPosition
    */
    double getVerticalPosition() const;

    /** Scrolls if necessary to make sure that a particular row is visible. */
    void scrollToEnsureRowIsOnscreen(int row);

    /** Returns a reference to the vertical scrollbar. */
    juce::ScrollBar& getVerticalScrollBar() const noexcept;

    /** Returns a reference to the horizontal scrollbar. */
    juce::ScrollBar& getHorizontalScrollBar() const noexcept;

    /** Finds the row index that contains a given x,y position.
        The position is relative to the VListBox's top-left.
        If no row exists at this position, the method will return -1.
        @see getComponentForRowNumber
    */
    int getRowContainingPosition(int x, int y) const noexcept;

    /**
     * Returns the height of the specified row.  Returns zero if no model has
     * been set.
     */
    int getRowHeight(int row) const;

    /** Finds a row index that would be the most suitable place to insert a new
        item for a given position.

        This is useful when the user is e.g. dragging and dropping onto the listbox,
        because it lets you easily choose the best position to insert the item that
        they drop, based on where they drop it.

        If the position is out of range, this will return -1. If the position is
        beyond the end of the list, it will return getNumRows() to indicate the end
        of the list.

        @see getComponentForRowNumber
    */
    int getInsertionIndexForPosition(int x, int y) const noexcept;

    /** Returns the position of one of the rows, relative to the top-left of
        the listbox.

        This may be off-screen, and the range of the row number that is passed-in is
        not checked to see if it's a valid row.
    */
    juce::Rectangle<int> getRowPosition(int rowNumber, bool relativeToComponentTopLeft) const noexcept;

    /** Finds the row component for a given row in the list.

        The component returned will have been created using VListBoxModel::refreshComponentForRow().

        If the component for this row is off-screen or if the row is out-of-range,
        this will return nullptr.

        @see getRowContainingPosition
    */
    juce::Component* getComponentForRowNumber(int rowNumber) const noexcept;

    /** Returns the row number that the given component represents.
        If the component isn't one of the list's rows, this will return -1.
    */
    int getRowNumberOfComponent(juce::Component* rowComponent) const noexcept;

    /** Returns the width of a row (which may be less than the width of this component
        if there's a scrollbar).
    */
    int getVisibleRowWidth() const noexcept;

    //==============================================================================

    /** Returns the number of rows actually visible.

        This is the number of whole rows which will fit on-screen, so the value might
        be more than the actual number of rows in the list.
    */
    int getNumRowsOnScreen() const noexcept;

    //==============================================================================
    /** A set of colour IDs to use to change the colour of various aspects of the label.

        These constants can be used either via the Component::setColour(), or LookAndFeel::setColour()
        methods.

        @see Component::setColour, Component::findColour, LookAndFeel::setColour, LookAndFeel::findColour
    */
    enum ColourIds {
        backgroundColourId = 0x1002800, /**< The background colour to fill the list with.
                                                Make this transparent if you don't want the background to be filled. */
        outlineColourId = 0x1002810, /**< An optional colour to use to draw a border around the list.
                                            Make this transparent to not have an outline. */
        textColourId = 0x1002820 /**< The preferred colour to use for drawing text in the listbox. */
    };

    /** Sets the thickness of a border that will be drawn around the box.

        To set the colour of the outline, use @code setColour (VListBox::outlineColourId, colourXYZ); @endcode
        @see outlineColourId
    */
    void setOutlineThickness(int outlineThickness);

    /** Returns the thickness of outline that will be drawn around the listbox.
        @see setOutlineColour
    */
    int getOutlineThickness() const noexcept { return outlineThickness; }

    /** Sets a component that the list should use as a header.

        This will position the given component at the top of the list, maintaining the
        height of the component passed-in, but rescaling it horizontally to match the
        width of the items in the listbox.

        The component will be deleted when setHeaderComponent() is called with a
        different component, or when the listbox is deleted.
    */
    void setHeaderComponent(std::unique_ptr<juce::Component> newHeaderComponent);

    /** Returns whatever header component was set with setHeaderComponent(). */
    juce::Component* getHeaderComponent() const noexcept { return headerComponent.get(); }

    /** Changes the width of the rows in the list.

        This can be used to make the list's row components wider than the list itself - the
        width of the rows will be either the width of the list or this value, whichever is
        greater, and if the rows become wider than the list, a horizontal scrollbar will
        appear.

        The default value for this is 0, which means that the rows will always
        be the same width as the list.
    */
    void setMinimumContentWidth(int newMinimumWidth);

    /** Returns the space currently available for the row items, taking into account
        borders, scrollbars, etc.
    */
    int getVisibleContentWidth() const noexcept;

    /** Repaints one of the rows.

        This does not invoke updateContent(), it just invokes a straightforward repaint
        for the area covered by this row.
    */
    void repaintRow(int rowNumber) noexcept;

    /** This fairly obscure method creates an image that shows the row components specified
        in rows (for example, these could be the currently selected row components).

        It's a handy method for doing drag-and-drop, as it can be passed to the
        DragAndDropContainer for use as the drag image.

        Note that it will make the row components temporarily invisible, so if you're
        using custom components this could affect them if they're sensitive to that
        sort of thing.

        @see Component::createComponentSnapshot
    */
    virtual juce::Image createSnapshotOfRows(const juce::SparseSet<int>& rows, int& x, int& y);

    /** Returns the viewport that this VListBox uses.

        You may need to use this to change parameters such as whether scrollbars
        are shown, etc.
    */
    juce::Viewport* getViewport() const noexcept;

    //==============================================================================
    /** @internal */
    bool keyPressed(const juce::KeyPress&) override;
    /** @internal */
    bool keyStateChanged(bool isKeyDown) override;
    /** @internal */
    void paint(juce::Graphics&) override;
    /** @internal */
    void paintOverChildren(juce::Graphics&) override;
    /** @internal */
    void resized() override;
    /** @internal */
    void visibilityChanged() override;
    /** @internal */
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    /** @internal */
    void mouseUp(const juce::MouseEvent&) override;
    /** @internal */
    void colourChanged() override;
    /** @internal */
    void parentHierarchyChanged() override;
    /** @internal */
    void startDragAndDrop(const juce::MouseEvent&,
        const juce::SparseSet<int>& rowsToDrag,
        const juce::var& dragDescription,
        bool allowDraggingToOtherWindows);

private:
    //==============================================================================
    JUCE_PUBLIC_IN_DLL_BUILD(class ListViewport)
        JUCE_PUBLIC_IN_DLL_BUILD(class RowComponent)
            friend class ListViewport;
        friend class juce::TableListBox;
        VListBoxModel* model;
        std::unique_ptr<ListViewport> viewport;
        std::unique_ptr<juce::Component> headerComponent;
        std::unique_ptr<juce::MouseListener> mouseMoveSelector;
        juce::SparseSet<int> selected;
    int totalItems = 0, minimumRowWidth = 0;
    int outlineThickness = 0;
    int lastRowSelected = -1;
    bool multipleSelection = false, alwaysFlipSelection = false, hasDoneInitialUpdate = false, selectOnMouseDown = true;

    void selectRowInternal(int rowNumber, bool dontScrollToShowThisRow, bool deselectOthersFirst, bool isMouseClick);
    int getContentHeight() const;
    int getRowForPosition(int y) const;
    int getPositionForRow(int row) const;

#if JUCE_CATCH_DEPRECATED_CODE_MISUSE
    // This method's bool parameter has changed: see the new method signature.
    JUCE_DEPRECATED(void setSelectedRows(const juce::SparseSet<int>&, bool));

    // Rows can now have different heights.  See getRowHeight(int rowNumber) instead.
    JUCE_DEPRECATED(void getRowHeight());

    // Row height is now set by the VListBoxModel as rows can have different heights.
    // Implement VListBoxModel::getRowHeight(int rowNumber).
    JUCE_DEPRECATED(void setRowHeight(int));

    // This method has been replaced by the more flexible method createSnapshotOfRows.
    // Please call createSnapshotOfRows (getSelectedRows(), x, y) to get the same behaviour.
    JUCE_DEPRECATED(virtual void createSnapshotOfSelectedRows(int&, int&)) {}
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VListBox)
};