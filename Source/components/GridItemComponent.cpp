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
    , selectAnimator(juce::ValueAnimatorBuilder{}
          .withEasing(juce::Easings::createEaseOut())
          .withDurationMs(150)
          .withValueChangedCallback([this](auto value) {
              selectionProgress = static_cast<float>(value);
              repaint();
              resized();
          })
          .build())
    , deselectAnimator(juce::ValueAnimatorBuilder{}
          .withEasing(juce::Easings::createEaseOut())
          .withDurationMs(150)
          .withValueChangedCallback([this](auto value) {
              selectionProgress = 1.0f - static_cast<float>(value);
              repaint();
              resized();
          })
          .build())
{
    selectionAnimatorUpdater.addAnimator(selectAnimator);
    selectionAnimatorUpdater.addAnimator(deselectAnimator);

    iconSvgSource = icon;
    if (iconSvgSource.isEmpty()) {
        iconSvgSource = juce::String::createStringFromData(BinaryData::rotate_svg, BinaryData::rotate_svgSize);
    }
    iconButton = std::make_unique<SvgButton>(
        "gridItemIcon",
        iconSvgSource,
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
    // Interpolate card inset based on selection animation progress
    float selReduction = 12.0f - selectionProgress * 4.0f;
    auto bounds = getLocalBounds().toFloat().reduced(selReduction);

    // Get animation progress from inherited HoverAnimationMixin (disabled => no hover)
    auto animationProgress = (interactive && ! locked && isEnabled()) ? getAnimationProgress() : 0.0f;

    // Apply upward shift based on animation progress
    auto yOffset = -animationProgress * HOVER_LIFT_AMOUNT;
    bounds = bounds.translated(0, yOffset);

    // Draw drop shadow
    if (animationProgress > 0.01f || selected) {
        juce::DropShadow shadow;
        float shadowAlpha = selected ? juce::jmax(0.15f, animationProgress * 0.2f) : animationProgress * 0.2f;
        shadow.colour = juce::Colours::lime.withAlpha(shadowAlpha);
        shadow.radius = selected ? juce::jmax(10, (int)(15 * animationProgress)) : (int)(15 * animationProgress);
        shadow.offset = juce::Point<int>(0, 4);

        if (shadow.radius > 0) {
            juce::Path shadowPath;
            shadowPath.addRoundedRectangle(bounds.toFloat(), CORNER_RADIUS);
            shadow.drawForPath(g, shadowPath);
        }
    }

    // Draw background with rounded corners - interpolate between normal and hover colors
    juce::Colour normalBgColour = description.isNotEmpty() ? Colours::veryDark().brighter(0.1f)
                                                          : Colours::veryDark();
    juce::Colour hoverBgColour = normalBgColour.brighter(0.05f);
    juce::Colour bgColour = normalBgColour.interpolatedWith(hoverBgColour, animationProgress);

    // Slightly brighter background for selected items
    if (selected)
        bgColour = bgColour.brighter(0.06f);

    g.setColour(bgColour);
    g.fillRoundedRectangle(bounds.toFloat(), CORNER_RADIUS);

    // Draw colored outline
    juce::Colour outlineColour;
    float outlineThickness = 1.0f;

    if (selected) {
        outlineColour = Colours::accentColor();
        outlineThickness = 2.0f;
    } else if (description.isNotEmpty()) {
        outlineColour = juce::Colour::fromRGB(160, 160, 160);
    } else {
        outlineColour = juce::Colour::fromRGB(160, 160, 160);
    }

    float outlineAlpha = selected ? 1.0f : 0.3f;
    g.setColour(outlineColour.withAlpha(outlineAlpha));
    g.drawRoundedRectangle(bounds.toFloat(), CORNER_RADIUS, outlineThickness);

    // Draw "Recommended" badge
    if (recommended) {
        auto badgeFont = juce::Font(11.0f, juce::Font::bold);
        juce::String badgeText = "Recommended";
        auto badgeWidth = (float)(badgeFont.getStringWidth(badgeText) + 14);
        auto badgeHeight = 18.0f;
        auto badgeBounds = juce::Rectangle<float>(
            bounds.getRight() - badgeWidth - 8.0f,
            bounds.getY() + 6.0f,
            badgeWidth, badgeHeight);

        g.setColour(Colours::accentColor());
        g.fillRoundedRectangle(badgeBounds, 4.0f);

        g.setColour(Colours::veryDark());
        g.setFont(badgeFont);
        g.drawText(badgeText, badgeBounds, juce::Justification::centred, false);
    }

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
    float resizeReduction = 12.0f - selectionProgress * 4.0f;
    auto bounds = getLocalBounds().reduced((int)resizeReduction);

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

void GridItemComponent::setSelected(bool shouldBeSelected)
{
    if (selected == shouldBeSelected)
        return;

    selected = shouldBeSelected;
    updateIconColour(selected ? Colours::accentColor() : juce::Colours::white.withAlpha(0.7f));
    if (selected) {
        deselectAnimator.complete();
        selectAnimator.start();
    } else {
        selectAnimator.complete();
        deselectAnimator.start();
    }
}

void GridItemComponent::updateIconColour(juce::Colour colour)
{
    auto prevBounds = iconButton->getBounds();
    iconButton.reset();
    iconButton = std::make_unique<SvgButton>("gridItemIcon", iconSvgSource, colour);
    iconButton->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*iconButton);
    iconButton->setBounds(prevBounds);
}

void GridItemComponent::setRecommended(bool shouldBeRecommended)
{
    if (recommended == shouldBeRecommended)
        return;

    recommended = shouldBeRecommended;
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
