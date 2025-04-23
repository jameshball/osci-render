/*
  ==============================================================================

    MIT License

    Copyright (c) 2020 Tal Aviram

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to deal
    in the Software without restriction, including without limitation the rights
    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
    copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all
    copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

namespace jux
{
//==============================================================================
/**

    Fancy looking toggable button. commonly used in modern UIs.

    @see juce::ToggableButton

*/
class SwitchButton : public juce::Button, public juce::AudioProcessorParameter::Listener
{
public:
    enum ColourIds
    {
        switchColour = 0x1B06000,
        switchOnBackgroundColour = 0x1B06001,
        switchOffBackgroundColour = 0x1B06002
    };

    SwitchButton (juce::String name, bool isInverted, bool isVertical = false) : Button ("SwitchButton"), isInverted (isInverted), isVertical (isVertical) {
        setClickingTogglesState (true);
        addAndMakeVisible (switchCircle);
        switchCircle.setWantsKeyboardFocus (false);
        switchCircle.setInterceptsMouseClicks (false, false);
    }
    
    SwitchButton(osci::BooleanParameter* parameter) : SwitchButton(parameter->name, false) {
        this->parameter = parameter;
        setToggleState(parameter->getBoolValue(), juce::NotificationType::dontSendNotification);
        parameter->addListener(this);
        onStateChange = [this]() {
            this->parameter->setBoolValueNotifyingHost(getToggleState());
        };
        addAndMakeVisible(label);
        label.setTooltip(parameter->getDescription());
        label.setText(parameter->name, juce::NotificationType::dontSendNotification);
    }
    
    ~SwitchButton() {
        if (parameter != nullptr) {
            parameter->removeListener(this);
        }
    }
    
    void parameterValueChanged(int parameterIndex, float newValue) override {
        juce::WeakReference<SwitchButton> weakThis = this;
        juce::MessageManager::callAsync([weakThis]() {
            if (weakThis != nullptr) {
                weakThis->setToggleState(weakThis->parameter->getBoolValue(), juce::NotificationType::dontSendNotification);
            }
        });
    }
    
    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override {}

    void setMillisecondsToSpendMoving (int newValue)
    {
        millisecondsToSpendMoving = newValue;
    }

    void paintButton (juce::Graphics& g,
                      bool shouldDrawButtonAsHighlighted,
                      bool shouldDrawButtonAsDown) override
    {
        auto b = getSwitchBounds();
        auto cornerSize = (isVertical ? b.getWidth() : b.getHeight()) * 0.5;
        g.setColour (juce::Colours::black.withAlpha (0.1f));
        g.drawRoundedRectangle (b, cornerSize, 2.0f);
        g.setColour (findColour (getSwitchState() ? switchOnBackgroundColour : switchOffBackgroundColour));
        g.fillRoundedRectangle (b, cornerSize);

        juce::Path switchPath;
        switchPath.addRoundedRectangle (b, cornerSize, cornerSize);
        g.fillPath (switchPath);

        if (prevToggleState != getToggleState()) {
            juce::Rectangle<float> switchCircleBounds;
            if (! isVertical)
                switchCircleBounds = { getSwitchState() ? b.getRight() - b.getHeight() : b.getX(), b.getY(), b.getHeight(), b.getHeight() };
            else
                switchCircleBounds = {
                b.getX(),
                getSwitchState() ? b.getBottom() - b.getWidth() : b.getY(),
                b.getWidth(),
                b.getWidth()
            };
            animator.animateComponent (&switchCircle, switchCircleBounds.reduced (1).toNearestInt(), 1.0, millisecondsToSpendMoving, false, 0.5, 0);
        }

        prevToggleState = getToggleState();
    }

    void resized() override
    {
        Button::resized();
        auto b = getSwitchBounds();
        label.setBounds(getLabelBounds());
        
        juce::Rectangle<float> switchCircleBounds;
        if (!isVertical)
            switchCircleBounds = { getSwitchState() ? b.getRight() - b.getHeight() : b.getX(), b.getY(), b.getHeight(), b.getHeight() };
        else
            switchCircleBounds = {
                b.getX(),
                getSwitchState() ? b.getBottom() - b.getWidth() : b.getY(),
                b.getHeight(),
                b.getHeight()
            };
        switchCircle.setBounds (switchCircleBounds.reduced (1).toNearestInt());
    }

private:
    int millisecondsToSpendMoving { 75 };
    juce::Label label;

    bool getSwitchState() const
    {
        return isInverted ? ! getToggleState() : getToggleState();
    }
    bool isInverted = false;
    bool isVertical = false;

    juce::Rectangle<float> getSwitchBounds() {
        return getLocalBounds().removeFromLeft(30).withSizeKeepingCentre(30, 20).toFloat().reduced(4, 4).translated(0, -1);
    }
    
    juce::Rectangle<int> getLabelBounds() {
        auto b = getLocalBounds();
        b.removeFromLeft(34);
        return b.translated(0, -1);
    }

    class SwitchCircle : public Component
    {
        void paint (juce::Graphics& g) override
        {
            g.setColour (findColour (switchColour));
            g.fillEllipse (getLocalBounds().toFloat());
        }
    } switchCircle;
    juce::ComponentAnimator animator;

    bool prevToggleState = false;
    
    osci::BooleanParameter* parameter = nullptr;
    
    JUCE_DECLARE_WEAK_REFERENCEABLE(SwitchButton)
};

} // namespace jux
