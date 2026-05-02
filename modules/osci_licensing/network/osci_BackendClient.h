#pragma once

namespace osci::licensing
{

enum class ReleaseTrack
{
    Alpha,
    Beta,
    Stable,
};

struct BackendClientConfig
{
    juce::String apiBaseUrl = "https://api.osci-render.com";
    int timeoutMs = 15000;
};

struct ActivationResponse
{
    juce::String token;
    juce::int64 issuedAtSeconds = 0;
    juce::int64 expiresAtSeconds = 0;
    int keyId = 0;
    juce::String tier;
    juce::String email;
    juce::String productId;
    juce::String productName;
};

struct VersionInfo
{
    juce::String product;
    juce::String semver;
    juce::String releaseTrack;
    juce::String variant;
    juce::String platform;
    juce::String artifactKind;
    juce::int64 sizeBytes = 0;
    juce::String sha256;
    juce::String ed25519Signature;
    juce::String notesMarkdown;
    juce::String minSupportedFrom;
    bool isNewer = false;
};

struct VersionQuery
{
    juce::String product;
    juce::String currentVersion;
    ReleaseTrack releaseTrack = ReleaseTrack::Stable;
    juce::String variant = "premium";
    juce::String platform;
};

class BackendClient
{
public:
    explicit BackendClient (BackendClientConfig config = {});

    juce::Result activateLicense (juce::StringRef licenseKey,
                                  juce::StringRef productHint,
                                  ActivationResponse& response) const;

    juce::Result getLatestVersion (const VersionQuery& query, VersionInfo& response) const;
    juce::Result getDownloadUrl (const VersionInfo& version, juce::StringRef licenseKey, juce::String& url) const;

    const BackendClientConfig& getConfig() const noexcept { return config; }

    static juce::String toString (ReleaseTrack track);

private:
    BackendClientConfig config;

    juce::String endpoint (juce::StringRef path) const;
    juce::Result getJson (juce::StringRef path, const juce::StringPairArray& params, juce::var& response) const;
    juce::Result postJson (juce::StringRef path, const juce::var& body, juce::var& response) const;
};

} // namespace osci::licensing
