#pragma once
#include <JuceHeader.h>
#include "../LookAndFeel.h"
#include "HoverAnimationMixin.h"
#include "SvgButton.h"

class EffectTypeItemComponent : public juce::Component
{
public:
    EffectTypeItemComponent(const juce::String& name, const juce::String& icon, const juce::String& id);
    ~EffectTypeItemComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseEnter(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseMove(const juce::MouseEvent& event) override;

    const juce::String& getEffectId() const { return effectId; }
    const juce::String& getEffectName() const { return effectName; }

    std::function<void(const juce::String& effectId)> onEffectSelected;

private:
    juce::String effectName;
    juce::String effectId;
    
    // Hover animation functionality
    std::unique_ptr<HoverAnimationMixin> hoverAnimation;
    
    // Icon for the effect
    std::unique_ptr<SvgButton> iconButton;
    
    static constexpr int CORNER_RADIUS = 8;
    static constexpr float HOVER_LIFT_AMOUNT = 2.0f;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EffectTypeItemComponent)
};
