#include "HoverAnimationMixin.h"

HoverAnimationMixin::HoverAnimationMixin()
    : animatorUpdater(this),
      hoverAnimator(juce::ValueAnimatorBuilder{}
          .withEasing(getEasingFunction())
          .withDurationMs(getHoverAnimationDurationMs())
          .withValueChangedCallback([this](auto value) {
              animationProgress = static_cast<float>(value);
              repaint();
              resized();
          })
          .build()),
      unhoverAnimator(juce::ValueAnimatorBuilder{}
          .withEasing(getEasingFunction())
          .withDurationMs(getHoverAnimationDurationMs())
          .withValueChangedCallback([this](auto value) {
              animationProgress = 1.0f - static_cast<float>(value);
              repaint();
              resized();
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

void HoverAnimationMixin::mouseEnter(const juce::MouseEvent&)
{
    isHovered = true;
    animateHover(true);
}

void HoverAnimationMixin::mouseExit(const juce::MouseEvent&)
{
    isHovered = false;
    // Fixed logic to prevent getting stuck in hovered state
    animateHover(false);
}

void HoverAnimationMixin::mouseDown(const juce::MouseEvent&)
{
    // Keep hover animation active during drag
}

void HoverAnimationMixin::mouseUp(const juce::MouseEvent& event)
{
    // Un-hover only if the mouse left during the drag
    if (!getLocalBounds().contains(event.getEventRelativeTo(this).getPosition()))
        animateHover(false);
}
