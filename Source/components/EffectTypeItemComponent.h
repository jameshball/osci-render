#pragma once
#include <JuceHeader.h>

class EffectTypeItemComponent : public juce::Component
{
public:
    EffectTypeItemComponent(const juce::String& name, const juce::String& id);
    ~EffectTypeItemComponent() override;

    void paint(juce::Graphics& g) override;
    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;

    const juce::String& getEffectId() const { return effectId; }
    const juce::String& getEffectName() const { return effectName; }

    std::function<void(const juce::String& effectId)> onEffectSelected;

private:
    juce::String effectName;
    juce::String effectId;
    float animationProgress = 0.0f;
    bool isHovered = false;
    
    // Animation components
    juce::VBlankAnimatorUpdater animatorUpdater { this };
    juce::Animator hoverAnimator;
    juce::Animator unhoverAnimator;
    
    static constexpr int CORNER_RADIUS = 8;
    static constexpr float HOVER_LIFT_AMOUNT = 2.0f;
    
    void setupAnimators();
    void animateHover(bool isHovering);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectTypeItemComponent)
};
