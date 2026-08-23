#pragma once

#include "TransparentWindow.h"

class VisualiserComponent;

class VisualiserPresentationView final : public osci::OpenGLTextureView {
public:
    explicit VisualiserPresentationView(VisualiserComponent& owner);

    void setPaused(bool paused);
    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    VisualiserComponent& owner;
    bool paused = false;
    bool pauseOnMouseUp = false;
};

class VisualiserWindow final : public TransparentWindow {
public:
    VisualiserWindow(juce::String name, VisualiserComponent& owner, osci::SettingsStore& settings);

    void showPresentation();
    void suspendPresentation();
    void setPresentationPaused(bool paused);
    void setTransparencyEnabled(bool enabled);
    void setPresentationFadeAlpha(float alpha);
    static bool getAlwaysOnTopPreference(const osci::SettingsStore& settings);
    static void setAlwaysOnTopPreference(osci::SettingsStore& settings, bool alwaysOnTop);

protected:
    void closeRequested() override;
    void stateChanged(const TransparentWindowState& state) override;
    juce::Component* getAlphaHitTestComponent() override;
    juce::Point<int> getAlphaMaskSize() const override;
    std::uint64_t getAlphaMaskGeneration() const override;
    bool alphaMaskHasAlphaNear(juce::Point<float> point, juce::Point<float> radius,
                               std::uint8_t threshold) const override;
    void setAlphaMaskCaptureEnabled(bool enabled) override;
    void requestAlphaMaskRefresh() override;
    void refreshOpenGLSurfaceTransparency() override;

private:
    static TransparentWindowState loadWindowState(const osci::SettingsStore& settings);
    static juce::Rectangle<int> constrainSavedBounds(juce::Rectangle<int> bounds);

    VisualiserComponent& owner;
    VisualiserPresentationView* visualiser = nullptr;
    osci::SettingsStore& settings;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualiserWindow)
};
