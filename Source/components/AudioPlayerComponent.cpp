#include "AudioPlayerComponent.h"
#include "../CommonPluginProcessor.h"

AudioPlayerComponent::AudioPlayerComponent(CommonAudioProcessor& processor) : audioProcessor(processor) {
    setOpaque(false);

    audioProcessor.addAudioPlayerListener(this);

    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::SliderStyle::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider.setOpaque(false);
    slider.setRange(0, 1, 0.001);
    slider.setLookAndFeel(&timelineLookAndFeel);
    slider.setColour(juce::Slider::ColourIds::thumbColourId, juce::Colours::black);

    slider.onValueChange = [this]() {
        juce::SpinLock::ScopedLockType sl(audioProcessor.wavParserLock);

        audioProcessor.wavParser.setProgress(slider.getValue());
    };

    addChildComponent(playButton);
    addChildComponent(pauseButton);

    playButton.setTooltip("Play audio file");
    pauseButton.setTooltip("Pause audio file");

    playButton.onClick = [this]() {
        audioProcessor.wavParser.setPaused(false);
        if (audioProcessor.wavParser.isInitialised()) {
            playButton.setVisible(false);
            pauseButton.setVisible(true);
        }
    };

    pauseButton.onClick = [this]() {
        audioProcessor.wavParser.setPaused(true);
        if (audioProcessor.wavParser.isInitialised()) {
            playButton.setVisible(true);
            pauseButton.setVisible(false);
        }
    };

    addAndMakeVisible(repeatButton);

    repeatButton.setTooltip("Repeat audio file once it finishes playing");

    repeatButton.onClick = [this]() {
        audioProcessor.wavParser.setLooping(repeatButton.getToggleState());
    };

    addAndMakeVisible(stopButton);

    stopButton.setTooltip("Stop and close audio file");

    stopButton.onClick = [this]() {
        audioProcessor.stopAudioFile();
    };

    {
        juce::SpinLock::ScopedLockType sl(audioProcessor.wavParserLock);
        setup();
    }
}

AudioPlayerComponent::~AudioPlayerComponent() {
    audioProcessor.removeAudioPlayerListener(this);
    audioProcessor.wavParser.onProgress = nullptr;
}

// must hold lock
void AudioPlayerComponent::setup() {
    repeatButton.setToggleState(audioProcessor.wavParser.isLooping(), juce::dontSendNotification);

    if (audioProcessor.wavParser.isInitialised()) {
        slider.setVisible(true);
        repeatButton.setVisible(true);
        stopButton.setVisible(true);
        audioProcessor.wavParser.onProgress = [this](double progress) {
            juce::WeakReference<AudioPlayerComponent> weakRef(this);
            juce::MessageManager::callAsync([this, progress, weakRef]() {
                if (weakRef) {
                    slider.setValue(progress, juce::dontSendNotification);
                    repaint();
                }
            });
        };
        playButton.setVisible(audioProcessor.wavParser.isPaused());
        pauseButton.setVisible(!audioProcessor.wavParser.isPaused());
    } else {
        slider.setVisible(false);
        repeatButton.setVisible(false);
        stopButton.setVisible(false);
        slider.setValue(0);
        playButton.setVisible(false);
        pauseButton.setVisible(false);
    }
}

void AudioPlayerComponent::setPaused(bool paused) {
    if (paused) {
        pauseButton.triggerClick();
    } else {
        playButton.triggerClick();
    }
}

void AudioPlayerComponent::parserChanged() {
    setup();
    repaint();
    if (onParserChanged != nullptr) {
        onParserChanged();
    }
}

void AudioPlayerComponent::resized() {
    auto r = getLocalBounds();

    auto playPauseBounds = r.removeFromLeft(25);
    playButton.setBounds(playPauseBounds);
    pauseButton.setBounds(playPauseBounds);
    stopButton.setBounds(r.removeFromLeft(25));

    repeatButton.setBounds(r.removeFromRight(25));

    slider.setBounds(r);
}
