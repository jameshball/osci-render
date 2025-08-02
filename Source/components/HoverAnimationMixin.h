#pragma once
#include <JuceHeader.h>

class HoverAnimationMixin
{
public:
    HoverAnimationMixin(juce::Component* targetComponent);
    virtual ~HoverAnimationMixin() = default;

    // Animation control
    void animateHover(bool isHovering);
    
    // Getters
    float getAnimationProgress() const { return animationProgress; }
    bool getIsHovered() const { return isHovered; }
    
    // Mouse event handlers to be called from the component
    void handleMouseEnter();
    void handleMouseExit();
    void handleMouseDown();
    void handleMouseUp(const juce::Point<int>& mousePosition, const juce::Rectangle<int>& componentBounds);

protected:
    // Override this to customize animation parameters
    virtual int getHoverAnimationDurationMs() const { return 200; }
    virtual std::function<float(float)> getEasingFunction() const { return juce::Easings::createEaseOut(); }

private:
    juce::Component* component;
    float animationProgress = 0.0f;
    bool isHovered = false;
    
    // Animation components
    juce::VBlankAnimatorUpdater animatorUpdater;
    juce::Animator hoverAnimator;
    juce::Animator unhoverAnimator;
    
    void setupAnimators();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HoverAnimationMixin)
};
