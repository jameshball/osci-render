#pragma once

#include <JuceHeader.h>

class CommonAudioProcessor;

namespace InternalSampleRateMenu {
// `sentenceCase` for menus written that way (osci-motion's).
void addSubmenu(juce::PopupMenu& menu, CommonAudioProcessor& processor, int baseId, bool sentenceCase = false);
bool handleMenuId(int menuItemId, int baseId, CommonAudioProcessor& processor, std::function<void()> showPremiumSplashScreen);
// The same, the ratio being `current` and set by `choose` (osci-motion keeps
// it with the project).
void addSubmenu(juce::PopupMenu& menu, CommonAudioProcessor& processor, double current, int baseId, bool sentenceCase);
bool handleMenuId(int menuItemId, int baseId, CommonAudioProcessor& processor, std::function<void()> showPremiumSplashScreen, std::function<void(double)> choose);
}
