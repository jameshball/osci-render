namespace osci::licensing
{

UpdateChecker::UpdateChecker (BackendClient client)
    : backend (std::move (client))
{
}

std::optional<VersionInfo> UpdateChecker::checkForUpdate (juce::StringRef product,
                                                          juce::StringRef currentVersion,
                                                          ReleaseTrack track,
                                                          juce::StringRef variant)
{
    VersionQuery query;
    query.product = juce::String (product);
    query.currentVersion = juce::String (currentVersion);
    query.releaseTrack = track;
    query.variant = juce::String (variant);
    query.platform = HardwareInfo::getCurrentPlatform();

    VersionInfo version;
    lastResult = backend.getLatestVersion (query, version);
    if (lastResult.failed() || ! version.isNewer)
        return std::nullopt;

    return version;
}

juce::Result UpdateChecker::getLastResult() const
{
    return lastResult;
}

} // namespace osci::licensing
