/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

#include "CustomMidiKeyboardComponent.h"

namespace juce
{

//==============================================================================
CustomMidiKeyboardComponent::CustomMidiKeyboardComponent (MidiKeyboardState& stateToUse, Orientation orientationToUse)
    : KeyboardComponentBase (orientationToUse), state (stateToUse)
{
    state.addListener (this);

    // initialise with a default set of qwerty key-mappings
    const std::string_view keys { "awsedftgyhujkolp;" };

    for (const char& c : keys)
        setKeyPressForNote ({c, 0, 0}, (int) std::distance (keys.data(), &c));

    mouseOverNotes.insertMultiple (0, -1, 32);
    mouseDownNotes.insertMultiple (0, -1, 32);

    colourChanged();
    setWantsKeyboardFocus (true);
    setOpaque (false);

    startTimerHz (20);
}

CustomMidiKeyboardComponent::~CustomMidiKeyboardComponent()
{
    state.removeListener (this);
}

//==============================================================================
void CustomMidiKeyboardComponent::setVelocity (float v, bool useMousePosition)
{
    velocity = v;
    useMousePositionForVelocity = useMousePosition;
}

//==============================================================================
void CustomMidiKeyboardComponent::setMidiChannel (int midiChannelNumber)
{
    jassert (midiChannelNumber > 0 && midiChannelNumber <= 16);

    if (midiChannel != midiChannelNumber)
    {
        resetAnyKeysInUse();
        midiChannel = jlimit (1, 16, midiChannelNumber);
    }
}

void CustomMidiKeyboardComponent::setMidiChannelsToDisplay (int midiChannelMask)
{
    midiInChannelMask = midiChannelMask;
    noPendingUpdates.store (false);
}

//==============================================================================
void CustomMidiKeyboardComponent::clearKeyMappings()
{
    resetAnyKeysInUse();
    keyPressNotes.clear();
    keyPresses.clear();
}

void CustomMidiKeyboardComponent::setKeyPressForNote (const KeyPress& key, int midiNoteOffsetFromC)
{
    removeKeyPressForNote (midiNoteOffsetFromC);

    keyPressNotes.add (midiNoteOffsetFromC);
    keyPresses.add (key);
}

void CustomMidiKeyboardComponent::removeKeyPressForNote (int midiNoteOffsetFromC)
{
    for (int i = keyPressNotes.size(); --i >= 0;)
    {
        if (keyPressNotes.getUnchecked (i) == midiNoteOffsetFromC)
        {
            keyPressNotes.remove (i);
            keyPresses.remove (i);
        }
    }
}

void CustomMidiKeyboardComponent::setKeyPressBaseOctave (int newOctaveNumber)
{
    jassert (newOctaveNumber >= 0 && newOctaveNumber <= 10);

    keyMappingOctave = newOctaveNumber;
}

//==============================================================================
void CustomMidiKeyboardComponent::resetAnyKeysInUse()
{
    if (! keysPressed.isZero())
    {
        for (int i = 128; --i >= 0;)
            if (keysPressed[i])
                state.noteOff (midiChannel, i, 0.0f);

        keysPressed.clear();
    }

    for (int i = mouseDownNotes.size(); --i >= 0;)
    {
        auto noteDown = mouseDownNotes.getUnchecked (i);

        if (noteDown >= 0)
        {
            state.noteOff (midiChannel, noteDown, 0.0f);
            mouseDownNotes.set (i, -1);
        }

        mouseOverNotes.set (i, -1);
    }
}

void CustomMidiKeyboardComponent::updateNoteUnderMouse (const MouseEvent& e, bool isDown)
{
    updateNoteUnderMouse (e.getEventRelativeTo (this).position, isDown, e.source.getIndex());
}

void CustomMidiKeyboardComponent::updateNoteUnderMouse (Point<float> pos, bool isDown, int fingerNum)
{
    const auto noteInfo = getNoteAndVelocityAtPosition (pos);
    const auto newNote = noteInfo.note;
    const auto oldNote = mouseOverNotes.getUnchecked (fingerNum);
    const auto oldNoteDown = mouseDownNotes.getUnchecked (fingerNum);
    const auto eventVelocity = useMousePositionForVelocity ? noteInfo.velocity * velocity : velocity;

    if (oldNote != newNote)
    {
        repaintNote (oldNote);
        repaintNote (newNote);
        mouseOverNotes.set (fingerNum, newNote);
    }

    if (isDown)
    {
        if (newNote != oldNoteDown)
        {
            if (oldNoteDown >= 0)
            {
                mouseDownNotes.set (fingerNum, -1);

                if (! mouseDownNotes.contains (oldNoteDown))
                    state.noteOff (midiChannel, oldNoteDown, eventVelocity);
            }

            if (newNote >= 0 && ! mouseDownNotes.contains (newNote))
            {
                state.noteOn (midiChannel, newNote, eventVelocity);
                mouseDownNotes.set (fingerNum, newNote);
            }
        }
    }
    else if (oldNoteDown >= 0)
    {
        mouseDownNotes.set (fingerNum, -1);

        if (! mouseDownNotes.contains (oldNoteDown))
            state.noteOff (midiChannel, oldNoteDown, eventVelocity);
    }
}

void CustomMidiKeyboardComponent::repaintNote (int noteNum)
{
    if (getRangeStart() <= noteNum && noteNum <= getRangeEnd())
        repaint (getRectangleForKey (noteNum).getSmallestIntegerContainer());
}


void CustomMidiKeyboardComponent::mouseMove (const MouseEvent& e)
{
    updateNoteUnderMouse (e, false);
}

void CustomMidiKeyboardComponent::mouseDrag (const MouseEvent& e)
{
    auto newNote = getNoteAndVelocityAtPosition (e.position).note;

    if (newNote >= 0 && mouseDraggedToKey (newNote, e))
        updateNoteUnderMouse (e, true);
}

void CustomMidiKeyboardComponent::mouseDown (const MouseEvent& e)
{
    auto newNote = getNoteAndVelocityAtPosition (e.position).note;

    if (newNote >= 0 && mouseDownOnKey (newNote, e))
        updateNoteUnderMouse (e, true);
}

void CustomMidiKeyboardComponent::mouseUp (const MouseEvent& e)
{
    updateNoteUnderMouse (e, false);

    auto note = getNoteAndVelocityAtPosition (e.position).note;

    if (note >= 0)
        mouseUpOnKey (note, e);
}

void CustomMidiKeyboardComponent::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& wheel)
{
    if (auto* viewport = findParentComponentOfClass<Viewport>())
    {
        const auto dominantDelta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;

        if (std::abs (dominantDelta) > 0.0f)
        {
            const auto pixelDelta = roundToInt (-dominantDelta * (wheel.isSmooth ? 180.0f : 72.0f));
            viewport->setViewPosition (viewport->getViewPositionX() + pixelDelta, viewport->getViewPositionY());
            return;
        }
    }

    Component::mouseWheelMove (e, wheel);
}

void CustomMidiKeyboardComponent::mouseEnter (const MouseEvent& e)
{
    updateNoteUnderMouse (e, false);
}

void CustomMidiKeyboardComponent::mouseExit (const MouseEvent& e)
{
    updateNoteUnderMouse (e, false);
}

void CustomMidiKeyboardComponent::timerCallback()
{
    if (noPendingUpdates.exchange (true))
        return;

    for (auto i = getRangeStart(); i <= getRangeEnd(); ++i)
    {
        const auto isOn = state.isNoteOnForChannels (midiInChannelMask, i);

        if (keysCurrentlyDrawnDown[i] != isOn)
        {
            keysCurrentlyDrawnDown.setBit (i, isOn);
            repaintNote (i);
        }
    }
}

bool CustomMidiKeyboardComponent::keyStateChanged (bool /*isKeyDown*/)
{
    bool keyPressUsed = false;

    for (int i = keyPresses.size(); --i >= 0;)
    {
        auto note = 12 * keyMappingOctave + keyPressNotes.getUnchecked (i);

        if (keyPresses.getReference (i).isCurrentlyDown())
        {
            if (! keysPressed[note])
            {
                keysPressed.setBit (note);
                state.noteOn (midiChannel, note, velocity);
                keyPressUsed = true;
            }
        }
        else
        {
            if (keysPressed[note])
            {
                keysPressed.clearBit (note);
                state.noteOff (midiChannel, note, 0.0f);
                keyPressUsed = true;
            }
        }
    }

    return keyPressUsed;
}

bool CustomMidiKeyboardComponent::keyPressed (const KeyPress& key)
{
    return keyPresses.contains (key);
}

void CustomMidiKeyboardComponent::focusLost (FocusChangeType)
{
    resetAnyKeysInUse();
}

//==============================================================================
void CustomMidiKeyboardComponent::drawKeyboardBackground (Graphics& g, Rectangle<float> /*area*/)
{
    juce::ignoreUnused (g);
}

void CustomMidiKeyboardComponent::drawWhiteNote (int midiNoteNumber, Graphics& g, Rectangle<float> area,
                                                 bool isDown, bool isOver, Colour /*lineColour*/, Colour /*textColour*/)
{
    auto c = findColour (whiteNoteColourId);
    if (isDown)       c = c.overlaidWith (findColour (keyDownOverlayColourId));
    else if (isOver)  c = c.overlaidWith (findColour (mouseOverKeyOverlayColourId));

    constexpr float cornerRadius = 2.5f;
    constexpr float gap = 0.5f;

    // Inset horizontally to expose background as 1px separator lines
    auto keyArea = area.reduced (gap, 0.0f);

    const auto isFirstKey = midiNoteNumber == getRangeStart();
    const auto isLastKey = midiNoteNumber == getRangeEnd();

    // Outer keys inherit the panel's rounded corners so the whole keyboard reads as one control.
    Path keyPath;
    keyPath.addRoundedRectangle (keyArea.getX(), keyArea.getY(),
                                  keyArea.getWidth(), keyArea.getHeight(),
                                  cornerRadius, cornerRadius,
                                  isFirstKey, isLastKey, true, true);
    g.setColour (c);
    g.fillPath (keyPath);
}

void CustomMidiKeyboardComponent::drawBlackNote (int midiNoteNumber, Graphics& g, Rectangle<float> area,
                                                 bool isDown, bool isOver, Colour noteFillColour)
{
    auto c = noteFillColour;
    if (isDown)       c = c.overlaidWith (findColour (keyDownOverlayColourId));
    else if (isOver)  c = c.overlaidWith (findColour (mouseOverKeyOverlayColourId));

    constexpr float cornerRadius = 2.5f;

    const auto isFirstKey = midiNoteNumber == getRangeStart();
    const auto isLastKey = midiNoteNumber == getRangeEnd();

    // Preserve the outer rounded corners at the keyboard edges.
    Path keyPath;
    keyPath.addRoundedRectangle (area.getX(), area.getY(),
                                  area.getWidth(), area.getHeight(),
                                  cornerRadius, cornerRadius,
                                  isFirstKey, isLastKey, true, true);
    g.setColour (c);
    g.fillPath (keyPath);

    // Faint lighter bevel/highlight on top edge
    if (! isDown)
    {
        g.setColour (Colours::white.withAlpha (0.07f));
        g.fillRect (area.withHeight (1.0f));
    }
}

String CustomMidiKeyboardComponent::getWhiteNoteText (int /*midiNoteNumber*/)
{
    return {};
}

void CustomMidiKeyboardComponent::colourChanged()
{
    setOpaque (false);
    repaint();
}

//==============================================================================
void CustomMidiKeyboardComponent::drawWhiteKey (int midiNoteNumber, Graphics& g, Rectangle<float> area)
{
    drawWhiteNote (midiNoteNumber, g, area, state.isNoteOnForChannels (midiInChannelMask, midiNoteNumber),
                   mouseOverNotes.contains (midiNoteNumber), findColour (keySeparatorLineColourId), findColour (textLabelColourId));
}

void CustomMidiKeyboardComponent::drawBlackKey (int midiNoteNumber, Graphics& g, Rectangle<float> area)
{
    drawBlackNote (midiNoteNumber, g, area, state.isNoteOnForChannels (midiInChannelMask, midiNoteNumber),
                   mouseOverNotes.contains (midiNoteNumber), findColour (blackNoteColourId));
}

//==============================================================================
void CustomMidiKeyboardComponent::handleNoteOn (MidiKeyboardState*, int /*midiChannel*/, int /*midiNoteNumber*/, float /*velocity*/)
{
    noPendingUpdates.store (false);
}

void CustomMidiKeyboardComponent::handleNoteOff (MidiKeyboardState*, int /*midiChannel*/, int /*midiNoteNumber*/, float /*velocity*/)
{
    noPendingUpdates.store (false);
}

//==============================================================================
bool CustomMidiKeyboardComponent::mouseDownOnKey    ([[maybe_unused]] int midiNoteNumber, [[maybe_unused]] const MouseEvent& e)  { return true; }
bool CustomMidiKeyboardComponent::mouseDraggedToKey ([[maybe_unused]] int midiNoteNumber, [[maybe_unused]] const MouseEvent& e)  { return true; }
void CustomMidiKeyboardComponent::mouseUpOnKey      ([[maybe_unused]] int midiNoteNumber, [[maybe_unused]] const MouseEvent& e)  {}

//==============================================================================
bool CustomMidiKeyboardComponent::isWhiteMidiKey(int midiNote)
{
    switch (midiNote % 12)
    {
        case 1: case 3: case 6: case 8: case 10:
            return false;
        default:
            return true;
    }
}

int CustomMidiKeyboardComponent::getWhiteKeyCount(int startNote, int endNote)
{
    int count = 0;
    for (int note = startNote; note <= endNote; ++note)
        count += isWhiteMidiKey(note) ? 1 : 0;
    return count;
}

} // namespace juce
