#include "GridItemComponent.h"

#include <cmath>

GridItemComponent::TextLayoutMetrics GridItemComponent::computeTextLayouts(int textWidth) const
{
    TextLayoutMetrics metrics;
    metrics.hasDescription = description.isNotEmpty();

    if (metrics.hasDescription)
    {
        juce::AttributedString headingString;
        headingString.setJustification(juce::Justification::topLeft);
        headingString.append(itemName, juce::FontOptions(17.0f, juce::Font::bold), juce::Colours::white);

        metrics.headingLayout.createLayout(headingString, (float) textWidth);
        metrics.headingHeight = metrics.headingLayout.getHeight();

        juce::AttributedString bodyString;
        bodyString.setJustification(juce::Justification::topLeft);
        bodyString.append(description, juce::FontOptions(14.0f), juce::Colours::white.withAlpha(0.85f));

        metrics.bodyLayout.createLayout(bodyString, (float) textWidth);
        metrics.bodyHeight = metrics.bodyLayout.getHeight();

        metrics.totalHeight = metrics.headingHeight + TITLE_DESCRIPTION_SPACING + metrics.bodyHeight;
    }
    else
    {
        juce::Font headingFont { juce::FontOptions(16.0f, juce::Font::plain) };
        metrics.headingHeight = headingFont.getHeight();
        metrics.totalHeight = metrics.headingHeight;
    }

    return metrics;
}

GridItemComponent::GridItemComponent(const juce::String& name, const juce::String& icon, const juce::String& id, juce::Colour iconTint)
    : itemName(name)
    , itemId(id)
    , iconColour(iconTint)
    , lockIcon(
        "gridItemLock",
        juce::String::createStringFromData(BinaryData::lock_svg, BinaryData::lock_svgSize),
        juce::Colours::white.withAlpha(0.9f),
        juce::Colours::white.withAlpha(0.9f))
{
    juce::String iconSvg = icon;
    if (icon.isEmpty()) {
        // Default icon if none is provided
        iconSvg = juce::String::createStringFromData(BinaryData::rotate_svg, BinaryData::rotate_svgSize);
    }
    iconButton = std::make_unique<SvgButton>(
        "gridItemIcon",
        iconSvg,
        iconColour
    );

    // Make the icon non-interactive since this is just a visual element
    iconButton->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*iconButton);

    lockIcon.setClickingTogglesState(false);
    lockIcon.setInterceptsMouseClicks(false, false);
    lockIcon.setVisible(false);
    lockIcon.setEnabled(false);
    addChildComponent(lockIcon);
}

GridItemComponent::~GridItemComponent() = default;

void GridItemComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(10);

    // Get animation progress from inherited HoverAnimationMixin (disabled => no hover)
    auto animationProgress = (interactive && ! locked && isEnabled()) ? getAnimationProgress() : 0.0f;

    // Apply upward shift based on animation progress
    auto yOffset = -animationProgress * HOVER_LIFT_AMOUNT;
    bounds = bounds.translated(0, yOffset);

    // Draw drop shadow
    if (animationProgress > 0.01f) {
        juce::DropShadow shadow;
        shadow.colour = juce::Colours::lime.withAlpha(animationProgress * 0.2f);
        shadow.radius = 15 * animationProgress;
        shadow.offset = juce::Point<int>(0, 4);

        if (shadow.radius > 0) {
            juce::Path shadowPath;
            shadowPath.addRoundedRectangle(bounds.toFloat(), CORNER_RADIUS);
            shadow.drawForPath(g, shadowPath);
        }
    }

    // Draw background with rounded corners - interpolate between normal and hover colors
    juce::Colour normalBgColour = description.isNotEmpty() ? Colours::veryDark.brighter(0.1f)
                                                          : Colours::veryDark;
    juce::Colour hoverBgColour = normalBgColour.brighter(0.05f);
    juce::Colour bgColour = normalBgColour.interpolatedWith(hoverBgColour, animationProgress);

    g.setColour(bgColour);
    g.fillRoundedRectangle(bounds.toFloat(), CORNER_RADIUS);

    // Draw colored outline
    juce::Colour outlineColour = description.isNotEmpty()
        ? Colours::accentColor
        : juce::Colour::fromRGB(160, 160, 160);
    g.setColour(outlineColour.withAlpha(description.isNotEmpty() ? 0.8f : 0.9f));
    g.drawRoundedRectangle(bounds.toFloat(), CORNER_RADIUS, 1.0f);

    // Create text area - now accounting for icon space on the left
    auto textArea = bounds.reduced(10, 8);
    auto iconPadding = description.isNotEmpty() ? 52 : 28;
    textArea.removeFromLeft(iconPadding);

    const auto textLayouts = computeTextLayouts(juce::jmax(1.0f, textArea.getWidth()));

    if (textLayouts.hasDescription)
    {
        juce::Rectangle<float> textBounds = textArea;

        textLayouts.headingLayout.draw(g, textBounds);

        const int headingPixels = (int) std::ceil(textLayouts.headingHeight);
        textBounds.removeFromTop(headingPixels);
        textBounds.removeFromTop(TITLE_DESCRIPTION_SPACING);

        if (textBounds.getHeight() > 0)
            textLayouts.bodyLayout.draw(g, textBounds);
    }
    else
    {
        g.setColour(juce::Colours::white);
        g.setFont(juce::FontOptions(16.0f, juce::Font::plain));
        g.drawText(itemName, textArea, juce::Justification::centred, true);
    }

    // If disabled, draw a dark transparent overlay over the rounded rect to simplify visuals
    if (! isEnabled()) {
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRoundedRectangle(bounds.toFloat(), CORNER_RADIUS);
    }

    if (locked) {
        g.setColour(juce::Colours::black.withAlpha(0.4f));
        g.fillRoundedRectangle(bounds.toFloat(), CORNER_RADIUS);
    }
}

void GridItemComponent::resized()
{
    auto bounds = getLocalBounds().reduced(10);

    // Reserve space for the icon on the left
    auto iconWidth = description.isNotEmpty() ? 56 : 60;
    auto iconArea = bounds.removeFromLeft(iconWidth);
    iconArea = iconArea.withSizeKeepingCentre(40, 40);

    iconButton->setBounds(iconArea);

    const bool canAnimate = interactive && ! locked && isEnabled();
    auto animationProgress = canAnimate ? getAnimationProgress() : 0.0f;
    auto yOffset = -animationProgress * HOVER_LIFT_AMOUNT;

    iconButton->setTransform(canAnimate ? juce::AffineTransform::translation(0, yOffset)
                                        : juce::AffineTransform::identity);

    const int lockSize = 22;
    auto lockBounds = bounds.withSizeKeepingCentre(lockSize, lockSize);
    lockBounds.setX(bounds.getRight() - lockSize - 6);
    lockBounds.setY(bounds.getY() + 12);
    lockIcon.setBounds(lockBounds);
}

void GridItemComponent::mouseDown(const juce::MouseEvent& event)
{
    if (! interactive || ! isEnabled())
        return;
    // Extend base behavior to keep hover press animation
    HoverAnimationMixin::mouseDown(event);
    if (locked) {
        if (onLockedClick) onLockedClick();
        return;
    }
    // Ensure any hover preview is cleared before permanently selecting/enabling the item
    if (onHoverEnd) onHoverEnd();
    if (onItemSelected) {
        onItemSelected(itemId);
    }
}

void GridItemComponent::mouseMove(const juce::MouseEvent& event) {
    juce::ignoreUnused(event);
    if (! interactive || ! isEnabled()) {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    juce::Desktop::getInstance().getMainMouseSource().forceMouseCursorUpdate();
}

void GridItemComponent::mouseEnter(const juce::MouseEvent& event)
{
    if (! interactive || ! isEnabled())
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    HoverAnimationMixin::mouseEnter(event);
    if (locked) {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        return;
    }
    if (isEnabled() && onHoverStart)
        onHoverStart(itemId);
}

void GridItemComponent::mouseExit(const juce::MouseEvent& event)
{
    if (! interactive || ! isEnabled())
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }
    HoverAnimationMixin::mouseExit(event);
    if (! locked && onHoverEnd)
        onHoverEnd();
}

void GridItemComponent::setInteractive(bool shouldBeInteractive)
{
    if (interactive == shouldBeInteractive)
        return;

    interactive = shouldBeInteractive;
    if (! interactive)
        animateHover(false);
    if (! interactive)
        iconButton->setTransform(juce::AffineTransform());

    setMouseCursor(interactive && isEnabled() ? juce::MouseCursor::PointingHandCursor
                                              : juce::MouseCursor::NormalCursor);
}

void GridItemComponent::setDescription(const juce::String& text)
{
    if (description == text)
        return;

    description = text;
    resized();
    repaint();
}

void GridItemComponent::setLocked(bool shouldBeLocked)
{
    if (locked == shouldBeLocked)
        return;

    locked = shouldBeLocked;

    if (locked) {
        animateHover(false);
    }

    lockIcon.setVisible(locked);
    if (locked)
        lockIcon.toFront(false);

    if (! locked)
        setTooltip({});
    
    iconButton->setEnabled(isEnabled() && !locked);
    if (locked || ! isEnabled())
        iconButton->setTransform(juce::AffineTransform());

    repaint();
}

int GridItemComponent::getPreferredHeight(int width) const
{
    const int minimumHeight = 80;
    const int outerPadding = 20; // 10px margin on top and bottom
    const int innerVerticalPadding = 16; // 8px padding inside text area
    const int iconHeight = 40;
    
    const int iconPadding = description.isNotEmpty() ? 52 : 28;
    const int textWidth = juce::jmax(1, width - TEXT_HORIZONTAL_PADDING - iconPadding);
    
    const auto textLayouts = computeTextLayouts(textWidth);
    
    const float contentHeight = juce::jmax(textLayouts.totalHeight, (float) iconHeight);
    const float totalHeight = contentHeight + outerPadding + innerVerticalPadding;
    
    return juce::jmax(minimumHeight, juce::roundToInt(totalHeight + 4.0f));
}
