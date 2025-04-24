#include "AudioPlayerComponent.h"
#include "../CommonPluginProcessor.h"

AudioPlayerComponent::AudioPlayerComponent(CommonAudioProcessor& processor)
    : audioProcessor(processor)
{
    setOpaque(false);

    onValueChange = [this](double value) {
        juce::SpinLock::ScopedLockType sl(audioProcessor.wavParserLock);
        audioProcessor.wavParser.setProgress(value);
    };

    onPlay = [this]() {
        audioProcessor.wavParser.setPaused(false);
    };

    onPause = [this]() {
        audioProcessor.wavParser.setPaused(true);
    };

    onStop = [this]() {
        audioProcessor.stopAudioFile();
        hide();
    };

    onRepeatChanged = [this](bool shouldRepeat) {
        audioProcessor.wavParser.setLooping(shouldRepeat);
    };

    playButton.setTooltip("Play audio file");
    pauseButton.setTooltip("Pause audio file");
    stopButton.setTooltip("Stop and close audio file");
    repeatButton.setTooltip("Repeat audio file once it finishes playing");

    {
        juce::SpinLock::ScopedLockType sl(audioProcessor.wavParserLock);
        setup();
    }
    
    audioProcessor.addAudioPlayerListener(this);
}

AudioPlayerComponent::~AudioPlayerComponent()
{
    audioProcessor.removeAudioPlayerListener(this);
}

void AudioPlayerComponent::setup()
{
    setRepeat(audioProcessor.wavParser.isLooping());

    if (audioProcessor.wavParser.isInitialised()) {
        slider.setVisible(true);
        repeatButton.setVisible(true);
        stopButton.setVisible(true);
        setPlaying(!audioProcessor.wavParser.isPaused());
    } else {
        hide();
    }
}

void AudioPlayerComponent::hide() {
    slider.setVisible(false);
    repeatButton.setVisible(false);
    stopButton.setVisible(false);
    setValue(0, juce::dontSendNotification);
    playButton.setVisible(false);
    pauseButton.setVisible(false);
}

void AudioPlayerComponent::setPaused(bool paused)
{
    audioProcessor.wavParser.setPaused(paused);
    setPlaying(!paused);
}

void AudioPlayerComponent::parserChanged()
{
    setup();
    repaint();
    if (onParserChanged != nullptr) {
        onParserChanged();
    }
}

bool AudioPlayerComponent::isInitialised() const {
    return audioProcessor.wavParser.isInitialised();
}

void AudioPlayerComponent::timerCallback() {
    if (!isInitialised()) {
        return;
    }
    setValue((double)audioProcessor.wavParser.currentSample / audioProcessor.wavParser.totalSamples, juce::dontSendNotification);
}
