#pragma once

#include <JuceHeader.h>

class CommonAudioProcessor;

namespace InternalSampleRateMenu
{
void addSubmenu (juce::PopupMenu& menu, CommonAudioProcessor& processor, int baseId);
bool handleMenuId (int menuItemId,
                   int baseId,
                   CommonAudioProcessor& processor,
                   std::function<void()> showPremiumSplashScreen);
}
