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
        osci::LicenseToken::setPublicKeysForTesting (makePublicKey(), makePublicKey());
        osci::LicenseToken::setEd25519VerifierForTesting (
            [] (const juce::MemoryBlock& message,
                const juce::MemoryBlock& signature,
                const juce::MemoryBlock& publicKey)
            {
                return message.getSize() > 0 && signature.getSize() == 64 && publicKey.getSize() == 32;
            });
    }

    void resetTestVerifier()
    {
        osci::LicenseToken::setEd25519VerifierForTesting ({});
        osci::LicenseToken::setPublicKeysForTesting ({}, {});
    }

    juce::File makeTempSettingsDirectory()
    {
        auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory)
            .getChildFile ("osci-settings-test-" + juce::Uuid().toString());
        directory.createDirectory();
        return directory;
    }

    juce::PropertiesFile::Options makeTempSettingsOptions (juce::StringRef applicationName)
    {
        juce::PropertiesFile::Options options;
        options.applicationName = juce::String (applicationName) + "-" + juce::Uuid().toString();
        options.filenameSuffix = ".settings";
        options.folderName = "osci-settings-test-" + juce::Uuid().toString();
        options.osxLibrarySubFolder = "Application Support";
        options.millisecondsBeforeSaving = 0;
        return options;
    }

    void deleteTempSettings (const juce::PropertiesFile::Options& options)
    {
        options.getDefaultFile().getParentDirectory().deleteRecursively();
    }
}

class LicensingModuleTest : public juce::UnitTest
{
public:
    LicensingModuleTest() : juce::UnitTest ("Licensing module", "Licensing") {}

    void runTest() override
    {
        juce::MessageManager::getInstance();
        installTestVerifier();

        beginTest ("Premium token validates before expiry");
        {
            const juce::Time now (1'000'000LL * 1000);
            const auto token = makeToken (999'000, 1'010'000, "premium");
            const auto validation = osci::LicenseToken::validate (token, now);
            expect (validation.result.wasOk(), validation.result.getErrorMessage());
            expect (validation.signatureVerified);
            expect (validation.hasPremium());
            expect (! validation.expired);
        }

        beginTest ("Expired token remains premium inside offline grace");
        {
            const juce::Time now ((1'000'000LL + 10 * 24 * 60 * 60) * 1000);
            const auto token = makeToken (900'000, 1'000'000, "premium");
            const auto validation = osci::LicenseToken::validate (token, now);
            expect (validation.result.wasOk(), validation.result.getErrorMessage());
            expect (validation.expired);
            expect (validation.withinOfflineGrace);
            expect (validation.hasPremium());
        }

        beginTest ("Expired token fails outside offline grace");
        {
            const juce::Time now ((1'000'000LL + 15 * 24 * 60 * 60) * 1000);
            const auto token = makeToken (900'000, 1'000'000, "premium");
            const auto validation = osci::LicenseToken::validate (token, now);
            expect (validation.result.failed());
            expect (! validation.hasPremium());
        }

        beginTest ("Release signing message matches backend contract");
        expectEquals (osci::LicenseToken::releaseSigningMessage ("2.8.10.8", "mac-arm64", "ABCDEF"),
                      juce::String ("2.8.10.8|mac-arm64|abcdef"));

        beginTest ("Release signature verifier uses configured public key");
        expect (osci::LicenseToken::verifyReleaseSignature ("2.8.10.8", "mac-arm64", "abcdef", makeSignature()));

        beginTest ("Production Ed25519 verifier accepts valid signatures");
        resetTestVerifier();
        osci::LicenseToken::setPublicKeysForTesting ({}, "iT+WRr9r0vLoIXcT6vStDDJiOYhkp4lLfhQI11p2OIk=");
        expect (osci::LicenseToken::verifyReleaseSignature (
            "2.0.0", "mac-arm64", "abcdef",
            "2SKjjG3qSG3DTL0qgtBCwg2u2Tc34OB62jVK7CVArxFn9WzXyorb8jgIKoEV5d03kndkRBcZV+VnwoeyQsEKAA=="));
        expect (! osci::LicenseToken::verifyReleaseSignature (
            "2.0.1", "mac-arm64", "abcdef",
            "2SKjjG3qSG3DTL0qgtBCwg2u2Tc34OB62jVK7CVArxFn9WzXyorb8jgIKoEV5d03kndkRBcZV+VnwoeyQsEKAA=="));
        installTestVerifier();

        beginTest ("SettingsStore preserves expected settings filenames");
        {
            expectEquals (osci::SettingsStore::optionsForProductGlobals ("osci-render").getDefaultFile().getFileName(),
                          juce::String ("osci-render_globals.settings"));
            expectEquals (osci::SettingsStore::optionsForStandaloneApp ("osci-render").getDefaultFile().getFileName(),
                          juce::String ("osci-render.settings"));
            expectEquals (osci::SettingsStore::optionsForSharedLicensing().getDefaultFile().getFileName(),
                          juce::String ("osci-licensing.settings"));
        }

        beginTest ("SettingsStore persists typed values");
        {
            auto options = makeTempSettingsOptions ("typed");

            {
                osci::SettingsStore store (options);
                store.set ("bool", true);
                store.set ("int", 7);
                store.set ("double", 1.25);
                store.set ("string", "value");
                expect (store.save());
            }

            {
                osci::SettingsStore store (options);
                expect (store.getBool ("bool"));
                expectEquals (store.getInt ("int"), 7);
                expectWithinAbsoluteError (store.getDouble ("double"), 1.25, 0.0001);
                expectEquals (store.getString ("string"), juce::String ("value"));
                store.remove ("string");
                expect (store.save());
            }

            {
                osci::SettingsStore store (options);
                expectEquals (store.getString ("string", "fallback"), juce::String ("fallback"));
            }

            deleteTempSettings (options);
        }

        beginTest ("SettingsStore merges independent saves by key");
        {
            auto options = makeTempSettingsOptions ("merge");

            osci::SettingsStore first (options);
            osci::SettingsStore second (options);

            first.set ("license.osci-render.token", "token");
            expect (first.save());

            second.set ("updates.osci-render.releaseTrack", "beta");
            expect (second.save());

            osci::SettingsStore reloaded (options);
            expectEquals (reloaded.getString ("license.osci-render.token"), juce::String ("token"));
            expectEquals (reloaded.getString ("updates.osci-render.releaseTrack"), juce::String ("beta"));

            deleteTempSettings (options);
        }

        beginTest ("SettingsStore does not resurrect externally removed keys");
        {
            auto options = makeTempSettingsOptions ("merge-remove");

            osci::SettingsStore staleWriter (options);
            staleWriter.set ("license.osci-render.token", "token");
            expect (staleWriter.save());

            osci::SettingsStore remover (options);
            remover.remove ("license.osci-render.token");
            expectEquals (remover.getString ("license.osci-render.token", "missing"), juce::String ("missing"));
            expect (remover.save());

            staleWriter.set ("updates.osci-render.dismissedAt", 123.0);
            expect (staleWriter.save());

            osci::SettingsStore reloaded (options);
            expectEquals (reloaded.getString ("license.osci-render.token", "missing"), juce::String ("missing"));
            expectWithinAbsoluteError (reloaded.getDouble ("updates.osci-render.dismissedAt"), 123.0, 0.0001);

            deleteTempSettings (options);
        }

        beginTest ("LicenseManager treats free cached tokens as non-premium");
        {
            osci::LicenseManager::Config config;
            config.settingsOptions = makeTempSettingsOptions ("license-free");
            osci::LicenseManager manager (config);
            const auto nowSeconds = juce::Time::getCurrentTime().toMilliseconds() / 1000;

            osci::SettingsStore store (config.settingsOptions);
            store.set (manager.getTokenSettingsKey(), makeToken (nowSeconds - 60, nowSeconds + 3600, "free"));
            expect (store.save());
            expect (manager.loadCachedToken().wasOk());
            expect (manager.status() == osci::LicenseManager::Status::Free);
            expect (! manager.hasPremium());
            deleteTempSettings (config.settingsOptions);
        }

        beginTest ("LicenseManager isolates tokens by product");
        {
            auto options = makeTempSettingsOptions ("license-products");
            const auto nowSeconds = juce::Time::getCurrentTime().toMilliseconds() / 1000;

            osci::LicenseManager::Config osciConfig;
            osciConfig.productSlug = "osci-render";
            osciConfig.settingsOptions = options;
            osci::LicenseManager osciManager (osciConfig);

            osci::LicenseManager::Config sosciConfig;
            sosciConfig.productSlug = "sosci";
            sosciConfig.settingsOptions = options;
            osci::LicenseManager sosciManager (sosciConfig);

            osci::SettingsStore store (options);
            store.set (osciManager.getTokenSettingsKey(), makeToken (nowSeconds - 60, nowSeconds + 3600, "premium"));
            store.set (sosciManager.getTokenSettingsKey(), makeToken (nowSeconds - 60, nowSeconds + 3600, "free"));
            expect (store.save());

            expect (osciManager.loadCachedToken().wasOk());
            expect (sosciManager.loadCachedToken().wasOk());
            expect (osciManager.hasPremium());
            expect (! sosciManager.hasPremium());

            osciManager.deactivate();
            expect (! osciManager.hasPremium());
            expect (sosciManager.loadCachedToken().wasOk());
            expect (! sosciManager.hasPremium());

            deleteTempSettings (options);
        }

        beginTest ("LicenseManager ignores old license.dat files");
        {
            const auto product = "osci-test-" + juce::Uuid().toString();
            const auto oldDirectory = osci::HardwareInfo::getDefaultStorageDirectory (product);
            oldDirectory.createDirectory();

            const auto nowSeconds = juce::Time::getCurrentTime().toMilliseconds() / 1000;
            expect (oldDirectory.getChildFile ("license.dat").replaceWithText (makeToken (nowSeconds - 60, nowSeconds + 3600, "premium")));

            osci::LicenseManager::Config config;
            config.productSlug = product;
            config.settingsOptions = makeTempSettingsOptions ("license-ignore-old");
            osci::LicenseManager manager (config);

            expect (manager.loadCachedToken().wasOk());
            expect (manager.status() == osci::LicenseManager::Status::Free);
            expect (! manager.hasPremium());

            oldDirectory.deleteRecursively();
            deleteTempSettings (config.settingsOptions);
        }

        beginTest ("Token payload exposes license key for refresh");
        {
            const auto nowSeconds = juce::Time::getCurrentTime().toMilliseconds() / 1000;
            auto payload = osci::LicenseToken::inspectUnverified (makeToken (nowSeconds - 60, nowSeconds + 3600, "premium"));
            expect (payload.has_value());
            expectEquals (payload->licenseKey, juce::String ("LICENSE-KEY"));
        }

        beginTest ("UpdateSettings stores per-product update preferences");
        {
            auto options = makeTempSettingsOptions ("updates");

            osci::UpdateSettings osciUpdates ("osci-render", osci::SettingsStore (options));
            osci::UpdateSettings sosciUpdates ("sosci", osci::SettingsStore (options));

            osciUpdates.setReleaseTrack (osci::ReleaseTrack::Beta);
            osciUpdates.dismiss ("2.9.1.0", 100.0);

            expect (osciUpdates.betaUpdatesEnabled());
            expect (osciUpdates.releaseTrack() == osci::ReleaseTrack::Beta);
            expect (osciUpdates.isDismissed ("2.9.1.0", 120.0));
            expect (! sosciUpdates.betaUpdatesEnabled());
            expect (sosciUpdates.releaseTrack() == osci::ReleaseTrack::Stable);
            expect (! sosciUpdates.isDismissed ("2.9.1.0", 120.0));
            deleteTempSettings (options);
        }

        beginTest ("PendingInstall stores markers and validates artifacts");
        {
            auto options = makeTempSettingsOptions ("pending");
            auto directory = makeTempSettingsDirectory();
            auto artifact = directory.getChildFile ("installer.pkg");
            expect (artifact.replaceWithText ("installer bytes"));

            osci::VersionInfo version;
            version.product = "osci-render";
            version.semver = "2.9.1.0";
            version.releaseTrack = "stable";
            version.variant = "premium";
            version.platform = "mac-arm64";
            version.artifactKind = "pkg";
            version.sha256 = osci::fileSha256Hex (artifact);
            version.ed25519Signature = makeSignature();
            version.sizeBytes = artifact.getSize();

            osci::PendingInstall pending ("osci-render", osci::SettingsStore (options));
            auto marker = osci::PendingInstall::makeMarker ("osci-render", "2.9.0.0", version, artifact);
            expect (pending.write (marker).wasOk());

            auto loaded = pending.load();
            expect (loaded.has_value());
            expectEquals (loaded->targetVersion, juce::String ("2.9.1.0"));
            expect (osci::PendingInstall::isResolvedByRunningVersion (*loaded, "2.9.1.0"));
            expect (osci::PendingInstall::isResolvedByRunningVersion (*loaded, "2.10.0.0"));
            expect (! osci::PendingInstall::isResolvedByRunningVersion (*loaded, "2.9.0.0"));
            expect (osci::PendingInstall::validateArtifact (*loaded).wasOk());

            loaded->sha256 = "bad";
            expect (osci::PendingInstall::validateArtifact (*loaded).failed());

            pending.clear();
            expect (! pending.load().has_value());
            directory.deleteRecursively();
            deleteTempSettings (options);
        }

        beginTest ("DAW matcher recognises known hosts without broad false positives");
        {
            juce::String display;
            expect (osci::DawProcessDetector::isKnownDawProcessName (
                "/Applications/Ableton Live 12 Suite.app/Contents/MacOS/Ableton Live 12 Suite", &display));
            expectEquals (display, juce::String ("Ableton Live"));
            expect (osci::DawProcessDetector::isKnownDawProcessName ("pluginval", &display));
            expectEquals (display, juce::String ("pluginval"));
            expect (osci::DawProcessDetector::isKnownDawProcessName ("VST3PluginTestHost.exe", &display));
            expectEquals (display, juce::String ("VST3 plugin test host"));
            expect (osci::DawProcessDetector::isKnownDawProcessName ("auval", &display));
            expectEquals (display, juce::String ("AU validation tool"));
            expect (! osci::DawProcessDetector::isKnownDawProcessName ("Final Cut Pro"));
        }

        beginTest ("Hardware helpers return stable local values");
        expect (osci::HardwareInfo::getCurrentPlatform().isNotEmpty());
        expectEquals (osci::HardwareInfo::getDefaultStorageDirectory ("osci-render").getFileName(),
                      juce::String ("osci-render"));

        resetTestVerifier();
    }
};

static LicensingModuleTest licensingModuleTest;
