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
        object->setProperty ("token_format_version", 3);
        object->setProperty ("key_id", 1);
        object->setProperty ("license_key", "LICENSE-KEY");
        object->setProperty ("provider_product_id", "osci-render");
        object->setProperty ("provider", "gumroad");
        object->setProperty ("email", "buyer@example.com");
        object->setProperty ("tier", std::move (tier));
        object->setProperty ("issued_at", issuedAtSeconds);
        object->setProperty ("expires_at", expiresAtSeconds);

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

    osci::LinuxInstallManifest makeOsciRenderManifest()
    {
        return { "osci-render", "osci-render", "osci-render",
                 { "osci-render.vst3" }, { "osci-render-instrument.vst3" }, "AudioVideo;Audio;" };
    }

    juce::String testLinuxArchitectureDirectory() {
#if JUCE_ARM && JUCE_64BIT
        return "aarch64-linux";
#else
        return "x86_64-linux";
#endif
    }

    juce::File makeLinuxArchive (const juce::File& directory,
                                 bool includeUnexpectedFile = false,
                                 bool omitInstrument = false,
                                 bool wrongInstrumentArchitecture = false)
    {
        const auto payload = directory.getChildFile ("payload");
        payload.createDirectory();
        const auto standalone = payload.getChildFile ("osci-render");
        const auto effect = payload.getChildFile ("effect.so");
        const auto instrument = payload.getChildFile ("instrument.so");
        standalone.replaceWithText ("standalone");
        effect.replaceWithText ("effect");
        instrument.replaceWithText ("instrument");

        juce::ZipFile::Builder builder;
        builder.addFile (standalone, 0, "osci-render");
        const auto architecture = testLinuxArchitectureDirectory();
        builder.addFile (effect, 0, "osci-render.vst3/Contents/" + architecture + "/osci-render.so");
        if (!omitInstrument) {
            const auto instrumentArchitecture = wrongInstrumentArchitecture
                ? (architecture == "aarch64-linux" ? "x86_64-linux" : "aarch64-linux")
                : architecture;
            builder.addFile (instrument, 0, "osci-render-instrument.vst3/Contents/" + instrumentArchitecture
                                             + "/osci-render-instrument.so");
        }
        if (includeUnexpectedFile) {
            builder.addFile (effect, 0, "unexpected.txt");
        }

        const auto archive = directory.getChildFile (includeUnexpectedFile ? "invalid.zip" : "valid.zip");
        juce::FileOutputStream output (archive);
        if (!output.openedOk() || !builder.writeToStream (output, nullptr)) {
            return {};
        }
        output.flush();
        return archive;
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

        beginTest ("Feedback values match the API contract");
        expectEquals (osci::FeedbackClient::kindToString (osci::FeedbackKind::bug), juce::String ("bug"));
        expectEquals (osci::FeedbackClient::kindToString (osci::FeedbackKind::featureRequest), juce::String ("feature_request"));
        expectEquals (osci::FeedbackClient::attachmentKindToString (osci::FeedbackAttachmentKind::screenshot), juce::String ("screenshot"));
        expectEquals (osci::FeedbackClient::attachmentKindToString (osci::FeedbackAttachmentKind::project), juce::String ("project"));

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

        beginTest ("Linux install settings use defaults and isolate products");
        {
            auto options = makeTempSettingsOptions ("linux-install-settings");
            osci::LinuxInstallSettings osciSettings ("osci-render", osci::SettingsStore (options));
            osci::LinuxInstallSettings sosciSettings ("sosci", osci::SettingsStore (options));
            std::optional<osci::LinuxInstallLocations> savedLocations;
            expect (osciSettings.loadSaved (savedLocations).wasOk());
            expect (!savedLocations.has_value());

            const auto root = makeTempSettingsDirectory();
            const osci::LinuxInstallLocations locations {
                root.getChildFile ("applications"), root.getChildFile ("plugins")
            };
            expect (osciSettings.save (locations).wasOk());
            expect (osciSettings.loadSaved (savedLocations).wasOk());
            expect (savedLocations.has_value());
            osci::LinuxInstallLocations loaded;
            expect (osciSettings.load (loaded).wasOk());
            expect (loaded == locations);
            expect (sosciSettings.loadSaved (savedLocations).wasOk());
            expect (!savedLocations.has_value());

            osci::SettingsStore incompleteStore (options);
            incompleteStore.remove (osci::LinuxInstallSettings::vst3Key ("osci-render"));
            expect (incompleteStore.save());
            loaded = {};
            osci::LinuxInstallSettings incompleteSettings ("osci-render", osci::SettingsStore (options));
            expect (incompleteSettings.loadSaved (savedLocations).failed());
            expect (incompleteSettings.load (loaded).failed());
            root.deleteRecursively();
            deleteTempSettings (options);
        }

#if JUCE_LINUX
        beginTest ("Linux installer validates, installs and migrates known files");
        {
            auto root = makeTempSettingsDirectory();
            auto options = makeTempSettingsOptions ("linux-installer");
            osci::LinuxInstaller::Config config;
            config.dataHome = root.getChildFile ("data");
            config.settingsOptions = options;
            config.refreshDesktopCaches = false;
            osci::LinuxInstaller installer (config);

            osci::LinuxInstaller::Request request;
            request.manifest = makeOsciRenderManifest();
            request.archive = makeLinuxArchive (root);
            request.locations = { root.getChildFile ("first % $/bin"), root.getChildFile ("first % $/vst3") };
            juce::Image icon (juce::Image::ARGB, 1024, 1024, true);
            juce::MemoryOutputStream iconOutput;
            expect (juce::PNGImageFormat().writeImageToStream (icon, iconOutput));
            request.iconPng = iconOutput.getMemoryBlock();

            osci::LinuxInstaller::Report report;
            auto result = installer.install (request, report);
            expect (result.wasOk(), result.getErrorMessage());
            expect (request.locations.standaloneDirectory.getChildFile ("osci-render").existsAsFile());
            expect (request.locations.vst3Directory.getChildFile ("osci-render.vst3").isDirectory());
            expect (request.locations.vst3Directory.getChildFile ("osci-render-instrument.vst3").isDirectory());
            const auto desktopFile = config.dataHome.getChildFile ("applications/osci-render.desktop");
            const auto installedIcon = config.dataHome.getChildFile ("icons/hicolor/256x256/apps/osci-render.png");
            expect (desktopFile.existsAsFile());
            expect (desktopFile.loadFileAsString().contains ("first %% \\\\$"));
            expect (desktopFile.loadFileAsString().contains ("Categories=AudioVideo;Audio;"));
            expect (installedIcon.existsAsFile());
            expectEquals (juce::ImageFileFormat::loadFrom (installedIcon).getWidth(), 256);

            result = installer.install (request, report);
            expect (result.wasOk(), result.getErrorMessage());
            expect (request.locations.standaloneDirectory.getChildFile ("osci-render").existsAsFile());

            const auto physicalLocations = request.locations;
            const osci::LinuxInstallLocations linkedLocations {
                root.getChildFile ("linked-bin"), root.getChildFile ("linked-vst3")
            };
            expect (physicalLocations.standaloneDirectory.createSymbolicLink (linkedLocations.standaloneDirectory, true));
            expect (physicalLocations.vst3Directory.createSymbolicLink (linkedLocations.vst3Directory, true));
            request.locations = linkedLocations;
            result = installer.install (request, report);
            expect (result.wasOk(), result.getErrorMessage());
            expect (physicalLocations.standaloneDirectory.getChildFile ("osci-render").existsAsFile());

            const auto previousLocations = request.locations;
            previousLocations.standaloneDirectory.getChildFile ("unrelated").replaceWithText ("keep");
            request.locations = { root.getChildFile ("second/bin"), root.getChildFile ("second/vst3") };
            result = installer.install (request, report);
            expect (result.wasOk(), result.getErrorMessage());
            expect (!previousLocations.standaloneDirectory.getChildFile ("osci-render").exists());
            expect (!previousLocations.vst3Directory.getChildFile ("osci-render.vst3").exists());
            expect (previousLocations.standaloneDirectory.getChildFile ("unrelated").existsAsFile());
            expect (request.locations.standaloneDirectory.getChildFile ("osci-render").existsAsFile());

            const osci::LinuxInstallLocations realLocations {
                root.getChildFile ("real-parent/bin"), root.getChildFile ("real-parent/vst3")
            };
            request.locations = realLocations;
            result = installer.install (request, report);
            expect (result.wasOk(), result.getErrorMessage());
            const auto aliasParent = root.getChildFile ("alias-parent");
            expect (realLocations.standaloneDirectory.getParentDirectory().createSymbolicLink (aliasParent, true));
            request.locations = { aliasParent.getChildFile ("bin"), aliasParent.getChildFile ("vst3") };
            result = installer.install (request, report);
            expect (result.wasOk(), result.getErrorMessage());
            expect (realLocations.standaloneDirectory.getChildFile ("osci-render").existsAsFile());
            expect (realLocations.vst3Directory.getChildFile ("osci-render.vst3").isDirectory());

            request.locations = { realLocations.vst3Directory.getChildFile ("osci-render.vst3"),
                                  root.getChildFile ("overlap-vst3") };
            result = installer.install (request, report);
            expect (result.failed());
            expect (result.getErrorMessage().contains ("overlap"));
            expect (realLocations.vst3Directory.getChildFile ("osci-render.vst3").isDirectory());

            const auto nestedDestination = realLocations.vst3Directory.getChildFile ("osci-render.vst3/nested-bin");
            request.locations = { nestedDestination, root.getChildFile ("nested-overlap-vst3") };
            result = installer.install (request, report);
            expect (result.failed());
            expect (result.getErrorMessage().contains ("overlap"));
            expect (!nestedDestination.exists());

            expect (config.dataHome.deleteRecursively());
            expect (config.dataHome.replaceWithText ("blocks desktop registration"));
            request.locations = { root.getChildFile ("launcher-failure/bin"), root.getChildFile ("launcher-failure/vst3") };
            result = installer.install (request, report);
            expect (result.wasOk(), result.getErrorMessage());
            expect (!report.warnings.isEmpty());
            expect (realLocations.standaloneDirectory.getChildFile ("osci-render").existsAsFile());

            osci::LinuxInstallSettings saved ("osci-render", osci::SettingsStore (options));
            std::optional<osci::LinuxInstallLocations> savedLocations;
            expect (saved.loadSaved (savedLocations).wasOk());
            expect (savedLocations.has_value());
            osci::LinuxInstallLocations loaded;
            expect (saved.load (loaded).wasOk());
            expect (loaded == request.locations);
            root.deleteRecursively();
            deleteTempSettings (options);
        }

        beginTest ("Linux installer rejects unexpected archive contents");
        {
            auto root = makeTempSettingsDirectory();
            osci::LinuxInstaller::Config config;
            config.dataHome = root.getChildFile ("data");
            config.settingsOptions = makeTempSettingsOptions ("linux-installer-invalid");
            config.refreshDesktopCaches = false;
            osci::LinuxInstaller installer (config);
            osci::LinuxInstaller::Request request;
            request.manifest = makeOsciRenderManifest();
            request.archive = makeLinuxArchive (root, true);
            request.locations = { root.getChildFile ("bin"), root.getChildFile ("vst3") };
            osci::LinuxInstaller::Report report;
            const auto result = installer.install (request, report);
            expect (result.failed());
            expect (result.getErrorMessage().contains ("unexpected entry"));
            root.deleteRecursively();
            deleteTempSettings (*config.settingsOptions);
        }

        beginTest ("Linux installer accepts an omitted optional plugin");
        {
            auto root = makeTempSettingsDirectory();
            osci::LinuxInstaller::Config config;
            config.dataHome = root.getChildFile ("data");
            config.settingsOptions = makeTempSettingsOptions ("linux-installer-missing-plugin");
            config.refreshDesktopCaches = false;
            osci::LinuxInstaller installer (config);
            osci::LinuxInstaller::Request request;
            request.manifest = makeOsciRenderManifest();
            request.archive = makeLinuxArchive (root, false, true);
            request.locations = { root.getChildFile ("bin"), root.getChildFile ("vst3") };
            const auto staleInstrument = request.locations.vst3Directory
                .getChildFile ("osci-render-instrument.vst3/Contents/" + testLinuxArchitectureDirectory()
                               + "/osci-render-instrument.so");
            expect (staleInstrument.getParentDirectory().createDirectory());
            expect (staleInstrument.replaceWithText ("old instrument"));
            osci::LinuxInstaller::Report report;
            const auto result = installer.install (request, report);
            expect (result.wasOk(), result.getErrorMessage());
            expect (request.locations.vst3Directory.getChildFile ("osci-render.vst3").isDirectory());
            expect (!request.locations.vst3Directory.getChildFile ("osci-render-instrument.vst3").exists());
            root.deleteRecursively();
            deleteTempSettings (*config.settingsOptions);
        }

        beginTest ("Linux installer rejects an optional plugin for the wrong architecture");
        {
            auto root = makeTempSettingsDirectory();
            osci::LinuxInstaller::Config config;
            config.dataHome = root.getChildFile ("data");
            config.settingsOptions = makeTempSettingsOptions ("linux-installer-wrong-optional-architecture");
            config.refreshDesktopCaches = false;
            osci::LinuxInstaller::Request request;
            request.manifest = makeOsciRenderManifest();
            request.archive = makeLinuxArchive (root, false, false, true);
            request.locations = { root.getChildFile ("bin"), root.getChildFile ("vst3") };
            osci::LinuxInstaller::Report report;
            const auto result = osci::LinuxInstaller (config).install (request, report);
            expect (result.failed());
            expect (result.getErrorMessage().contains ("plugin binary"));
            root.deleteRecursively();
            deleteTempSettings (*config.settingsOptions);
        }

        beginTest ("Linux installer does not recreate unavailable saved locations");
        {
            auto root = makeTempSettingsDirectory();
            osci::LinuxInstaller::Config config;
            config.dataHome = root.getChildFile ("data");
            config.settingsOptions = makeTempSettingsOptions ("linux-installer-unavailable-location");
            config.refreshDesktopCaches = false;
            osci::LinuxInstaller::Request request;
            request.manifest = makeOsciRenderManifest();
            request.archive = makeLinuxArchive (root);
            request.locations = { root.getChildFile ("missing/bin"), root.getChildFile ("missing/vst3") };
            request.missingDirectoryPolicy = osci::LinuxInstaller::MissingDirectoryPolicy::Reject;
            osci::LinuxInstaller::Report report;
            const auto result = osci::LinuxInstaller (config).install (request, report);
            expect (result.failed());
            expect (!request.locations.standaloneDirectory.exists());
            expect (!request.locations.vst3Directory.exists());
            root.deleteRecursively();
            deleteTempSettings (*config.settingsOptions);
        }
#endif

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
            expect(osci::DawProcessDetector::isKnownDawProcessName(
                "/Applications/Ableton Live 12 Suite.app/Contents/MacOS/Ableton Live 12 Suite", &display));
            expectEquals(display, juce::String("Ableton Live"));
            expect(osci::DawProcessDetector::isKnownDawProcessName(
                "/Applications/Logic Pro X.app/Contents/MacOS/Logic Pro X", &display));
            expectEquals(display, juce::String("Logic Pro"));
            expect(osci::DawProcessDetector::isKnownDawProcessName(
                "C:\\Program Files\\Steinberg\\Cubase 13\\Cubase13.exe", &display));
            expectEquals(display, juce::String("Cubase"));
            expect(osci::DawProcessDetector::isKnownDawProcessName("pluginval", &display));
            expectEquals(display, juce::String("pluginval"));
            expect(osci::DawProcessDetector::isKnownDawProcessName("VST3PluginTestHost.exe", &display));
            expectEquals(display, juce::String("VST3 plugin test host"));
            expect(osci::DawProcessDetector::isKnownDawProcessName("auval", &display));
            expectEquals(display, juce::String("AU validation tool"));
            expect(!osci::DawProcessDetector::isKnownDawProcessName(
                "/Applications/Logic Pro X.app/Contents/PlugIns/LogicProThumbnailExtension.appex/Contents/MacOS/LogicProThumbnailExtension"));
            expect(!osci::DawProcessDetector::isKnownDawProcessName("LogicProThumbnailExtension"));
            expect(!osci::DawProcessDetector::isKnownDawProcessName("Final Cut Pro"));
        }

        beginTest ("Hardware helpers return stable local values");
        expect (osci::HardwareInfo::getCurrentPlatform().isNotEmpty());
        expectEquals (osci::HardwareInfo::getDefaultStorageDirectory ("osci-render").getFileName(),
                      juce::String ("osci-render"));

        resetTestVerifier();
    }
};

static LicensingModuleTest licensingModuleTest;
