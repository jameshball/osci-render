#include "EffectTypeItemComponent.h"

EffectTypeItemComponent::EffectTypeItemComponent(const juce::String& name, const juce::String& icon, const juce::String& id)
    : effectName(name), effectId(id),
      hoverAnimation(std::make_unique<HoverAnimationMixin>(this))
{
    juce::String iconSvg = icon;
    if (icon.isEmpty()) {
        // Default icon if none is provided
        iconSvg = juce::String::createStringFromData(BinaryData::rotate_svg, BinaryData::rotate_svgSize);
    }
    iconButton = std::make_unique<SvgButton>(
        "effectIcon",
        iconSvg,
        juce::Colours::white.withAlpha(0.7f)
    );
    
    // Make the icon non-interactive since this is just a visual element
    iconButton->setInterceptsMouseClicks(false, false);
    addAndMakeVisible(*iconButton);
}

EffectTypeItemComponent::~EffectTypeItemComponent() = default;

void EffectTypeItemComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(10);
    
    // Get animation progress from the hover animation mixin
    auto animationProgress = hoverAnimation->getAnimationProgress();
     
    // Apply upward shift based on animation progress
    auto yOffset = -animationProgress * HOVER_LIFT_AMOUNT;
    bounds = bounds.translated(0, yOffset);
    
    // Draw drop shadow
    if (animationProgress > 0.01f) {
        juce::DropShadow shadow;
        shadow.colour = juce::Colours::lime.withAlpha(animationProgress * 0.2f);
        shadow.radius = 15 * animationProgress;
        shadow.offset = juce::Point<int>(0, 4);
        
        juce::Path shadowPath;
        shadowPath.addRoundedRectangle(bounds.toFloat(), CORNER_RADIUS);
        shadow.drawForPath(g, shadowPath);
        
    }
    
    // Draw background with rounded corners - interpolate between normal and hover colors
    juce::Colour normalBgColour = Colours::veryDark;
    juce::Colour hoverBgColour = normalBgColour.brighter(0.05f);
    juce::Colour bgColour = normalBgColour.interpolatedWith(hoverBgColour, animationProgress);
        
    g.setColour(bgColour);
    g.fillRoundedRectangle(bounds.toFloat(), CORNER_RADIUS);
    
    // Draw colored outline
    juce::Colour outlineColour = juce::Colour::fromRGB(160, 160, 160);
    g.setColour(outlineColour.withAlpha(0.9f));
    g.drawRoundedRectangle(bounds.toFloat(), CORNER_RADIUS, 1.0f);
    
    // Create text area - now accounting for icon space on the left
    auto textArea = bounds.reduced(8, 4);
    textArea.removeFromLeft(28); // Remove space for icon (24px + 4px gap)
    
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(16.0f, juce::Font::plain));
    g.drawText(effectName, textArea, juce::Justification::centred, true);
}

void EffectTypeItemComponent::resized()
{
    auto bounds = getLocalBounds().reduced(10);
    
    // Reserve space for the icon on the left
    auto iconArea = bounds.removeFromLeft(60); // 24px for icon
    iconArea = iconArea.withSizeKeepingCentre(40, 40); // Make icon 20x20px

    iconButton->setBounds(iconArea);
    
    // Get animation progress and calculate Y offset
    auto animationProgress = hoverAnimation->getAnimationProgress();
    auto yOffset = -animationProgress * HOVER_LIFT_AMOUNT;
    
    iconButton->setTransform(juce::AffineTransform::translation(0, yOffset));
}

void EffectTypeItemComponent::mouseEnter(const juce::MouseEvent& event)
{
    hoverAnimation->handleMouseEnter();
}

void EffectTypeItemComponent::mouseExit(const juce::MouseEvent& event)
{
    hoverAnimation->handleMouseExit();
}

void EffectTypeItemComponent::mouseDown(const juce::MouseEvent& event)
{
    if (onEffectSelected)
        onEffectSelected(effectId);
    hoverAnimation->handleMouseDown();
}

void EffectTypeItemComponent::mouseUp(const juce::MouseEvent& event)
{
    hoverAnimation->handleMouseUp(event.getPosition(), getLocalBounds());
}

void EffectTypeItemComponent::mouseMove(const juce::MouseEvent& event) {
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    juce::Desktop::getInstance().getMainMouseSource().forceMouseCursorUpdate();
}
