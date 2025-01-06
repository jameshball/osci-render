#include "AudioPlayerComponent.h"
#include "../CommonPluginProcessor.h"

AudioPlayerComponent::AudioPlayerComponent(CommonAudioProcessor& processor) : audioProcessor(processor) {
    setOpaque(false);

    audioProcessor.addAudioPlayerListener(this);

    parser = audioProcessor.wavParser;

    addAndMakeVisible(slider);
    slider.setSliderStyle(juce::Slider::SliderStyle::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    slider.setOpaque(false);
    slider.setRange(0, 1, 0.001);
    slider.setLookAndFeel(&timelineLookAndFeel);
    slider.setColour(juce::Slider::ColourIds::thumbColourId, juce::Colours::black);

    slider.onValueChange = [this]() {
        juce::SpinLock::ScopedLockType sl(audioProcessor.wavParserLock);

        if (parser != nullptr) {
            parser->setProgress(slider.getValue());
        } else {
            slider.setValue(0, juce::dontSendNotification);
        }
    };

    addChildComponent(playButton);
    addChildComponent(pauseButton);

    playButton.setTooltip("Play audio file");
    pauseButton.setTooltip("Pause audio file");

    repeatButton.setToggleState(true, juce::dontSendNotification);

    playButton.onClick = [this]() {
        juce::SpinLock::ScopedLockType sl(audioProcessor.wavParserLock);

        if (parser != nullptr) {
            parser->setPaused(false);
            playButton.setVisible(false);
            pauseButton.setVisible(true);
        }
    };

    pauseButton.onClick = [this]() {
        juce::SpinLock::ScopedLockType sl(audioProcessor.wavParserLock);

        if (parser != nullptr) {
            parser->setPaused(true);
            playButton.setVisible(true);
            pauseButton.setVisible(false);
        }
    };

    addAndMakeVisible(repeatButton);

    repeatButton.setTooltip("Repeat audio file once it finishes playing");

    repeatButton.onClick = [this]() {
        juce::SpinLock::ScopedLockType sl(audioProcessor.wavParserLock);

        if (parser != nullptr) {
            parser->setLooping(repeatButton.getToggleState());
        }
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
    juce::SpinLock::ScopedLockType sl(audioProcessor.wavParserLock);
    if (parser != nullptr) {
        parser->onProgress = nullptr;
    }
}

// must hold lock
void AudioPlayerComponent::setup() {
    if (parser != nullptr) {
        slider.setVisible(true);
        repeatButton.setVisible(true);
        stopButton.setVisible(true);
        parser->onProgress = [this](double progress) {
            juce::WeakReference<AudioPlayerComponent> weakRef(this);
            juce::MessageManager::callAsync([this, progress, weakRef]() {
                if (weakRef) {
                    slider.setValue(progress, juce::dontSendNotification);
                    repaint();
                }
            });
        };
        playButton.setVisible(parser->isPaused());
        pauseButton.setVisible(!parser->isPaused());
        parser->setLooping(repeatButton.getToggleState());
    } else {
        slider.setVisible(false);
        repeatButton.setVisible(false);
        stopButton.setVisible(false);
        slider.setValue(0);
        playButton.setVisible(false);
        pauseButton.setVisible(false);
    }

    parserWasNull = parser == nullptr;
}

void AudioPlayerComponent::setPaused(bool paused) {
    if (paused) {
        pauseButton.triggerClick();
    } else {
        playButton.triggerClick();
    }
}

void AudioPlayerComponent::parserChanged(std::shared_ptr<WavParser> parser) {
    this->parser = parser;
    setup();
    repaint();
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
