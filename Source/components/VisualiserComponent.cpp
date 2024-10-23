#include "../LookAndFeel.h"
#include "VisualiserComponent.h"

VisualiserComponent::VisualiserComponent(SampleRateManager& sampleRateManager, ConsumerManager& consumerManager, VisualiserSettings& settings, VisualiserComponent* parent, bool useOldVisualiser, bool visualiserOnly) : settings(settings), backgroundColour(juce::Colours::black), waveformColour(juce::Colour(0xff00ff00)), sampleRateManager(sampleRateManager), consumerManager(consumerManager), oldVisualiser(useOldVisualiser), visualiserOnly(visualiserOnly), juce::Thread("VisualiserComponent"), parent(parent) {
    resetBuffer();
    addChildComponent(openGLVisualiser);
    setVisualiserType(oldVisualiser);
    startTimerHz(60);
    startThread();
    
    addAndMakeVisible(record);
    record.setPulseAnimation(true);
    record.onClick = [this] {
        toggleRecording();
        stopwatch.stop();
        stopwatch.reset();
        if (record.getToggleState()) {
            stopwatch.start();
        }
        resized();
    };
    
    addAndMakeVisible(stopwatch);
    
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
    
    if (parent == nullptr && !visualiserOnly) {
        addAndMakeVisible(fullScreenButton);
    }
    if (child == nullptr && parent == nullptr && !visualiserOnly) {
        addAndMakeVisible(popOutButton);
    }
    addAndMakeVisible(settingsButton);
    
    fullScreenButton.onClick = [this]() {
        enableFullScreen();
    };
    
    settingsButton.onClick = [this]() {
        if (oldVisualiser) {
            juce::PopupMenu menu;

            menu.addCustomItem(1, roughness, 160, 40, false);
            menu.addCustomItem(1, intensity, 160, 40, false);

            menu.showMenuAsync(juce::PopupMenu::Options(), [this](int result) {});
        } else if (openSettings != nullptr) {
            openSettings();
        }
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
    if (!oldVisualiser) {
        openGLVisualiser.updateBuffer(newBuffer);
        return;
    }
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
    g.fillAll(Colours::veryDark);
    if (oldVisualiser) {
        g.setColour(backgroundColour);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), OscirenderLookAndFeel::RECT_RADIUS);
        
        auto r = getLocalBounds().toFloat();
        r.removeFromBottom(25);
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
            g.fillRoundedRectangle(r, OscirenderLookAndFeel::RECT_RADIUS);
            
            // add text
            g.setColour(juce::Colours::white);
            g.setFont(14.0f);
            auto text = child != nullptr ? "Open in separate window" : "Paused";
            g.drawFittedText(text, r.toNearestInt(), juce::Justification::centred, 1);
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
    }
}

void VisualiserComponent::setPaused(bool paused) {
    openGLVisualiser.setPaused(paused);
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
    if (event.mods.isLeftButtonDown() && child == nullptr) {
        setPaused(active);
    }
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
        if (closeSettings != nullptr) {
            closeSettings();
        }
    }
    openGLVisualiser.setVisible(!oldVisualiser);
    resized();
    repaint();
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

void VisualiserComponent::resetBuffer() {
    sampleRate = (int) sampleRateManager.getSampleRate();
    tempBuffer = std::vector<Point>(sampleRate * BUFFER_LENGTH_SECS);
}

void VisualiserComponent::toggleRecording() {
    
}

void VisualiserComponent::haltRecording() {
    record.setToggleState(false, juce::NotificationType::dontSendNotification);
}

void VisualiserComponent::resized() {
    auto area = getLocalBounds();
    juce::Rectangle<int> topRow;
    if (visualiserOnly) {
        topRow = area.removeFromTop(25);
    } else {
        topRow = area.removeFromBottom(25);
    }
    if (parent == nullptr && !visualiserOnly) {
        fullScreenButton.setBounds(topRow.removeFromRight(30));
    }
    if (child == nullptr && parent == nullptr && !visualiserOnly) {
        popOutButton.setBounds(topRow.removeFromRight(30));
    }
    settingsButton.setBounds(topRow.removeFromRight(30));
    record.setBounds(topRow.removeFromRight(25));
    if (record.getToggleState()) {
        stopwatch.setVisible(true);
        stopwatch.setBounds(topRow.removeFromRight(100));
    } else {
        stopwatch.setVisible(false);
    }
    if (!oldVisualiser) {
        openGLVisualiser.setBounds(area);
    }
}

void VisualiserComponent::popoutWindow() {
    haltRecording();
    auto visualiser = new VisualiserComponent(sampleRateManager, consumerManager, settings, this, oldVisualiser);
    visualiser->settings.setLookAndFeel(&getLookAndFeel());
    visualiser->openSettings = openSettings;
    visualiser->closeSettings = closeSettings;
    visualiser->recordingHalted = recordingHalted;
    child = visualiser;
    childUpdated();
    visualiser->setSize(300, 325);
    popout = std::make_unique<VisualiserWindow>("Software Oscilloscope", this);
    popout->setContentOwned(visualiser, true);
    popout->setUsingNativeTitleBar(true);
    popout->setResizable(true, false);
    popout->setVisible(true);
    popout->centreWithSize(300, 325);
    setPaused(true);
    resized();
}

void VisualiserComponent::childUpdated() {
    popOutButton.setVisible(child == nullptr);
}
