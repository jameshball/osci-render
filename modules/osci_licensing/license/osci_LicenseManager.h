#pragma once

namespace osci::licensing
{

class LicenseManager
{
public:
    enum class Status
    {
        Free,
        PremiumOnline,
        PremiumCachedToken,
        ExpiredOffline,
    };

    struct Config
    {
        juce::String productSlug = "osci-render";
        BackendClientConfig backend;
        juce::File storageDirectory;
        juce::RelativeTime offlineGrace = juce::RelativeTime::days (14.0);
    };

    LicenseManager();
    explicit LicenseManager (Config config);

    static LicenseManager& instance();

    Status status() const noexcept;
    bool hasPremium (const juce::String& featureGroup = {}) const noexcept;

    juce::Result loadCachedToken();
    juce::Result activate (juce::StringRef licenseKey);
    juce::Result refreshNow();
    void scheduleBackgroundRefresh();
    void deactivate();

    juce::ValueTree getStateForUi() const;
    std::optional<LicenseTokenPayload> getPayload() const;

    juce::File getTokenFile() const;

private:
    Config config;
    BackendClient backend;

    mutable juce::SpinLock stateLock;
    Status currentStatus = Status::Free;
    juce::String cachedToken;
    std::optional<LicenseTokenPayload> cachedPayload;

    void setStateFromValidation (const LicenseTokenValidation& validation, juce::String token);
    static juce::String statusToString (Status status);
};

} // namespace osci::licensing
