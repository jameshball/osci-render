namespace osci::licensing
{

LicenseManager::LicenseManager()
    : LicenseManager (Config {})
{
}

LicenseManager::LicenseManager (Config configToUse)
    : config (std::move (configToUse)), backend (config.backend)
{
    if (config.storageDirectory == juce::File())
        config.storageDirectory = HardwareInfo::getDefaultStorageDirectory (config.productSlug);
}

LicenseManager& LicenseManager::instance()
{
    static LicenseManager manager;
    return manager;
}

LicenseManager::Status LicenseManager::status() const noexcept
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    return currentStatus;
}

bool LicenseManager::hasPremium (const juce::String& featureGroup) const noexcept
{
    juce::ignoreUnused (featureGroup);
    const juce::SpinLock::ScopedLockType lock (stateLock);
    return (currentStatus == Status::PremiumOnline || currentStatus == Status::PremiumCachedToken)
           && cachedPayload.has_value() && cachedPayload->isPremium();
}

juce::Result LicenseManager::loadCachedToken()
{
    const auto tokenFile = getTokenFile();
    if (! tokenFile.existsAsFile())
    {
        deactivate();
        return juce::Result::ok();
    }

    const auto token = tokenFile.loadFileAsString().trim();
    if (token.isEmpty())
    {
        deactivate();
        return juce::Result::ok();
    }

    const auto validation = LicenseToken::validate (token, juce::Time::getCurrentTime(), config.offlineGrace);
    if (validation.result.failed())
    {
        if (auto payload = LicenseToken::inspectUnverified (token))
        {
            const juce::SpinLock::ScopedLockType lock (stateLock);
            cachedToken = token;
            cachedPayload = *payload;
            currentStatus = Status::ExpiredOffline;
        }

        return validation.result;
    }

    setStateFromValidation (validation, token);
    return juce::Result::ok();
}

juce::Result LicenseManager::activate (juce::StringRef licenseKey)
{
    ActivationResponse activation;
    if (auto result = backend.activateLicense (licenseKey, config.productSlug, activation); result.failed())
        return result;

    const auto validation = LicenseToken::validate (activation.token, juce::Time::getCurrentTime(), config.offlineGrace);
    if (validation.result.failed())
        return validation.result;

    if (! config.storageDirectory.createDirectory())
        return juce::Result::fail ("Could not create license storage directory");

    if (! getTokenFile().replaceWithText (activation.token))
        return juce::Result::fail ("Could not write license token");

    setStateFromValidation (validation, activation.token);
    return juce::Result::ok();
}

juce::Result LicenseManager::refreshNow()
{
    juce::String licenseKey;
    {
        const juce::SpinLock::ScopedLockType lock (stateLock);
        if (cachedPayload.has_value())
            licenseKey = cachedPayload->licenseKey;
    }

    if (licenseKey.isEmpty())
    {
        const auto token = getTokenFile().loadFileAsString().trim();
        if (auto payload = LicenseToken::inspectUnverified (token))
            licenseKey = payload->licenseKey;
    }

    if (licenseKey.isEmpty())
        return juce::Result::fail ("No cached license key is available to refresh");

    return activate (licenseKey);
}

void LicenseManager::scheduleBackgroundRefresh()
{
    juce::Thread::launch ([this]
    {
        juce::ignoreUnused (refreshNow());
    });
}

void LicenseManager::deactivate()
{
    getTokenFile().deleteFile();

    const juce::SpinLock::ScopedLockType lock (stateLock);
    currentStatus = Status::Free;
    cachedToken.clear();
    cachedPayload.reset();
}

juce::ValueTree LicenseManager::getStateForUi() const
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    juce::ValueTree state ("license");
    state.setProperty ("status", statusToString (currentStatus), nullptr);
    state.setProperty ("premium", currentStatus == Status::PremiumOnline || currentStatus == Status::PremiumCachedToken, nullptr);

    if (cachedPayload.has_value())
    {
        state.setProperty ("email", cachedPayload->email, nullptr);
        state.setProperty ("tier", cachedPayload->tier, nullptr);
        state.setProperty ("product_id", cachedPayload->productId, nullptr);
        state.setProperty ("expires_at", cachedPayload->expiresAt.toISO8601 (true), nullptr);
    }

    return state;
}

std::optional<LicenseTokenPayload> LicenseManager::getPayload() const
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    return cachedPayload;
}

juce::File LicenseManager::getTokenFile() const
{
    return config.storageDirectory.getChildFile ("license.dat");
}

void LicenseManager::setStateFromValidation (const LicenseTokenValidation& validation, juce::String token)
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    cachedToken = std::move (token);
    cachedPayload = validation.payload;
    currentStatus = validation.payload.isPremium()
        ? (validation.withinOfflineGrace ? Status::PremiumCachedToken : Status::PremiumOnline)
        : Status::Free;
}

juce::String LicenseManager::statusToString (Status status)
{
    switch (status)
    {
        case Status::Free: return "free";
        case Status::PremiumOnline: return "premium_online";
        case Status::PremiumCachedToken: return "premium_cached_token";
        case Status::ExpiredOffline: return "expired_offline";
    }

    return "free";
}

} // namespace osci::licensing
