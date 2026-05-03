#include "InternalSampleRateMenu.h"

#include "../../CommonPluginProcessor.h"

#include <cmath>

namespace
{
constexpr double ratioEpsilon = 0.000001;

juce::String getRatioLabel (double ratio)
{
    if (ratio < 1.0)
        return "1/" + juce::String (juce::roundToInt (1.0 / ratio)) + " x";

    return juce::String (juce::roundToInt (ratio)) + " x";
}

bool getRatioForMenuId (int menuItemId, int baseId, double& selectedRatio) noexcept
{
    const auto& ratios = osci::IntegerRatioSampleRateAdapter::getSupportedRatios();
    const auto index = menuItemId - baseId;

    if (index < 0 || index >= (int) ratios.size())
        return false;

    selectedRatio = ratios[(size_t) index];
    return true;
}
}

namespace InternalSampleRateMenu
{
void addSubmenu (juce::PopupMenu& menu, CommonAudioProcessor& processor, int baseId)
{
    juce::PopupMenu sub;
    const auto currentRatio = processor.getInternalSampleRateRatio();
    const auto& ratios = osci::IntegerRatioSampleRateAdapter::getSupportedRatios();

    for (size_t i = 0; i < ratios.size(); ++i)
    {
        const auto optionRatio = ratios[i];
        sub.addItem (baseId + (int) i,
                     getRatioLabel (optionRatio),
#if OSCI_PREMIUM
                     processor.canSetInternalSampleRateRatio (optionRatio),
#else
                     true,
#endif
                     std::abs (optionRatio - currentRatio) < ratioEpsilon);
    }

    menu.addSubMenu ("Internal Sample Rate", sub);
}

bool handleMenuId (int menuItemId,
                   int baseId,
                   CommonAudioProcessor& processor,
                   std::function<void()> showPremiumSplashScreen)
{
    double ratio = 1.0;
    if (! getRatioForMenuId (menuItemId, baseId, ratio))
        return false;

#if !OSCI_PREMIUM
    if (std::abs (ratio - 1.0) >= ratioEpsilon)
    {
        if (showPremiumSplashScreen != nullptr)
            showPremiumSplashScreen();

        return true;
    }
#endif

    if (! processor.canSetInternalSampleRateRatio (ratio))
        return false;

    processor.setInternalSampleRateRatio (ratio);
    return true;
}
}
