#pragma once

#include <JuceHeader.h>
#include "../CommonPluginProcessor.h"

class LicenseRegistrationComponent : public juce::Component,
                                   public juce::Timer
{
public:
    LicenseRegistrationComponent(CommonAudioProcessor& processor, std::function<void(bool)> onLicenseVerified);
    ~LicenseRegistrationComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    
    // Timer callback for background license verification
    void timerCallback() override;

private:
    void verifyLicense(const juce::String& licenseKey, bool showErrorDialog = true);
    bool validateSavedLicense();
    void clearLicense();
    void setupComponents();
    
    CommonAudioProcessor& audioProcessor;
    juce::Label titleLabel;
    juce::Label instructionsLabel;
    juce::TextEditor licenseKeyEditor;
    juce::TextButton verifyButton;
    bool isVerifying = false;
    
    const juce::String SOSCI_PRODUCT_ID = "Hsr9C58_YhTxYP0MNvsIow==";
    
    std::function<void(bool)> onLicenseVerified;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LicenseRegistrationComponent)
    JUCE_DECLARE_WEAK_REFERENCEABLE(LicenseRegistrationComponent)
};
