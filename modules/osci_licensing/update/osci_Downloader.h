#pragma once

namespace osci::licensing
{

class Downloader
{
public:
    using Progress = std::function<void (double fraction, juce::int64 downloadedBytes)>;

    struct Config
    {
        BackendClientConfig backend;
        juce::File downloadDirectory;
    };

    Downloader();
    explicit Downloader (Config config);

    juce::Result downloadAndVerify (const VersionInfo& version,
                                    juce::StringRef licenseKey,
                                    Progress progress = {});

    juce::File getDownloadedFile() const;

private:
    Config config;
    BackendClient backend;
    juce::File downloadedFile;

    juce::File targetFileFor (const VersionInfo& version) const;
};

} // namespace osci::licensing
