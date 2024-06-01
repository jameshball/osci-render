#include "VisualiserComponent.h"
#include "../LookAndFeel.h"

VisualiserComponent::VisualiserComponent(int numChannels, OscirenderAudioProcessor& p) : numChannels(numChannels), backgroundColour(juce::Colours::black), waveformColour(juce::Colour(0xff00ff00)), audioProcessor(p), juce::Thread("VisualiserComponent") {
    resetBuffer();
    startTimerHz(60);
    startThread();

    setFullScreen(false);
    
    roughness.textBox.setValue(audioProcessor.roughness);
    roughness.textBox.onValueChange = [this]() {
        audioProcessor.roughness = (int) roughness.textBox.getValue();
    };
    intensity.textBox.setValue(audioProcessor.intensity);
    intensity.textBox.onValueChange = [this]() {
        audioProcessor.intensity = intensity.textBox.getValue();
    };

    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus(true);
}

VisualiserComponent::~VisualiserComponent() {
    audioProcessor.consumerStop(consumer);
    stopThread(1000);
}

void VisualiserComponent::setFullScreenCallback(std::function<void(FullScreenMode)> callback) {
    fullScreenCallback = callback;
}

void VisualiserComponent::mouseDoubleClick(const juce::MouseEvent& event) {
    if (fullScreenCallback) {
        fullScreenCallback(FullScreenMode::TOGGLE);
    }
    grabKeyboardFocus();
}

void VisualiserComponent::setBuffer(std::vector<float>& newBuffer) {
    juce::CriticalSection::ScopedLockType scope(lock);
    buffer.clear();
    for (int i = 0; i < newBuffer.size(); i += (int) roughness.textBox.getValue() * numChannels) {
        buffer.push_back(newBuffer[i]);
        buffer.push_back(newBuffer[i + 1]);
    }
}

void VisualiserComponent::setColours(juce::Colour bk, juce::Colour fg) {
    backgroundColour = bk;
    waveformColour = fg;
}

void VisualiserComponent::paint(juce::Graphics& g) {
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
        g.drawFittedText("Paused", getLocalBounds(), juce::Justification::centred, 1);
    }
}

void VisualiserComponent::timerCallback() {
    repaint();
}

void VisualiserComponent::run() {
    while (!threadShouldExit()) {
        if (sampleRate != (int) audioProcessor.currentSampleRate) {
            resetBuffer();
        }
        
        consumer = audioProcessor.consumerRegister(tempBuffer);
        audioProcessor.consumerRead(consumer);
        setBuffer(tempBuffer);
    }
}

void VisualiserComponent::mouseDown(const juce::MouseEvent& event) {
    if (event.mods.isLeftButtonDown()) {
        active = !active;
        if (active) {
            startTimerHz(60);
            startThread();
        } else {
            audioProcessor.consumerStop(consumer);
            stopTimer();
            stopThread(1000);
        }
        repaint();
    } else if (event.mods.isPopupMenu()) {
        juce::PopupMenu menu;

        menu.addCustomItem(1, roughness, 160, 40, false);
        menu.addCustomItem(1, intensity, 160, 40, false);

        menu.showMenuAsync(juce::PopupMenu::Options(), [this](int result) {});
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

void VisualiserComponent::setFullScreen(bool fullScreen) {
	if (fullScreen) {
        setTooltip("");
	} else {
        setTooltip("Click to pause. Double click to toggle full screen. Right click to change quality and intensity settings.");
	}
}

void VisualiserComponent::paintChannel(juce::Graphics& g, juce::Rectangle<float> area, int channel) {
    juce::Path path;

    for (int i = 0; i < buffer.size(); i += numChannels) {
        auto sample = buffer[i + channel];

        if (i == 0) {
            path.startNewSubPath(0.0f, sample);
        } else {
            path.lineTo((float)i, sample);
        }
    }

    // apply affine transform to path to fit in area
    auto transform = juce::AffineTransform::fromTargetPoints(0.0f, -1.0f, area.getX(), area.getY(), 0.0f, 1.0f, area.getX(), area.getBottom(), buffer.size(), -1.0f, area.getRight(), area.getY());
    path.applyTransform(transform);

    g.strokePath(path, juce::PathStrokeType(1.0f));
}

void VisualiserComponent::paintXY(juce::Graphics& g, juce::Rectangle<float> area) {
    auto transform = juce::AffineTransform::fromTargetPoints(-1.0f, -1.0f, area.getX(), area.getBottom(), 1.0f, 1.0f, area.getRight(), area.getY(), 1.0f, -1.0f, area.getRight(), area.getBottom());
    std::vector<juce::Line<float>> lines;

    for (int i = 2; i < buffer.size(); i += 2) {
        lines.emplace_back(buffer[i - 2], buffer[i - 1], buffer[i], buffer[i + 1]);
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
    sampleRate = (int) audioProcessor.currentSampleRate;
    tempBuffer = std::vector<float>(2 * sampleRate * BUFFER_LENGTH_SECS);
}
