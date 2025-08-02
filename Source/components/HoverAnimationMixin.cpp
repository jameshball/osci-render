#include "HoverAnimationMixin.h"

HoverAnimationMixin::HoverAnimationMixin(juce::Component* targetComponent)
    : component(targetComponent),
      animatorUpdater(targetComponent),
      hoverAnimator(juce::ValueAnimatorBuilder{}
          .withEasing(getEasingFunction())
          .withDurationMs(getHoverAnimationDurationMs())
          .withValueChangedCallback([this](auto value) {
              animationProgress = static_cast<float>(value);
              if (component != nullptr) {
                  component->repaint();
                  component->resized();
              }
          })
          .build()),
      unhoverAnimator(juce::ValueAnimatorBuilder{}
          .withEasing(getEasingFunction())
          .withDurationMs(getHoverAnimationDurationMs())
          .withValueChangedCallback([this](auto value) {
              animationProgress = 1.0f - static_cast<float>(value);
              if (component != nullptr) {
                  component->repaint();
                  component->resized();
              }
          })
          .build())
{
    setupAnimators();
}

void HoverAnimationMixin::setupAnimators()
{
    animatorUpdater.addAnimator(hoverAnimator);
    animatorUpdater.addAnimator(unhoverAnimator);
}

void HoverAnimationMixin::animateHover(bool isHovering)
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

void HoverAnimationMixin::handleMouseEnter()
{
    isHovered = true;
    animateHover(true);
}

void HoverAnimationMixin::handleMouseExit()
{
    isHovered = false;
    // Fixed logic to prevent getting stuck in hovered state
    animateHover(false);
}

void HoverAnimationMixin::handleMouseDown()
{
    animateHover(false);
}

void HoverAnimationMixin::handleMouseUp(const juce::Point<int>& mousePosition, const juce::Rectangle<int>& componentBounds)
{
    // Only animate hover if the mouse is still within the component bounds
    if (componentBounds.contains(mousePosition))
        animateHover(true);
}
