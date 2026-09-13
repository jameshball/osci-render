#pragma once

#include "InstallerComponent.h"

namespace osci::installer {

class InstallerWindow final : public juce::DocumentWindow {
public:
    explicit InstallerWindow (juce::String name)
        : DocumentWindow (std::move (name), osci::Colours::veryDark(), juce::DocumentWindow::allButtons) {
        setUsingNativeTitleBar (true);
        setResizable (false, false);
#if JUCE_LINUX
        auto* viewport = new juce::Viewport();
        installer = new InstallerComponent();
        viewport->setViewedComponent (installer, true);
        viewport->setScrollBarsShown (true, true);
        auto viewportSize = juce::Point<int> { 780, 520 };
        const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
        if (display != nullptr) {
            viewportSize.x = juce::jmin (viewportSize.x, juce::jmax (480, juce::roundToInt (display->userBounds.getWidth()) - 40));
            viewportSize.y = juce::jmin (viewportSize.y, juce::jmax (420, juce::roundToInt (display->userBounds.getHeight()) - 80));
        }
        viewport->setSize (viewportSize.x, viewportSize.y);
        setContentOwned (viewport, true);
        const auto frameHeight = juce::jmax (0, getHeight() - viewport->getHeight());
        setContentComponentSize (viewportSize.x, viewportSize.y + frameHeight);
        installer->onBusyChanged = [this] (bool busy) {
            if (auto* button = getCloseButton()) {
                button->setEnabled (!busy);
            }
        };
#else
        installer = new InstallerComponent();
        setContentOwned (installer, true);
#endif
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

    bool canClose() const noexcept {
        return installer == nullptr || !installer->installationInProgress();
    }

private:
    InstallerComponent* installer = nullptr;
};

} // namespace osci::installer
