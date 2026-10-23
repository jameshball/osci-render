#pragma once

#include <JuceHeader.h>
#include "CommonPluginProcessor.h"
#include "visualiser/VisualiserComponent.h"
#include "LookAndFeel.h"
#include "audio/OutputClip.h"
#include "visualiser/VisualiserSettings.h"
#include "components/menu/SosciMainMenuBarModel.h"
#include <osci_gui/osci_gui.h>
#include "components/ProductUpdateConfig.h"

#if (DEBUG || OSCI_PROFILING) && JUCE_MODULE_AVAILABLE_jucewright
    #include <jucewright/jucewright.h>
#endif

class CommonPluginEditor : public juce::AudioProcessorEditor,
                           public juce::KeyListener,
                           public osci::OverlayHost {
public:
    CommonPluginEditor(CommonAudioProcessor&, juce::String appName, juce::String projectFileType, int width, int height);
    ~CommonPluginEditor() override;

    virtual void handleCommandLine(const juce::String& commandLine);
    bool openFile(const juce::File& file);
    void initialiseMenuBar(juce::MenuBarModel& menuBarModel);
    virtual void openProject(const juce::File& file);
    virtual void openProject();
    void saveProject();
    // Runs `write` once the project is ready to be written: a product can
    // first gather what it saves with it (osci-motion takes a picture).
    virtual void beforeSaving(std::function<void()> write) { write(); }
    void saveProjectAs();
    // The name Save As suggests for a project not yet saved, without extension.
    virtual juce::String suggestedProjectName() const { return {}; }
    // Where the update prompt goes, given its size: top right by default. A
    // product moves it off an OpenGL view, which would draw over it.
    virtual juce::Rectangle<int> updatePromptArea(int width, int height) const { return {getWidth() - width - 14, 42, width, height}; }
    virtual void resetWindowSizeAndPosition();
    void updateTitle();
    void fileUpdated(juce::String fileName);
    void openAudioSettings();
    void openLicenseAndUpdates();
    void openPrivacyAndTerms();
    void refreshBetaUpdatesButton();
    virtual void openRecordingSettings();
    virtual void showPremiumSplashScreen();

    // Overlay management — any component can show/dismiss full-editor overlays
    void showOverlay(std::unique_ptr<osci::OverlayComponent> overlay) override;
    virtual void dismissOverlay(osci::OverlayComponent* overlay,
                                std::function<void()> beforeVisualiserRestore = nullptr);

    template<typename T>
    T* findActiveOverlay() {
        for (auto& o : activeOverlays)
            if (auto* found = dynamic_cast<T*>(o.get()))
                return found;
        return nullptr;
    }

    // Offline render: input audio file -> encoded video using Recording Settings
    void renderAudioFileToVideo();
    virtual void resetToDefault();
    // Called once the project is written to its file.
    virtual void projectSaved() {}
    void toggleFullScreen();
    bool isFullScreen();
    void resized() override;
    void parentHierarchyChanged() override;
    // Windows draws the editor with Direct2D unless this is on: then with
    // JUCE's software renderer, which screen capture that reads a window's
    // pixels (OBS's Window Capture on Automatic or BitBlt) can see; Direct2D
    // windows come out white there.
    bool usesSoftwareRendering() const;
    void setSoftwareRendering(bool software);

protected:
#if OSCI_PREMIUM
    // The title over an offline render's progress.
    virtual juce::String offlineRenderTitle() const { return "Render Audio File to Video"; }
    // Says an offline render to `outputFile` failed; `errorMessage` may be empty.
    virtual void showOfflineRenderFailure(const juce::String& errorMessage, const juce::File& outputFile);
    // Completion retains caller resources until the worker and OpenGL renderer
    // have stopped, and is told whether the render succeeded. It also runs
    // (unsuccessful) on failed launch/editor teardown: cleanup only. A beam,
    // drawn instead of the processor's visualiser parameters, must keep alive
    // whatever owns its parameters (VisualiserParameters does not): alias the
    // owner, as osci-motion does.
    bool startOfflineVideoRender(const juce::File& inputSignal, const juce::File& muxAudio, const juce::File& outputFile, VideoEncodingConfiguration encodingConfiguration, VisualiserRenderer::RenderMode initialRenderMode, std::function<void(bool succeeded)> completion = {}, std::shared_ptr<VisualiserParameters> beam = nullptr);
#endif
    virtual bool openSourceFile(const juce::File& file) = 0;

private:
    // Sets the window's renderer from the saved choice, without saving it.
    void applyRenderingEngine();

    CommonAudioProcessor& audioProcessor;
    int defaultEditorWidth = 0;
    int defaultEditorHeight = 0;
    bool betaUpdatesEnabled = false;
public:
    PluginLookAndFeel lookAndFeel;

    juce::String appName;
    juce::String projectFileType;
    juce::String currentFileName;

#if OSCI_PREMIUM
    DownloaderComponent ffmpegDownloader;
#endif

    VisualiserSettings visualiserSettings = VisualiserSettings(audioProcessor.visualiserParameters, 3, audioProcessor.recordingParameters);
    RecordingSettings recordingSettings = RecordingSettings(audioProcessor.recordingParameters, audioProcessor.visualiserParameters);
    VisualiserComponent visualiser{
        audioProcessor,
        *this,
        audioProcessor.applicationFolder.getChildFile(audioProcessor.ffmpegFileName),
        visualiserSettings,
        recordingSettings,
        appName == "sosci"
    };

    osci::VolumeComponent volume{
        audioProcessor.threadManager,
        *audioProcessor.volumeEffect->parameters[0],
        *audioProcessor.thresholdEffect->parameters[0],
        *audioProcessor.muteParameter,
        juce::String::createStringFromData(BinaryData::volume_svg, BinaryData::volume_svgSize),
        juce::String::createStringFromData(BinaryData::mute_svg, BinaryData::mute_svgSize),
        osci::kOutputClipBypassThreshold,
        osci::kOutputClipPeakEpsilon
    };
    juce::TextButton betaUpdatesButton { "Beta updates" };
    osci::UpdatePromptComponent updatePrompt {
        audioProcessor.licenseManager, osci::makeProductUpdateConfig()
    };

    std::unique_ptr<juce::FileChooser> chooser;
    juce::MenuBarComponent menuBar;
    juce::SharedResourcePointer<CustomTooltipWindow> tooltipWindow;

    osci::UndoRedoComponent undoRedoControls{
        audioProcessor.getUndoManager(),
        juce::String::createStringFromData(BinaryData::undo_svg, BinaryData::undo_svgSize),
        juce::String::createStringFromData(BinaryData::redo_svg, BinaryData::redo_svgSize)
    };

    bool usingNativeMenuBar = false;
    // A click away from a text field ends the typing in it.
    osci::ClickAwayUnfocuser clickAway {*this};

#if JUCE_LINUX
    juce::OpenGLContext openGlContext;
#endif

#if (DEBUG || OSCI_PROFILING) && JUCE_MODULE_AVAILABLE_jucewright
    jucewright::EnvironmentAutomation automation { *this };
#endif

    bool keyPressed(const juce::KeyPress& key) override;
    // KeyListener — catches shortcuts on the top-level component when no child has focus
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;

    void layoutBetaUpdatesButton(juce::Rectangle<int>& topBar);

protected:
    bool handleShortcut(const juce::KeyPress& key);
    juce::Component* topLevelKeyTarget = nullptr;
    std::vector<std::unique_ptr<osci::OverlayComponent>> activeOverlays;
    std::optional<bool> visualiserWasVisibleBeforeOverlay;
    std::optional<bool> offlineRenderPreviousActiveState;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CommonPluginEditor)
};
