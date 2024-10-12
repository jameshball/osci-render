#include "../LookAndFeel.h"
#include "VisualiserComponent.h"

VisualiserComponent::VisualiserComponent(SampleRateManager& sampleRateManager, ConsumerManager& consumerManager, VisualiserSettings& settings, VisualiserComponent* parent, bool useOldVisualiser, bool visualiserOnly) : settings(settings), backgroundColour(juce::Colours::black), waveformColour(juce::Colour(0xff00ff00)), sampleRateManager(sampleRateManager), consumerManager(consumerManager), oldVisualiser(useOldVisualiser), visualiserOnly(visualiserOnly), juce::Thread("VisualiserComponent"), parent(parent) {
    resetBuffer();
    if (!oldVisualiser) {
        initialiseBrowser();
    }
    startTimerHz(60);
    startThread();
    
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus(true);
    
    roughness.textBox.setValue(settings.parameters.roughness);
    roughness.textBox.onValueChange = [this]() {
        this->settings.parameters.roughness = (int) roughness.textBox.getValue();
    };
    intensity.textBox.setValue(settings.parameters.intensity);
    intensity.textBox.onValueChange = [this]() {
        this->settings.parameters.intensity = intensity.textBox.getValue();
    };
    
    if (parent == nullptr) {
        addChildComponent(fullScreenButton);
    }
    if (child == nullptr && parent == nullptr) {
        addChildComponent(popOutButton);
    }
    addChildComponent(settingsButton);
    
    fullScreenButton.onClick = [this]() {
        enableFullScreen();
    };
    
    settingsButton.onClick = [this]() {
        juce::PopupMenu menu;

        menu.addCustomItem(1, roughness, 160, 40, false);
        menu.addCustomItem(1, intensity, 160, 40, false);

        menu.showMenuAsync(juce::PopupMenu::Options(), [this](int result) {});
    };
    
    popOutButton.onClick = [this]() {
        popoutWindow();
    };

    setFullScreen(false);
}

VisualiserComponent::~VisualiserComponent() {
    {
        juce::CriticalSection::ScopedLockType scope(consumerLock);
        consumerManager.consumerStop(consumer);
    }
    stopThread(1000);
    masterReference.clear();
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

void VisualiserComponent::mouseDoubleClick(const juce::MouseEvent& event) {
    enableFullScreen();
}

void VisualiserComponent::setBuffer(std::vector<Point>& newBuffer) {
    juce::CriticalSection::ScopedLockType scope(lock);
    buffer.clear();
    int stride = oldVisualiser ? roughness.textBox.getValue() : 1;
    for (int i = 0; i < newBuffer.size(); i += stride) {
        buffer.push_back(newBuffer[i]);
    }
}

void VisualiserComponent::setColours(juce::Colour bk, juce::Colour fg) {
    backgroundColour = bk;
    waveformColour = fg;
}

void VisualiserComponent::paint(juce::Graphics& g) {
    if (oldVisualiser) {
        g.setColour(backgroundColour);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), OscirenderLookAndFeel::RECT_RADIUS);
        
        auto r = getLocalBounds().toFloat();
        auto minDim = juce::jmin(r.getWidth(), r.getHeight());
        
        {
            juce::CriticalSection::ScopedLockType scope(lock);
            if (buffer.size() > 0) {
                g.setColour(waveformColour);
                paintXY(g, r.withSizeKeepingCentre(minDim, minDim));
            }
        }
        
        
        if (!active) {
            // add translucent layer
            g.setColour(juce::Colours::black.withAlpha(0.5f));
            g.fillRoundedRectangle(getLocalBounds().toFloat(), OscirenderLookAndFeel::RECT_RADIUS);
            
            // add text
            g.setColour(juce::Colours::white);
            g.setFont(14.0f);
            auto text = child != nullptr ? "Open in separate window" : "Paused";
            g.drawFittedText(text, getLocalBounds(), juce::Justification::centred, 1);
        }
    }
}

void VisualiserComponent::timerCallback() {
    if (oldVisualiser) {
        repaint();
    }
}

void VisualiserComponent::run() {
    while (!threadShouldExit()) {
        if (sampleRate != (int) sampleRateManager.getSampleRate()) {
            resetBuffer();
        }
        
        {
            juce::CriticalSection::ScopedLockType scope(consumerLock);
            consumer = consumerManager.consumerRegister(tempBuffer);
        }
        consumerManager.consumerRead(consumer);
        
        setBuffer(tempBuffer);
        if (!oldVisualiser) {
            audioUpdated = true;
            triggerAsyncUpdate();
        }
    }
}

void VisualiserComponent::setPaused(bool paused) {
    active = !paused;
    if (active) {
        startTimerHz(60);
        startThread();
    } else {
        {
            juce::CriticalSection::ScopedLockType scope(consumerLock);
            consumerManager.consumerStop(consumer);
        }
        stopTimer();
        stopThread(1000);
    }
    repaint();
}

void VisualiserComponent::mouseDown(const juce::MouseEvent& event) {
    if (!oldVisualiser) return;
    if (event.mods.isLeftButtonDown() && child == nullptr) {
        setPaused(active);
    }
}

void VisualiserComponent::mouseMove(const juce::MouseEvent& event) {
    if (!oldVisualiser) return;
    if (event.getScreenX() == lastMouseX && event.getScreenY() == lastMouseY) {
        return;
    }
    lastMouseX = event.getScreenX();
    lastMouseY = event.getScreenY();
    
    int newTimerId = juce::Random::getSystemRandom().nextInt();
    timerId = newTimerId;
    if (parent == nullptr) {
        fullScreenButton.setVisible(true);
    }
    if (child == nullptr && parent == nullptr) {
        popOutButton.setVisible(true);
    }
    settingsButton.setVisible(true);
    auto pos = event.getScreenPosition();
    auto parent = this->parent;
    
    juce::Timer::callAfterDelay(1000, [this, newTimerId, pos, parent]() {
        if (parent == nullptr || parent->child == this) {
            bool onButtonRow = settingsButton.getScreenBounds().contains(pos);
            if (parent == nullptr) {
                onButtonRow |= fullScreenButton.getScreenBounds().contains(pos);
            }
            if (child == nullptr && parent == nullptr) {
                onButtonRow |= popOutButton.getScreenBounds().contains(pos);
            }
            if (timerId == newTimerId && !onButtonRow) {
                fullScreenButton.setVisible(false);
                popOutButton.setVisible(false);
                settingsButton.setVisible(false);
                repaint();
            }
        }
    });
    repaint();
}

bool VisualiserComponent::keyPressed(const juce::KeyPress& key) {
    if (key.isKeyCode(juce::KeyPress::escapeKey)) {
        if (fullScreenCallback) {
            fullScreenCallback(FullScreenMode::MAIN_COMPONENT);
        }
        return true;
    }

    return false;
}

void VisualiserComponent::setFullScreen(bool fullScreen) {}

void VisualiserComponent::setVisualiserType(bool oldVisualiser) {
    this->oldVisualiser = oldVisualiser;
    if (child != nullptr) {
        child->setVisualiserType(oldVisualiser);
    }
    if (oldVisualiser) {
        oldBrowser = std::move(browser);
        if (oldBrowser != nullptr) {
            removeChildComponent(oldBrowser.get());
            oldBrowser->goToURL("about:blank");
        }
        if (closeSettings != nullptr) {
            closeSettings();
        }
    } else {
        initialiseBrowser();
    }
}

void VisualiserComponent::paintXY(juce::Graphics& g, juce::Rectangle<float> area) {
    auto transform = juce::AffineTransform::fromTargetPoints(-1.0f, -1.0f, area.getX(), area.getBottom(), 1.0f, 1.0f, area.getRight(), area.getY(), 1.0f, -1.0f, area.getRight(), area.getBottom());
    std::vector<juce::Line<float>> lines;

    for (int i = 2; i < buffer.size(); i++) {
        lines.emplace_back(buffer[i - 1].x, buffer[i - 1].y, buffer[i].x, buffer[i].y);
    }

    double strength = 15;
    double widthDivisor = 160;
    double lengthIntensityScale = 700;
    juce::Colour waveColor = waveformColour;

    for (auto& line : lines) {
        double thickness = area.getWidth() / widthDivisor;
        float normalisedLength = line.getLength() * (sampleRate / DEFAULT_SAMPLE_RATE) / roughness.textBox.getValue();
        line.applyTransform(transform);
        double beamIntensity = intensity.textBox.getValue();
        double lengthScale = (lengthIntensityScale * 0.5 + lengthIntensityScale * (1 - beamIntensity)) * (normalisedLength + 0.001);
        double lengthScaleLog = std::log(strength * (1 / lengthScale) + 1) / std::log(strength + 1);
        g.setColour(waveColor.withAlpha((float) std::max(0.0, std::min(lengthScaleLog * beamIntensity, 1.0))).withSaturation(lengthScale / 4));
        
        if (normalisedLength < 0.00001) {
            g.fillEllipse(line.getStartX(), line.getStartY(), thickness, thickness);
        } else {
            g.drawLine(line, thickness);
        }
    }
}

void VisualiserComponent::initialiseBrowser() {
    if (recordingHalted != nullptr) {
        recordingHalted();
    }
    oldBrowser = std::move(browser);
    if (oldBrowser != nullptr) {
        removeChildComponent(oldBrowser.get());
        oldBrowser->goToURL("about:blank");
    }
    
    browser = std::make_unique<juce::WebBrowserComponent>(
        juce::WebBrowserComponent::Options()
        .withNativeIntegrationEnabled()
        .withResourceProvider(provider)
        .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withWinWebView2Options(
            juce::WebBrowserComponent::Options::WinWebView2{}
            .withUserDataFolder(juce::File::getSpecialLocation(juce::File::SpecialLocationType::userApplicationDataDirectory).getChildFile("osci-render"))
            .withStatusBarDisabled()
            .withBuiltInErrorPageDisabled()
            .withBackgroundColour(Colours::dark)
        )
        .withNativeFunction("toggleFullscreen", [this](auto& var, auto complete) {
            enableFullScreen();
        })
        .withNativeFunction("popout", [this](auto& var, auto complete) {
            popoutWindow();
        })
        .withNativeFunction("settings", [this](auto& var, auto complete) {
            openSettings();
        })
        .withNativeFunction("isDebug", [this](auto& var, auto complete) {
#if JUCE_DEBUG
            complete(true);
#else
            complete(false);
#endif
        })
        .withNativeFunction("isOverlay", [this](auto& var, auto complete) {
            complete(parent != nullptr);
        })
        .withNativeFunction("pause", [this](auto& var, auto complete) {
            setPaused(active);
        })
        .withNativeFunction("getSettings", [this](auto& var, auto complete) {
            complete(settings.getSettings());
        })
        .withNativeFunction("bufferSize", [this](auto& var, auto complete) {
            complete((int) tempBuffer.size());
        })
        .withNativeFunction("sampleRate", [this](auto& var, auto complete) {
            complete(sampleRate);
        })
        .withNativeFunction("isVisualiserOnly", [this](auto& var, auto complete) {
            complete(visualiserOnly);
        })
        .withNativeFunction("downloadVideo", [this](const juce::Array<juce::var>& args, auto complete) {
            juce::String base64 = args[0].toString();
            chooser = std::make_unique<juce::FileChooser>("Save video", juce::File::getSpecialLocation(juce::File::SpecialLocationType::userDesktopDirectory).getChildFile("osci-render.webm"), "*.webm");
            chooser->launchAsync(juce::FileBrowserComponent::saveMode,
                [base64](const juce::FileChooser& chooser) {
                    juce::File result = chooser.getResult();
                    if (result.getFullPathName().isNotEmpty()) {
                        juce::FileOutputStream stream(result);
                        stream.setPosition(0);
                        stream.truncate();
                        juce::Base64::convertFromBase64(stream, base64);
                        stream.flush();
                    }
                });
        })
    );

    addAndMakeVisible(*browser);
    browser->goToURL(juce::WebBrowserComponent::getResourceProviderRoot() + "oscilloscope.html");
    resized();
}

void VisualiserComponent::resetBuffer() {
    sampleRate = (int) sampleRateManager.getSampleRate();
    tempBuffer = std::vector<Point>(sampleRate * BUFFER_LENGTH_SECS);
    if (!oldVisualiser && isShowing()) {
        restartBrowser = true;
        triggerAsyncUpdate();
    }
}

void VisualiserComponent::handleAsyncUpdate() {
    if (restartBrowser) {
        initialiseBrowser();
        restartBrowser = false;
    }
    if (audioUpdated && browser != nullptr) {
        juce::CriticalSection::ScopedLockType scope(lock);
        std::vector<float> rawBuffer;
        if (settings.numChannels == 2) {
            rawBuffer.reserve(buffer.size() * 2);
            for (auto& point : buffer) {
                rawBuffer.push_back(point.x);
                rawBuffer.push_back(point.y);
            }
        } else if (settings.numChannels == 3) {
            rawBuffer.reserve(buffer.size() * 3);
            for (auto& point : buffer) {
                rawBuffer.push_back(point.x);
                rawBuffer.push_back(point.y);
                rawBuffer.push_back(point.z);
            }
        }
        browser->emitEventIfBrowserIsVisible("audioUpdated", juce::Base64::toBase64(rawBuffer.data(), rawBuffer.size() * sizeof(float)));
        audioUpdated = false;
    }
}

void VisualiserComponent::toggleRecording() {
    if (oldVisualiser) {
        return;
    }
    browser->emitEventIfBrowserIsVisible("toggleRecording", juce::var());
}

void VisualiserComponent::resized() {
    if (!oldVisualiser) {
        browser->setBounds(getLocalBounds());
    }
    auto area = getLocalBounds();
    area.removeFromBottom(5);
    auto buttonRow = area.removeFromBottom(25);
    
    if (parent == nullptr) {
        fullScreenButton.setBounds(buttonRow.removeFromRight(30));
    }
    if (child == nullptr && parent == nullptr) {
        popOutButton.setBounds(buttonRow.removeFromRight(30));
    }
    settingsButton.setBounds(buttonRow.removeFromRight(30));
}

void VisualiserComponent::childChanged() {
    if (!oldVisualiser && browser != nullptr) {
        browser->emitEventIfBrowserIsVisible("childPresent", child != nullptr);
    }
}

void VisualiserComponent::popoutWindow() {
    if (recordingHalted != nullptr) {
        recordingHalted();
    }
    auto visualiser = new VisualiserComponent(sampleRateManager, consumerManager, settings, this, oldVisualiser);
    visualiser->settings.setLookAndFeel(&getLookAndFeel());
    visualiser->openSettings = openSettings;
    visualiser->closeSettings = closeSettings;
    visualiser->recordingHalted = recordingHalted;
    child = visualiser;
    childChanged();
    popOutButton.setVisible(false);
    visualiser->setSize(300, 300);
    popout = std::make_unique<VisualiserWindow>("Software Oscilloscope", this);
    popout->setContentOwned(visualiser, true);
    popout->setUsingNativeTitleBar(true);
    popout->setResizable(true, false);
    popout->setVisible(true);
    popout->centreWithSize(300, 300);
    setPaused(true);
    resized();
    popOutButton.setVisible(false);
}
