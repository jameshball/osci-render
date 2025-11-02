#include "VisualiserComponent.h"

#include "../CommonPluginEditor.h"
#include "../CommonPluginProcessor.h"
#include "../LookAndFeel.h"

VisualiserComponent::VisualiserComponent(
    CommonAudioProcessor &processor,
    CommonPluginEditor &pluginEditor,
#if OSCI_PREMIUM
    SharedTextureManager &sharedTextureManager,
#endif
    juce::File ffmpegFile,
    VisualiserSettings &settings,
    RecordingSettings &recordingSettings,
    VisualiserComponent *parent,
    bool visualiserOnly) : VisualiserRenderer(settings.parameters, processor.threadManager),
                           settings(settings),
                           audioProcessor(processor),
                           ffmpegFile(ffmpegFile),
#if OSCI_PREMIUM
                           sharedTextureManager(sharedTextureManager),
                           ffmpegEncoderManager(ffmpegFile),
#endif
                           recordingSettings(recordingSettings),
                           visualiserOnly(visualiserOnly),
                           parent(parent),
                           editor(pluginEditor) {
    setShouldBeRunning(true);

#if OSCI_PREMIUM
    addAndMakeVisible(editor.ffmpegDownloader);
#endif

    audioProcessor.haltRecording = [this] {
        setRecording(false);
    };

    addAndMakeVisible(record);
#if OSCI_PREMIUM
    record.setTooltip("Toggles recording of the oscilloscope's visuals and audio.");
#else
    record.setTooltip("Toggles recording of the audio.");
#endif
    record.setPulseAnimation(true);
    record.onClick = [this] {
        setRecording(record.getToggleState());
    };

    addAndMakeVisible(stopwatch);

    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus(true);

    if (parent == nullptr) {
        addAndMakeVisible(fullScreenButton);
        fullScreenButton.setTooltip("Toggles fullscreen mode.");
    }
    if (child == nullptr && parent == nullptr) {
        addAndMakeVisible(popOutButton);
        popOutButton.setTooltip("Opens the oscilloscope in a new window.");
    }
    addAndMakeVisible(settingsButton);
    settingsButton.setTooltip("Opens the visualiser settings window.");

    addAndMakeVisible(sharedTextureButton);
#if OSCI_PREMIUM
    sharedTextureButton.setTooltip("Toggles sending the oscilloscope's visuals to a Syphon/Spout receiver.");
    sharedTextureButton.onClick = [this] {
        if (sharedTextureSender != nullptr) {
            openGLContext.executeOnGLThread([this](juce::OpenGLContext &context) { closeSharedTexture(); },
                                            false);
        } else {
            openGLContext.executeOnGLThread([this](juce::OpenGLContext &context) { initialiseSharedTexture(); },
                                            false);
        }
    };
#else
    sharedTextureButton.setTooltip("Live video input via Syphon/Spout is a Premium feature. Click to learn more.");
    sharedTextureButton.setClickingTogglesState(false);
    sharedTextureButton.setToggleState(false, juce::NotificationType::dontSendNotification);
    sharedTextureButton.onClick = [this]() {
        editor.showPremiumSplashScreen();
    };
#endif

    fullScreenButton.onClick = [this]() {
        enableFullScreen();
    };

    settingsButton.onClick = [this]() {
        if (openSettings != nullptr) {
            openSettings();
        }
    };

    popOutButton.onClick = [this]() {
        popoutWindow();
    };

    if (visualiserOnly && juce::JUCEApplication::isStandaloneApp()) {
        addAndMakeVisible(audioInputButton);
        audioInputButton.setTooltip("Appears red when audio input is being used. Click to enable audio input and close any open audio files.");
        audioInputButton.setClickingTogglesState(false);
        audioInputButton.setToggleState(!audioPlayer.isInitialised(), juce::NotificationType::dontSendNotification);
        audioPlayer.onParserChanged = [this] {
            juce::MessageManager::callAsync([this] { audioInputButton.setToggleState(!audioPlayer.isInitialised(), juce::NotificationType::dontSendNotification); });
        };
        audioInputButton.onClick = [this] {
            audioProcessor.stopAudioFile();
        };
    }

    addChildComponent(audioPlayer);
    audioPlayer.setVisible(visualiserOnly);
    audioPlayer.addMouseListener(static_cast<juce::Component *>(this), true);

    preRenderCallback = [this] {
        if (!record.getToggleState()) {
            updateRenderModeFromProcessor();
            setResolution(this->recordingSettings.getResolution());
            setFrameRate(this->recordingSettings.getFrameRate());
        }
    };

    postRenderCallback = [this] {
#if OSCI_PREMIUM
        if (sharedTextureSender != nullptr) {
            sharedTextureSender->renderGL();
        }
#endif

        if (record.getToggleState()) {
#if OSCI_PREMIUM
            if (recordingVideo) {
                // draw frame to ffmpeg
                Texture renderTexture = getRenderTexture();
                getFrame(framePixels);
                if (ffmpegProcess.write(framePixels.data(), 4 * renderTexture.width * renderTexture.height, 3000) == 0) {
                    record.setToggleState(false, juce::NotificationType::dontSendNotification);

                    juce::MessageManager::callAsync([this] {
                        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                            "Recording Error",
                            "An error occurred while writing the video frame to the ffmpeg process. Recording has been stopped.",
                            "OK");
                    });
                }
            }
#endif
            if (recordingAudio) {
                audioRecorder.audioThreadCallback(audioOutputBuffer);
            }
        }
        
        stopwatch.addTime(juce::RelativeTime::seconds(1.0 / this->recordingSettings.getFrameRate()));
    };
}

VisualiserComponent::~VisualiserComponent() {
    setRecording(false);
    if (parent == nullptr) {
        audioProcessor.haltRecording = nullptr;
    }
}

void VisualiserComponent::setFullScreen(bool fullScreen) {
    this->fullScreen = fullScreen;
    hideButtonRow = false;
    setMouseCursor(juce::MouseCursor::PointingHandCursor);

    // Release renderingSemaphore to prevent deadlocks during layout changes
    renderingSemaphore.release();

    resized();
}

void VisualiserComponent::setFullScreenCallback(std::function<void(FullScreenMode)> callback) {
    fullScreenCallback = callback;
}

void VisualiserComponent::enableFullScreen() {
    if (fullScreenCallback) {
        fullScreenCallback(FullScreenMode::TOGGLE);
    }
    grabKeyboardFocus();
}

void VisualiserComponent::mouseDoubleClick(const juce::MouseEvent &event) { 
    if (event.originalComponent == this) {
        enableFullScreen();
    }
}

int VisualiserComponent::prepareTask(double sampleRate, int bufferSize) {
    int desiredBufferSize = VisualiserRenderer::prepareTask(sampleRate, bufferSize);
    audioRecorder.setSampleRate(sampleRate);

    return desiredBufferSize;
}

void VisualiserComponent::stopTask() {
    setRecording(false);
    VisualiserRenderer::stopTask();
}

void VisualiserComponent::setPaused(bool paused, bool affectAudio) {
    active = !paused;
    setShouldBeRunning(active);
    renderingSemaphore.release();
    if (affectAudio) {
        audioPlayer.setPaused(paused);
    }
    repaint();
}

void VisualiserComponent::mouseDrag(const juce::MouseEvent &event) {
    timerId = -1;
}

void VisualiserComponent::mouseMove(const juce::MouseEvent &event) {
    if (event.getScreenX() == lastMouseX && event.getScreenY() == lastMouseY) {
        return;
    }
    hideButtonRow = false;
    setMouseCursor(juce::MouseCursor::PointingHandCursor);

    // Treat both fullScreen mode and pop-out mode (parent != nullptr) as needing auto-hide controls
    if (fullScreen || parent != nullptr) {
        if (!getScreenBounds().removeFromBottom(25).contains(event.getScreenX(), event.getScreenY()) && !event.mods.isLeftButtonDown()) {
            lastMouseX = event.getScreenX();
            lastMouseY = event.getScreenY();

            int newTimerId = juce::Random::getSystemRandom().nextInt();
            timerId = newTimerId;
            auto pos = event.getScreenPosition();
            auto parent = this->parent;

            juce::WeakReference<VisualiserComponent> weakRef = this;
            juce::Timer::callAfterDelay(1000, [this, weakRef, newTimerId, pos, parent]() {
                if (weakRef) {
                    if (parent == nullptr || parent->child == this) {
                        // Check both fullscreen or pop-out mode
                        if (timerId == newTimerId && (fullScreen || this->parent != nullptr)) {
                            hideButtonRow = true;
                            setMouseCursor(juce::MouseCursor::NoCursor);
                            resized();
                        }
                    }
                } });
        }
        resized();
    }
}

void VisualiserComponent::mouseDown(const juce::MouseEvent &event) {
    if (event.originalComponent == this) {
        if (event.mods.isLeftButtonDown() && child == nullptr && !record.getToggleState()) {
            setPaused(active);
        }
    }
}

bool VisualiserComponent::keyPressed(const juce::KeyPress &key) {
    // If we're not accepting special keys, end early
    if (!audioProcessor.getAcceptsKeys()) return false;

    if (key.isKeyCode(juce::KeyPress::escapeKey)) {
        if (fullScreenCallback) {
            fullScreenCallback(FullScreenMode::MAIN_COMPONENT);
        }
        return true;
    } else if (key.isKeyCode(juce::KeyPress::spaceKey)) {
        setPaused(active);
        return true;
    }

    return false;
}

void VisualiserComponent::setRecording(bool recording) {
    stopwatch.stop();
    stopwatch.reset();

#if OSCI_PREMIUM
    bool stillRecording = ffmpegProcess.isRunning() || audioRecorder.isRecording();
#else
    bool stillRecording = audioRecorder.isRecording();
#endif

    // Release renderingSemaphore to prevent deadlock
    renderingSemaphore.release();

    if (recording) {
        RenderMode mode = getRenderMode();
        recordingChannelCount = VisualiserRenderer::getChannelCountForRenderMode(mode);
        audioRecorder.setNumChannels(recordingChannelCount);
#if OSCI_PREMIUM
        recordingVideo = recordingSettings.recordingVideo();
        recordingAudio = recordingSettings.recordingAudio();
        if (!recordingVideo && !recordingAudio) {
            record.setToggleState(false, juce::NotificationType::dontSendNotification);
            return;
        }

        if (recordingVideo) {
            auto onDownloadSuccess = [this] {
                juce::MessageManager::callAsync([this] {
                    record.setEnabled(true);
                    juce::Timer::callAfterDelay(3000, [this] {
                        juce::MessageManager::callAsync([this] {
                            editor.ffmpegDownloader.setVisible(false);
                            downloading = false;
                            resized();
                        });
                    }); });
            };
            auto onDownloadStart = [this] {
                juce::MessageManager::callAsync([this] {
                    record.setEnabled(false);
                    downloading = true;
                    resized(); });
            };
            if (!audioProcessor.ensureFFmpegExists(onDownloadStart, onDownloadSuccess)) {
                record.setToggleState(false, juce::NotificationType::dontSendNotification);
                return;
            }

            // Get the appropriate file extension based on codec
            juce::String fileExtension = recordingSettings.getFileExtensionForCodec();
            tempVideoFile = std::make_unique<juce::TemporaryFile>("." + fileExtension);

            VideoCodec codec = recordingSettings.getVideoCodec();
            juce::String cmd = ffmpegEncoderManager.buildVideoEncodingCommand(
                codec,
                recordingSettings.getCRF(),
                getRenderWidth(),
                getRenderHeight(),
                recordingSettings.getFrameRate(),
                recordingSettings.getCompressionPreset(),
                tempVideoFile->getFile());

            if (!ffmpegProcess.start(cmd)) {
                record.setToggleState(false, juce::NotificationType::dontSendNotification);
                return;
            }
            framePixels.resize(getRenderWidth() * getRenderHeight() * 4);
        }

        if (recordingAudio) {
            tempAudioFile = std::make_unique<juce::TemporaryFile>(".wav");
            audioRecorder.startRecording(tempAudioFile->getFile());
        }
#else
        // audio only recording
        tempAudioFile = std::make_unique<juce::TemporaryFile>(".wav");
        audioRecorder.startRecording(tempAudioFile->getFile());
#endif

        setPaused(false);
        stopwatch.start();
    } else if (stillRecording) {
#if OSCI_PREMIUM
        bool wasRecordingAudio = recordingAudio;
        bool wasRecordingVideo = recordingVideo;
        recordingAudio = false;
        recordingVideo = false;

        juce::String extension = wasRecordingVideo ? recordingSettings.getFileExtensionForCodec() : "wav";
        if (wasRecordingAudio) {
            audioRecorder.stop();
        }
        if (wasRecordingVideo) {
            ffmpegProcess.close();
        }
#else
        audioRecorder.stop();
        juce::String extension = "wav";
#endif
        chooser = std::make_unique<juce::FileChooser>("Save recording", audioProcessor.getLastOpenedDirectory(), "*." + extension);
        auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting;

#if OSCI_PREMIUM
        chooser->launchAsync(flags, [this, wasRecordingAudio, wasRecordingVideo, extension](const juce::FileChooser &chooser) {
            auto file = chooser.getResult();
            if (file != juce::File()) {
                // Ensure the file has the correct extension
                if (!file.hasFileExtension(extension)) {
                    file = file.withFileExtension(extension);
                }
                
                if (wasRecordingAudio && wasRecordingVideo) {
                    // delete the file if it exists
                    if (file.existsAsFile()) {
                        file.deleteFile();
                    }
                    ffmpegProcess.start("\"" + ffmpegFile.getFullPathName() + "\" -i \"" + tempVideoFile->getFile().getFullPathName() + "\" -i \"" + tempAudioFile->getFile().getFullPathName() + "\" -c:v copy -c:a aac -b:a 384k -y \"" + file.getFullPathName() + "\"");
                    ffmpegProcess.close();
                } else if (wasRecordingAudio) {
                    tempAudioFile->getFile().copyFileTo(file);
                } else if (wasRecordingVideo) {
                    tempVideoFile->getFile().copyFileTo(file);
                }
                audioProcessor.setLastOpenedDirectory(file.getParentDirectory());
            } });
#else
        chooser->launchAsync(flags, [this, extension](const juce::FileChooser &chooser) {
            auto file = chooser.getResult();
            if (file != juce::File()) {
                // Ensure the file has the correct extension
                if (!file.hasFileExtension(extension)) {
                    file = file.withFileExtension(extension);
                }
                
                tempAudioFile->getFile().copyFileTo(file);
                audioProcessor.setLastOpenedDirectory(file.getParentDirectory());
            } });
#endif
    }

    setBlockOnAudioThread(recording);
#if OSCI_PREMIUM
    numFrames = 0;
#endif
    record.setToggleState(recording, juce::NotificationType::dontSendNotification);
    resized();
}

void VisualiserComponent::resized() {
    auto area = getLocalBounds();
    // Apply hideButtonRow logic to both fullscreen and pop-out modes
    if ((fullScreen || parent != nullptr) && hideButtonRow) {
        buttonRow = area.removeFromBottom(0);
    } else {
        buttonRow = area.removeFromBottom(25);
    }
    auto buttons = buttonRow;
    if (parent == nullptr) {
        fullScreenButton.setBounds(buttons.removeFromRight(30));
    }
    if (child == nullptr && parent == nullptr) {
        popOutButton.setBounds(buttons.removeFromRight(30));
    }
    if (openSettings != nullptr) {
        settingsButton.setVisible(true);
        settingsButton.setBounds(buttons.removeFromRight(30));
    } else {
        settingsButton.setVisible(false);
    }

    if (visualiserOnly && juce::JUCEApplication::isStandaloneApp() && child == nullptr) {
        audioInputButton.setBounds(buttons.removeFromRight(30));
    }

    sharedTextureButton.setBounds(buttons.removeFromRight(30));

    record.setBounds(buttons.removeFromRight(25));
    if (record.getToggleState()) {
        stopwatch.setVisible(true);
        stopwatch.setBounds(buttons.removeFromRight(100));
    } else {
        stopwatch.setVisible(false);
    }

#if OSCI_PREMIUM
    if (child == nullptr && downloading) {
        auto bounds = buttons.removeFromRight(160);
        editor.ffmpegDownloader.setBounds(bounds.withSizeKeepingCentre(bounds.getWidth() - 10, bounds.getHeight() - 10));
    }
#endif

    buttons.removeFromRight(10); // padding

    if (child == nullptr) {
        audioPlayer.setBounds(buttons);
    }

    setViewportArea(area);
}

void VisualiserComponent::popoutWindow() {
#if OSCI_PREMIUM
    if (sharedTextureButton.getToggleState()) {
        sharedTextureButton.triggerClick();
    }
#endif
    setRecording(false);

    // Release renderingSemaphore to prevent deadlock when creating a child visualizer
    renderingSemaphore.release();

    auto visualiser = new VisualiserComponent(
        audioProcessor,
        editor,
#if OSCI_PREMIUM
        sharedTextureManager,
#endif
        ffmpegFile,
        settings,
        recordingSettings,
        this,
        visualiserOnly);
    visualiser->settings.setLookAndFeel(&getLookAndFeel());
    visualiser->openSettings = openSettings;
    visualiser->closeSettings = closeSettings;
    // Pop-out visualiser is created with parent set to this component
    child = visualiser;
    childUpdated();
    visualiser->setSize(350, 350);
    popout = std::make_unique<VisualiserWindow>("Software Oscilloscope", this);
    popout->setContentOwned(visualiser, true);
    popout->setUsingNativeTitleBar(true);
    popout->setResizable(true, false);
    popout->setVisible(true);
    popout->centreWithSize(350, 350);
    setPaused(true, false);
    resized();
}

void VisualiserComponent::childUpdated() {
    popOutButton.setVisible(child == nullptr);
#if OSCI_PREMIUM
    editor.ffmpegDownloader.setVisible(child == nullptr);
#endif
    record.setVisible(child == nullptr);
    audioPlayer.setVisible(child == nullptr);
    if (child != nullptr) {
        audioProcessor.haltRecording = [this] {
            setRecording(false);
            child->setRecording(false);
        };
    } else {
        audioPlayer.setup();
        audioProcessor.haltRecording = [this] {
            setRecording(false);
        };
    }
}

void VisualiserComponent::updateRenderModeFromProcessor() {
    // Called on message thread
    if (!visualiserOnly) {
        setRenderMode(RenderMode::XY);
        return;
    }
    // Determine based on whether brightness and RGB are enabled and not force-disabled
    bool brightnessAllowed = !audioProcessor.getForceDisableBrightnessInput();
    bool rgbAllowed = !audioProcessor.getForceDisableRgbInput();
    // Prefer RGB if we have 4th/5th channels effectively
    if (rgbAllowed && audioProcessor.isRgbEnabled()) {
        setRenderMode(RenderMode::XYRGB);
    } else if (brightnessAllowed && audioProcessor.isBrightnessEnabled()) {
        setRenderMode(RenderMode::XYZ);
    } else {
        setRenderMode(RenderMode::XY);
    }
}

#if OSCI_PREMIUM
void VisualiserComponent::initialiseSharedTexture() {
    Texture renderTexture = getRenderTexture();
    sharedTextureSender = sharedTextureManager.addSender(recordingSettings.getCustomSharedTextureServerName(), renderTexture.width, renderTexture.height);
    sharedTextureSender->initGL();
    sharedTextureSender->setSharedTextureId(renderTexture.id);
    sharedTextureSender->setDrawFunction([this] { drawFrame(); });
}

void VisualiserComponent::closeSharedTexture() {
    if (sharedTextureSender != nullptr) {
        sharedTextureManager.removeSender(sharedTextureSender);
        sharedTextureSender = nullptr;
    }
}
#endif

void VisualiserComponent::openGLContextClosing() {
#if OSCI_PREMIUM
    closeSharedTexture();
#endif

    VisualiserRenderer::openGLContextClosing();
}

void VisualiserComponent::paint(juce::Graphics &g) {
    bool colourSpecified = isColourSpecified(buttonRowColourId);
    auto buttonRowColour = Colours::veryDark;
    if (colourSpecified) {
        buttonRowColour = findColour(buttonRowColourId, true);
    }
    g.setColour(buttonRowColour);
    g.fillRect(buttonRow);
    if (!active) {
        // draw a translucent overlay
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRect(getViewportArea());

        g.setColour(juce::Colours::white);
        g.setFont(30.0f);
        juce::String text = child == nullptr ? "Paused" : "Open in another window";
        g.drawText(text, getViewportArea(), juce::Justification::centred);
    }
}
