#pragma once

#include <JuceHeader.h>

class CommonAudioProcessor;

namespace InternalSampleRateMenu {
// `sentenceCase` for menus written that way (osci-motion's).
void addSubmenu(juce::PopupMenu& menu, CommonAudioProcessor& processor, int baseId, bool sentenceCase = false);
bool handleMenuId(int menuItemId, int baseId, CommonAudioProcessor& processor, std::function<void()> showPremiumSplashScreen);
}
