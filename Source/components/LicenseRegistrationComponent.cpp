#include "LicenseRegistrationComponent.h"

LicenseRegistrationComponent::LicenseRegistrationComponent(CommonAudioProcessor& processor, std::function<void(bool)> onLicenseVerified)
    : audioProcessor(processor), onLicenseVerified(onLicenseVerified)
{
    setupComponents();
    
    // If we have a saved license, validate it in the background
    if (validateSavedLicense())
    {
        // Use Timer to ensure component is properly initialized before hiding
        juce::MessageManager::callAsync([this]() {
            setVisible(false);
        });
        // Start periodic checks every hour
        startTimer(1000 * 60 * 60);
    }
}

LicenseRegistrationComponent::~LicenseRegistrationComponent()
{
    stopTimer();
}

void LicenseRegistrationComponent::setupComponents()
{
    titleLabel.setText("License Registration Required", juce::dontSendNotification);
    titleLabel.setFont(juce::Font(24.0f, juce::Font::bold));
    titleLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(titleLabel);
    
    instructionsLabel.setText("Please enter your license key to continue", juce::dontSendNotification);
    instructionsLabel.setJustificationType(juce::Justification::centred);
    instructionsLabel.setFont(juce::Font(18.0f));
    addAndMakeVisible(instructionsLabel);
    
    licenseKeyEditor.setTextToShowWhenEmpty("XXXXXXXX-XXXXXXXX-XXXXXXXX-XXXXXXXX", juce::Colours::grey);
    licenseKeyEditor.setInputRestrictions(35, "0123456789ABCDEF-"); // Only allow hex digits and hyphens
    licenseKeyEditor.onReturnKey = [this] { verifyButton.triggerClick(); };
    licenseKeyEditor.setFont(juce::Font(24.0f));
    addAndMakeVisible(licenseKeyEditor);
    
    verifyButton.setButtonText("Verify License");
    verifyButton.onClick = [this] {
        if (!isVerifying)
        {
            verifyLicense(licenseKeyEditor.getText());
        }
    };
    addAndMakeVisible(verifyButton);
}

void LicenseRegistrationComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.9f));
    
    auto bounds = getLocalBounds().toFloat();
}

void LicenseRegistrationComponent::resized()
{
    auto bounds = getLocalBounds().reduced(20);
    
    titleLabel.setBounds(bounds.removeFromTop(40));
    bounds.removeFromTop(20);
    
    instructionsLabel.setBounds(bounds.removeFromTop(30));
    bounds.removeFromTop(20);
    
    auto row = bounds.removeFromTop(35);
    licenseKeyEditor.setBounds(row.reduced(50, 0));
    bounds.removeFromTop(20);
    
    verifyButton.setBounds(bounds.removeFromTop(40).withSizeKeepingCentre(120, 40));
}

void LicenseRegistrationComponent::verifyLicense(const juce::String& licenseKey, bool showErrorDialog)
{
    if (licenseKey.isEmpty())
        return;
        
    isVerifying = true;
    verifyButton.setEnabled(false);
    
    juce::URL url("https://api.osci-render.com/api/verify-license");
    
    auto jsonObj = std::make_unique<juce::DynamicObject>();
    jsonObj->setProperty("license_key", licenseKey);
    juce::var jsonData(jsonObj.release());
    
    url = url.withPOSTData(juce::JSON::toString(jsonData));
    
    auto webStream = url.createInputStream(false, nullptr, nullptr,
        "Content-Type: application/json",
        10000);
    
    bool successfullyVerified = false;
        
    if (webStream != nullptr)
    {
        auto response = webStream->readEntireStreamAsString();
        DBG(response);
        auto json = juce::JSON::parse(response);
        
        if (json.hasProperty("success")) {
            bool success = json["success"];
            
            if (success && json.hasProperty("valid") && json.hasProperty("purchase")) {
                bool valid = json["valid"];
                
                auto purchase = json["purchase"].getDynamicObject();
                auto productId = purchase->getProperty("product_id").toString();
                
                if (success && valid && productId == SOSCI_PRODUCT_ID)
                {
                    // Save the license key and validation timestamp
                    audioProcessor.setGlobalValue("license_key", licenseKey);
                    audioProcessor.setGlobalValue("license_last_validated", juce::Time::getCurrentTime().toISO8601(true));
                    audioProcessor.saveGlobalSettings();
                    
                    successfullyVerified = true;
                    
                    setVisible(false);
                    startTimer(1000 * 60 * 60); // Check every hour
                }
                else if (showErrorDialog)
                {
                    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                           "Invalid License",
                                                           "The license key you entered is not valid. Please check and try again.");
                }
                else
                {
                    // Background check failed, clear the license
                    clearLicense();
                }
            } else if (showErrorDialog && json.hasProperty("message")) {
                auto message = json["message"].toString();
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Error", message);
            }
        }
    }
    else if (showErrorDialog)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Connection Error",
            "Could not connect to the license server. Please check your internet connection and try again.");
    }
    
    isVerifying = false;
    verifyButton.setEnabled(true);
    
    if (onLicenseVerified != nullptr) {
        onLicenseVerified(successfullyVerified);
    }
}

bool LicenseRegistrationComponent::validateSavedLicense()
{
    auto savedKey = audioProcessor.getGlobalStringValue("license_key");
    if (savedKey.isNotEmpty())
    {
        auto lastValidated = audioProcessor.getGlobalStringValue("license_last_validated");
        if (lastValidated.isNotEmpty())
        {
            // Verify in the background without showing error dialogs
            verifyLicense(savedKey, false);
            return true;
        }
    }
    return false;
}

void LicenseRegistrationComponent::clearLicense()
{
    audioProcessor.removeGlobalValue("license_key");
    audioProcessor.removeGlobalValue("license_last_validated");
    audioProcessor.saveGlobalSettings();
    setVisible(true);
}

void LicenseRegistrationComponent::timerCallback()
{
    auto savedKey = audioProcessor.getGlobalStringValue("license_key");
    if (savedKey.isNotEmpty()) {
        verifyLicense(savedKey, false);
    }
}
