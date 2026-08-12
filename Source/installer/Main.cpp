#include "InstallerWindow.h"

namespace {

osci::LookAndFeel::TypefaceData makeTypefaceData() {
    return {
        BinaryData::FiraSansRegular_ttf,
        BinaryData::FiraSansRegular_ttfSize,
        BinaryData::FiraSansSemiBold_ttf,
        BinaryData::FiraSansSemiBold_ttfSize,
        BinaryData::FiraSansItalic_ttf,
        BinaryData::FiraSansItalic_ttfSize,
    };
}

} // namespace

class OsciInstallerApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override {
        return ProjectInfo::projectName;
    }

    const juce::String getApplicationVersion() override {
        return ProjectInfo::versionString;
    }

    bool moreThanOneInstanceAllowed() override {
        return true;
    }

    void initialise (const juce::String&) override {
        lookAndFeel = std::make_unique<osci::LookAndFeel> (makeTypefaceData());
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());
        mainWindow = std::make_unique<osci::installer::InstallerWindow> (getApplicationName());
    }

    void shutdown() override {
        mainWindow = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel = nullptr;
    }

    void systemRequestedQuit() override {
        if (mainWindow == nullptr || mainWindow->canClose()) {
            quit();
        }
    }

    void anotherInstanceStarted (const juce::String&) override {
    }

private:
    std::unique_ptr<osci::LookAndFeel> lookAndFeel;
    std::unique_ptr<osci::installer::InstallerWindow> mainWindow;
};

START_JUCE_APPLICATION (OsciInstallerApplication)
