#pragma once

namespace osci::licensing
{

class UpdateChecker
{
public:
    explicit UpdateChecker (BackendClient client = BackendClient());

    std::optional<VersionInfo> checkForUpdate (juce::StringRef product,
                                               juce::StringRef currentVersion,
                                               ReleaseTrack track = ReleaseTrack::Stable,
                                               juce::StringRef variant = "premium");

    juce::Result getLastResult() const;

private:
    BackendClient backend;
    juce::Result lastResult = juce::Result::ok();
};

} // namespace osci::licensing
