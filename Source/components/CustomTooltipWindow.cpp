#include "CustomTooltipWindow.h"

CustomTooltipWindow::CustomTooltipWindow(int delayMs)
    : Component("tooltip"), millisecondsBeforeTipAppears(delayMs)
{
    setAlwaysOnTop(true);
    setOpaque(true);
    setAccessible(false);

    auto& desktop = juce::Desktop::getInstance();

    if (desktop.getMainMouseSource().canHover()) {
        desktop.addGlobalMouseListener(this);
        startTimer(123);
    }
}

CustomTooltipWindow::~CustomTooltipWindow()
{
    hideTip();
    juce::Desktop::getInstance().removeGlobalMouseListener(this);
}

void CustomTooltipWindow::setMillisecondsBeforeTipAppears(const int newTimeMs) noexcept
{
    millisecondsBeforeTipAppears = newTimeMs;
}

void CustomTooltipWindow::paint(juce::Graphics& g)
{
    getLookAndFeel().drawTooltip(g, tipShowing, getWidth(), getHeight());
}

void CustomTooltipWindow::mouseEnter(const juce::MouseEvent& e)
{
    if (e.eventComponent == this)
        hideTip();
}

void CustomTooltipWindow::mouseDown(const juce::MouseEvent&)
{
    if (isVisible())
        dismissalMouseEventOccurred = true;
}

void CustomTooltipWindow::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&)
{
    if (isVisible())
        dismissalMouseEventOccurred = true;
}

void CustomTooltipWindow::updatePosition(const juce::String& tip, juce::Point<int> pos, juce::Rectangle<int> parentArea)
{
    setBounds(getLookAndFeel().getTooltipBounds(tip, pos, parentArea));
    setVisible(true);
}

void CustomTooltipWindow::displayTip(juce::Point<int> screenPos, const juce::String& tip)
{
    jassert(tip.isNotEmpty());
    displayTipInternal(screenPos, tip, ShownManually::yes);
}

void CustomTooltipWindow::displayTipInternal(juce::Point<int> screenPos, const juce::String& tip, ShownManually shownManually)
{
    if (!reentrant) {
        juce::ScopedValueSetter<bool> setter(reentrant, true, false);

        if (tipShowing != tip) {
            tipShowing = tip;
            repaint();
        }

        // Add to desktop WITHOUT windowHasDropShadow to prevent Windows DirectComposition errors
        addToDesktop(juce::ComponentPeer::windowIsTemporary
                   | juce::ComponentPeer::windowIgnoresKeyPresses
                   | juce::ComponentPeer::windowIgnoresMouseClicks);

        updatePosition(tip, screenPos, juce::Desktop::getInstance().getDisplays().getDisplayForPoint(screenPos)->userArea);

        toFront(false);
        manuallyShownTip = shownManually == ShownManually::yes ? tip : juce::String();
        dismissalMouseEventOccurred = false;
    }
}

juce::String CustomTooltipWindow::getTipFor(juce::Component& c)
{
    // Check if mouse button is down
    if (!juce::ModifierKeys::getCurrentModifiers().isAnyMouseButtonDown()) {
        if (auto* ttc = dynamic_cast<juce::TooltipClient*>(&c))
            if (!c.isCurrentlyBlockedByAnotherModalComponent())
                return ttc->getTooltip();
    }

    return {};
}

void CustomTooltipWindow::hideTip()
{
    if (isVisible() && !reentrant) {
        tipShowing = {};
        manuallyShownTip = {};
        dismissalMouseEventOccurred = false;

        removeFromDesktop();
        setVisible(false);

        lastHideTime = juce::Time::getApproximateMillisecondCounter();
    }
}

float CustomTooltipWindow::getDesktopScaleFactor() const
{
    if (lastComponentUnderMouse != nullptr)
        return Component::getApproximateScaleFactorForComponent(lastComponentUnderMouse);

    return Component::getDesktopScaleFactor();
}

std::unique_ptr<juce::AccessibilityHandler> CustomTooltipWindow::createAccessibilityHandler()
{
    return createIgnoredAccessibilityHandler(*this);
}

void CustomTooltipWindow::timerCallback()
{
    const auto mouseSource = juce::Desktop::getInstance().getMainMouseSource();
    auto* newComp = mouseSource.isTouch() ? nullptr : mouseSource.getComponentUnderMouse();

    if (manuallyShownTip.isNotEmpty()) {
        if (dismissalMouseEventOccurred || newComp == nullptr)
            hideTip();

        return;
    }

    // Always check for tooltips since we're using desktop windows
    {
        const auto newTip = newComp != nullptr ? getTipFor(*newComp) : juce::String();

        const auto mousePos = mouseSource.getScreenPosition();
        const auto mouseMovedQuickly = (mousePos.getDistanceFrom(lastMousePos) > 12);
        lastMousePos = mousePos;

        const auto tipChanged = (newTip != lastTipUnderMouse || newComp != lastComponentUnderMouse);
        const auto now = juce::Time::getApproximateMillisecondCounter();

        lastComponentUnderMouse = newComp;
        lastTipUnderMouse = newTip;

        if (tipChanged || dismissalMouseEventOccurred || mouseMovedQuickly)
            lastCompChangeTime = now;

        const auto showTip = [this, &mouseSource, &mousePos, &newTip] {
            if (mouseSource.getLastMouseDownPosition() != lastMousePos)
                displayTipInternal(mousePos.roundToInt(), newTip, ShownManually::no);
        };

        if (isVisible() || now < lastHideTime + 500) {
            // if a tip is currently visible (or has just disappeared), update to a new one
            // immediately if needed..
            if (newComp == nullptr || dismissalMouseEventOccurred || newTip.isEmpty())
                hideTip();
            else if (tipChanged)
                showTip();
        } else {
            // if there isn't currently a tip, but one is needed, only let it appear after a timeout
            if (newTip.isNotEmpty()
                && newTip != tipShowing
                && now > lastCompChangeTime + (juce::uint32)millisecondsBeforeTipAppears) {
                showTip();
            }
        }
    }
}
