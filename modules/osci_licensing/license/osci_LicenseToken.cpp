#ifndef OSCI_LICENSING_BACKEND_PUBLIC_KEY_B64
#define OSCI_LICENSING_BACKEND_PUBLIC_KEY_B64 ""
#endif

#ifndef OSCI_LICENSING_RELEASE_PUBLIC_KEY_B64
#define OSCI_LICENSING_RELEASE_PUBLIC_KEY_B64 ""
#endif

namespace osci::licensing
{
namespace
{
    LicenseToken::Ed25519Verifier verifierOverride;
    juce::String backendPublicKeyOverride;
    juce::String releasePublicKeyOverride;

    juce::MemoryBlock stringToBytes (juce::StringRef text)
    {
        const juce::String textString (text);
        return { textString.toRawUTF8(), std::strlen (textString.toRawUTF8()) };
    }

    juce::String toBase64 (const juce::MemoryBlock& bytes)
    {
        return juce::Base64::toBase64 (bytes.getData(), bytes.getSize());
    }

    std::optional<juce::MemoryBlock> decodeBase64 (juce::String text)
    {
        text = text.trim().replace ("-", "+").replace ("_", "/");
        while ((text.length() % 4) != 0)
            text << "=";

        juce::MemoryBlock decoded;
        juce::MemoryOutputStream output (decoded, false);
        if (! juce::Base64::convertFromBase64 (output, text))
            return std::nullopt;

        output.flush();

        return decoded;
    }

    std::optional<LicenseTokenPayload> parsePayloadSegment (juce::StringRef payloadSegment)
    {
        const auto decodedPayload = decodeBase64 (juce::String (payloadSegment));
        if (! decodedPayload)
            return std::nullopt;

        const juce::String payloadJson (static_cast<const char*> (decodedPayload->getData()),
                                        static_cast<int> (decodedPayload->getSize()));
        juce::var parsed;
        if (juce::JSON::parse (payloadJson, parsed).failed())
            return std::nullopt;

        auto* object = parsed.getDynamicObject();
        if (object == nullptr)
            return std::nullopt;

        LicenseTokenPayload payload;
        payload.version = static_cast<int> (object->getProperty ("v"));
        payload.keyId = static_cast<int> (object->getProperty ("kid"));
        payload.licenseKey = object->getProperty ("lk").toString();
        payload.productId = object->getProperty ("pid").toString();
        payload.provider = object->getProperty ("prv").toString();
        payload.email = object->getProperty ("em").toString();
        payload.tier = object->getProperty ("tr").toString();

        const auto issuedAtSeconds = static_cast<juce::int64> (object->getProperty ("iat"));
        const auto expiresAtSeconds = static_cast<juce::int64> (object->getProperty ("exp"));
        payload.issuedAt = juce::Time (issuedAtSeconds * 1000);
        payload.expiresAt = juce::Time (expiresAtSeconds * 1000);

        if (payload.version <= 0 || payload.keyId <= 0 || payload.licenseKey.isEmpty()
            || payload.productId.isEmpty() || payload.tier.isEmpty() || expiresAtSeconds <= 0)
            return std::nullopt;

        return payload;
    }

    bool verifyEd25519 (const juce::MemoryBlock& message,
                        const juce::MemoryBlock& signature,
                        const juce::MemoryBlock& publicKey)
    {
        if (verifierOverride != nullptr)
            return verifierOverride (message, signature, publicKey);

        juce::ignoreUnused (message, signature, publicKey);
        return false;
    }

    bool hasVerifier()
    {
        return verifierOverride != nullptr;
    }

    std::optional<juce::MemoryBlock> configuredBackendPublicKey()
    {
        const auto key = LicenseToken::getBackendPublicKeyBase64();
        if (key.isEmpty())
            return std::nullopt;

        auto decoded = decodeBase64 (key);
        if (! decoded || decoded->getSize() != 32)
            return std::nullopt;

        return decoded;
    }

    std::optional<juce::MemoryBlock> configuredReleasePublicKey()
    {
        const auto key = LicenseToken::getReleasePublicKeyBase64();
        if (key.isEmpty())
            return std::nullopt;

        auto decoded = decodeBase64 (key);
        if (! decoded || decoded->getSize() != 32)
            return std::nullopt;

        return decoded;
    }

    std::optional<juce::MemoryBlock> decodeSignature (juce::StringRef signatureBase64)
    {
        auto decoded = decodeBase64 (juce::String (signatureBase64));
        if (! decoded || decoded->getSize() != 64)
            return std::nullopt;

        return decoded;
    }
}

std::optional<LicenseTokenPayload> LicenseToken::inspectUnverified (const juce::String& token)
{
    const auto separator = token.indexOfChar ('.');
    if (separator <= 0 || separator >= token.length() - 1)
        return std::nullopt;

    return parsePayloadSegment (token.substring (0, separator));
}

LicenseTokenValidation LicenseToken::validate (const juce::String& token,
                                               juce::Time now,
                                               juce::RelativeTime offlineGrace)
{
    LicenseTokenValidation validation;

    const auto separator = token.indexOfChar ('.');
    if (separator <= 0 || separator >= token.length() - 1)
    {
        validation.result = juce::Result::fail ("License token is malformed");
        return validation;
    }

    const auto payloadSegment = token.substring (0, separator);
    const auto signatureSegment = token.substring (separator + 1);
    auto payload = parsePayloadSegment (payloadSegment);
    if (! payload)
    {
        validation.result = juce::Result::fail ("License token payload is invalid");
        return validation;
    }

    validation.payload = *payload;

    const auto publicKey = configuredBackendPublicKey();
    if (! publicKey)
    {
        validation.result = juce::Result::fail ("License token public key is not configured");
        return validation;
    }

    const auto signature = decodeSignature (signatureSegment);
    if (! signature)
    {
        validation.result = juce::Result::fail ("License token signature is invalid");
        return validation;
    }

    if (! hasVerifier())
    {
        validation.result = juce::Result::fail ("Ed25519 verifier is not configured");
        return validation;
    }

    validation.signatureVerified = verifyEd25519 (stringToBytes (payloadSegment), *signature, *publicKey);
    if (! validation.signatureVerified)
    {
        validation.result = juce::Result::fail ("License token signature does not verify");
        return validation;
    }

    const auto nowMs = now.toMilliseconds();
    const auto expiryMs = validation.payload.expiresAt.toMilliseconds();
    validation.expired = nowMs > expiryMs;
    validation.withinOfflineGrace = validation.expired && nowMs <= expiryMs + offlineGrace.inMilliseconds();

    if (validation.expired && ! validation.withinOfflineGrace)
    {
        validation.result = juce::Result::fail ("License token has expired");
        return validation;
    }

    validation.result = juce::Result::ok();
    return validation;
}

juce::String LicenseToken::releaseSigningMessage (juce::StringRef semver,
                                                  juce::StringRef platform,
                                                  juce::StringRef sha256Hex)
{
    return juce::String (semver) + "|" + juce::String (platform) + "|" + juce::String (sha256Hex).toLowerCase();
}

bool LicenseToken::verifyReleaseSignature (juce::StringRef semver,
                                           juce::StringRef platform,
                                           juce::StringRef sha256Hex,
                                           juce::StringRef signatureBase64)
{
    const auto publicKey = configuredReleasePublicKey();
    const auto signature = decodeSignature (signatureBase64);
    if (! publicKey || ! signature || ! hasVerifier())
        return false;

    return verifyEd25519 (stringToBytes (releaseSigningMessage (semver, platform, sha256Hex)), *signature, *publicKey);
}

bool LicenseToken::canVerifyLicenseTokens()
{
    return hasVerifier() && configuredBackendPublicKey().has_value();
}

bool LicenseToken::canVerifyReleaseSignatures()
{
    return hasVerifier() && configuredReleasePublicKey().has_value();
}

juce::String LicenseToken::getBackendPublicKeyBase64()
{
    if (backendPublicKeyOverride.isNotEmpty())
        return backendPublicKeyOverride;

    return OSCI_LICENSING_BACKEND_PUBLIC_KEY_B64;
}

juce::String LicenseToken::getReleasePublicKeyBase64()
{
    if (releasePublicKeyOverride.isNotEmpty())
        return releasePublicKeyOverride;

    return OSCI_LICENSING_RELEASE_PUBLIC_KEY_B64;
}

void LicenseToken::setEd25519VerifierForTesting (Ed25519Verifier verifier)
{
    verifierOverride = std::move (verifier);
}

void LicenseToken::setPublicKeysForTesting (juce::String backendPublicKeyBase64,
                                            juce::String releasePublicKeyBase64)
{
    backendPublicKeyOverride = std::move (backendPublicKeyBase64);
    releasePublicKeyOverride = std::move (releasePublicKeyBase64);
}

} // namespace osci::licensing
