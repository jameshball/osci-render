#include "EffectTypeItemComponent.h"

EffectTypeItemComponent::EffectTypeItemComponent(const juce::String& name, const juce::String& id)
    : effectName(name), effectId(id),
      hoverAnimator(juce::ValueAnimatorBuilder{}
          .withEasing(juce::Easings::createEaseOut())
          .withDurationMs(200)
          .withValueChangedCallback([this](auto value) {
              animationProgress = static_cast<float>(value);
              repaint();
          })
          .build()),
      unhoverAnimator(juce::ValueAnimatorBuilder{}
          .withEasing(juce::Easings::createEaseOut())
          .withDurationMs(200)
          .withValueChangedCallback([this](auto value) {
              animationProgress = 1.0f - static_cast<float>(value);
              repaint();
          })
          .build())
{
    setupAnimators();
}

EffectTypeItemComponent::~EffectTypeItemComponent() = default;

void EffectTypeItemComponent::setupAnimators()
{
    animatorUpdater.addAnimator(hoverAnimator);
    animatorUpdater.addAnimator(unhoverAnimator);
}

void EffectTypeItemComponent::animateHover(bool isHovering)
{
    if (isHovering)
    {
        unhoverAnimator.complete();
        hoverAnimator.start();
    }
    else
    {
        hoverAnimator.complete();
        unhoverAnimator.start();
    }
}

void EffectTypeItemComponent::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat().reduced(10);
    
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
    juce::Colour normalBgColour = juce::Colour::fromRGB(25, 25, 25);
    juce::Colour hoverBgColour = juce::Colour::fromRGB(40, 40, 40);
    juce::Colour bgColour = normalBgColour.interpolatedWith(hoverBgColour, animationProgress);
        
    g.setColour(bgColour);
    g.fillRoundedRectangle(bounds.toFloat(), CORNER_RADIUS);
    
    // Draw colored outline
    juce::Colour outlineColour = juce::Colour::fromRGB(160, 160, 160);
    g.setColour(outlineColour.withAlpha(0.9f));
    g.drawRoundedRectangle(bounds.toFloat(), CORNER_RADIUS, 1.0f);
    
    // Create areas for text (left) and icon (right)
    auto textArea = bounds.reduced(8, 4);
    auto iconArea = textArea.removeFromRight(20); // Reserve 20px for icon on right
    textArea = textArea.withTrimmedRight(4); // Add small gap between text and icon
    
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions(16.0f, juce::Font::plain));
    g.drawText(effectName, textArea, juce::Justification::centred, true);
    
    // Draw placeholder icon (simple circle with "+" symbol)
    g.setColour(juce::Colours::white.withAlpha(0.7f));
    auto iconSize = juce::jmin(iconArea.getWidth(), iconArea.getHeight()) - 4;
    auto iconBounds = iconArea.withSizeKeepingCentre(iconSize, iconSize);
    g.drawEllipse(iconBounds.toFloat(), 1.5f);
    
    // Draw "+" symbol in the circle
    auto centerX = iconBounds.getCentreX();
    auto centerY = iconBounds.getCentreY();
    auto halfSize = iconSize * 0.25f;
    g.drawLine(centerX - halfSize, centerY, centerX + halfSize, centerY, 2.0f);
    g.drawLine(centerX, centerY - halfSize, centerX, centerY + halfSize, 2.0f);
}

void EffectTypeItemComponent::mouseEnter(const juce::MouseEvent& event)
{
    isHovered = true;
    animateHover(true);
}

void EffectTypeItemComponent::mouseExit(const juce::MouseEvent& event)
{
    isHovered = false;
    animateHover(false);
}

void EffectTypeItemComponent::mouseDown(const juce::MouseEvent& event)
{
    if (onEffectSelected)
        onEffectSelected(effectId);
}
