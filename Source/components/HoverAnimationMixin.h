#pragma once
#include <JuceHeader.h>

// Base Component providing animated hover behavior via JUCE mouse overrides.
class HoverAnimationMixin : public juce::Component
{
public:
    HoverAnimationMixin();
    ~HoverAnimationMixin() override = default;

    // Animation control (available for programmatic triggers if needed)
    void animateHover(bool isHovering);
    
    // Getters
    float getAnimationProgress() const { return animationProgress; }
    bool getIsHovered() const { return isHovered; }

protected:
    // Customization hooks
    virtual int getHoverAnimationDurationMs() const { return 200; }
    virtual std::function<float(float)> getEasingFunction() const { return juce::Easings::createEaseOut(); }

public:
    // juce::Component overrides for mouse events - keep public so children can call base explicitly
    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    float animationProgress = 0.0f;
    bool isHovered = false;
    
    // Animation components
    juce::VBlankAnimatorUpdater animatorUpdater;
    juce::Animator hoverAnimator;
    juce::Animator unhoverAnimator;
    
    void setupAnimators();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HoverAnimationMixin)
};
