#include "JuceHeader.h"

namespace
{
    juce::String toBase64Url (const juce::MemoryBlock& bytes)
    {
        auto encoded = juce::Base64::toBase64 (bytes.getData(), bytes.getSize())
            .replace ("+", "-")
            .replace ("/", "_");

        while (encoded.endsWithChar ('='))
            encoded = encoded.dropLastCharacters (1);

        return encoded;
    }

    juce::String makePublicKey()
    {
        std::array<juce::uint8, 32> bytes {};
        return juce::Base64::toBase64 (bytes.data(), bytes.size());
    }

    juce::String makeSignature()
    {
        std::array<juce::uint8, 64> bytes {};
        juce::MemoryBlock signature (bytes.data(), bytes.size());
        return toBase64Url (signature);
    }

    juce::String makeToken (juce::int64 issuedAtSeconds, juce::int64 expiresAtSeconds, juce::String tier)
    {
        auto payload = juce::var (new juce::DynamicObject());
        auto* object = payload.getDynamicObject();
        object->setProperty ("v", 1);
        object->setProperty ("kid", 1);
        object->setProperty ("lk", "LICENSE-KEY");
        object->setProperty ("pid", "osci-render");
        object->setProperty ("prv", "gumroad");
        object->setProperty ("em", "buyer@example.com");
        object->setProperty ("tr", std::move (tier));
        object->setProperty ("iat", issuedAtSeconds);
        object->setProperty ("exp", expiresAtSeconds);

        const auto payloadJson = juce::JSON::toString (payload, true);
        const juce::MemoryBlock payloadBytes (payloadJson.toRawUTF8(), std::strlen (payloadJson.toRawUTF8()));
        return toBase64Url (payloadBytes) + "." + makeSignature();
    }

    void installTestVerifier()
    {
        osci::licensing::LicenseToken::setPublicKeysForTesting (makePublicKey(), makePublicKey());
        osci::licensing::LicenseToken::setEd25519VerifierForTesting (
            [] (const juce::MemoryBlock& message,
                const juce::MemoryBlock& signature,
                const juce::MemoryBlock& publicKey)
            {
                return message.getSize() > 0 && signature.getSize() == 64 && publicKey.getSize() == 32;
            });
    }

    void resetTestVerifier()
    {
        osci::licensing::LicenseToken::setEd25519VerifierForTesting ({});
        osci::licensing::LicenseToken::setPublicKeysForTesting ({}, {});
    }
}

class LicensingModuleTest : public juce::UnitTest
{
public:
    LicensingModuleTest() : juce::UnitTest ("Licensing module", "Licensing") {}

    void runTest() override
    {
        installTestVerifier();

        beginTest ("Premium token validates before expiry");
        {
            const juce::Time now (1'000'000LL * 1000);
            const auto token = makeToken (999'000, 1'010'000, "premium");
            const auto validation = osci::licensing::LicenseToken::validate (token, now);
            expect (validation.result.wasOk(), validation.result.getErrorMessage());
            expect (validation.signatureVerified);
            expect (validation.hasPremium());
            expect (! validation.expired);
        }

        beginTest ("Expired token remains premium inside offline grace");
        {
            const juce::Time now ((1'000'000LL + 10 * 24 * 60 * 60) * 1000);
            const auto token = makeToken (900'000, 1'000'000, "premium");
            const auto validation = osci::licensing::LicenseToken::validate (token, now);
            expect (validation.result.wasOk(), validation.result.getErrorMessage());
            expect (validation.expired);
            expect (validation.withinOfflineGrace);
            expect (validation.hasPremium());
        }

        beginTest ("Expired token fails outside offline grace");
        {
            const juce::Time now ((1'000'000LL + 15 * 24 * 60 * 60) * 1000);
            const auto token = makeToken (900'000, 1'000'000, "premium");
            const auto validation = osci::licensing::LicenseToken::validate (token, now);
            expect (validation.result.failed());
            expect (! validation.hasPremium());
        }

        beginTest ("Release signing message matches backend contract");
        expectEquals (osci::licensing::LicenseToken::releaseSigningMessage ("2.8.10.8", "mac-arm64", "ABCDEF"),
                      juce::String ("2.8.10.8|mac-arm64|abcdef"));

        beginTest ("Release signature verifier uses configured public key");
        expect (osci::licensing::LicenseToken::verifyReleaseSignature ("2.8.10.8", "mac-arm64", "abcdef", makeSignature()));

        beginTest ("Production Ed25519 verifier accepts valid signatures");
        resetTestVerifier();
        osci::licensing::LicenseToken::setPublicKeysForTesting ({}, "iT+WRr9r0vLoIXcT6vStDDJiOYhkp4lLfhQI11p2OIk=");
        expect (osci::licensing::LicenseToken::verifyReleaseSignature (
            "2.0.0", "mac-arm64", "abcdef",
            "2SKjjG3qSG3DTL0qgtBCwg2u2Tc34OB62jVK7CVArxFn9WzXyorb8jgIKoEV5d03kndkRBcZV+VnwoeyQsEKAA=="));
        expect (! osci::licensing::LicenseToken::verifyReleaseSignature (
            "2.0.1", "mac-arm64", "abcdef",
            "2SKjjG3qSG3DTL0qgtBCwg2u2Tc34OB62jVK7CVArxFn9WzXyorb8jgIKoEV5d03kndkRBcZV+VnwoeyQsEKAA=="));
        installTestVerifier();

        beginTest ("LicenseManager treats free cached tokens as non-premium");
        {
            const auto storageDirectory = juce::File::getSpecialLocation (juce::File::tempDirectory)
                .getChildFile ("osci-licensing-test-" + juce::Uuid().toString());

            osci::licensing::LicenseManager::Config config;
            config.storageDirectory = storageDirectory;
            osci::licensing::LicenseManager manager (config);
            const auto nowSeconds = juce::Time::getCurrentTime().toMilliseconds() / 1000;

            expect (storageDirectory.createDirectory());
            expect (manager.getTokenFile().replaceWithText (makeToken (nowSeconds - 60, nowSeconds + 3600, "free")));
            expect (manager.loadCachedToken().wasOk());
            expect (manager.status() == osci::licensing::LicenseManager::Status::Free);
            expect (! manager.hasPremium());
            storageDirectory.deleteRecursively();
        }

        beginTest ("Hardware helpers return stable local values");
        expect (osci::licensing::HardwareInfo::getCurrentPlatform().isNotEmpty());
        expectEquals (osci::licensing::HardwareInfo::getDefaultStorageDirectory ("osci-render").getFileName(),
                      juce::String ("osci-render"));

        resetTestVerifier();
    }
};

static LicensingModuleTest licensingModuleTest;
