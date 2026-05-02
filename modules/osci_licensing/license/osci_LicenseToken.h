#pragma once

#include <functional>
#include <optional>

namespace osci::licensing
{

struct LicenseTokenPayload
{
    int version = 0;
    int keyId = 0;
    juce::String licenseKey;
    juce::String productId;
    juce::String provider;
    juce::String email;
    juce::String tier;
    juce::Time issuedAt;
    juce::Time expiresAt;

    bool isPremium() const noexcept { return tier == "premium"; }
};

struct LicenseTokenValidation
{
    juce::Result result = juce::Result::fail ("Token has not been validated");
    LicenseTokenPayload payload;
    bool signatureVerified = false;
    bool expired = false;
    bool withinOfflineGrace = false;

    bool hasPremium() const noexcept { return result.wasOk() && signatureVerified && payload.isPremium(); }
};

class LicenseToken final
{
public:
    using Ed25519Verifier = std::function<bool (const juce::MemoryBlock& message,
                                                const juce::MemoryBlock& signature,
                                                const juce::MemoryBlock& publicKey)>;

    static std::optional<LicenseTokenPayload> inspectUnverified (const juce::String& token);

    static LicenseTokenValidation validate (const juce::String& token,
                                            juce::Time now = juce::Time::getCurrentTime(),
                                            juce::RelativeTime offlineGrace = juce::RelativeTime::days (14.0));

    static juce::String releaseSigningMessage (juce::StringRef semver,
                                               juce::StringRef platform,
                                               juce::StringRef sha256Hex);

    static bool verifyReleaseSignature (juce::StringRef semver,
                                        juce::StringRef platform,
                                        juce::StringRef sha256Hex,
                                        juce::StringRef signatureBase64);

    static bool canVerifyLicenseTokens();
    static bool canVerifyReleaseSignatures();

    static juce::String getBackendPublicKeyBase64();
    static juce::String getReleasePublicKeyBase64();

    static void setEd25519VerifierForTesting (Ed25519Verifier verifier);
    static void setPublicKeysForTesting (juce::String backendPublicKeyBase64,
                                         juce::String releasePublicKeyBase64);

private:
    LicenseToken() = delete;
};

} // namespace osci::licensing
