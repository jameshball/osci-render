#include "AnimationTimelineComponent.h"
#include "../PluginProcessor.h"

AnimationTimelineComponent::AnimationTimelineComponent(OscirenderAudioProcessor& processor)
    : audioProcessor(processor)
{
    onValueChange = [this](double value) {
        juce::SpinLock::ScopedLockType sl(audioProcessor.parsersLock);
        int currentFileIndex = audioProcessor.getCurrentFileIndex();
        if (currentFileIndex < 0) return;
        auto parser = audioProcessor.parsers[currentFileIndex];
        if (parser != nullptr) {
            audioProcessor.animationFrame = value * (parser->getNumFrames() - 1);
            parser->setFrame((int)audioProcessor.animationFrame);
        }
    };

    onPlay = [this]() {
        audioProcessor.animateFrames->setValueNotifyingHost(true);
    };

    onPause = [this]() {
        audioProcessor.animateFrames->setValueNotifyingHost(false);
    };

    onStop = [this]() {
        audioProcessor.animateFrames->setValueNotifyingHost(false);
    };

    onRepeatChanged = [this](bool shouldRepeat) {
        audioProcessor.loopAnimation->setValueNotifyingHost(shouldRepeat);
    };

    setup();
}

AnimationTimelineComponent::~AnimationTimelineComponent() = default;

void AnimationTimelineComponent::timerCallback()
{ 
    juce::SpinLock::ScopedLockType sl(audioProcessor.parsersLock);
    int currentFileIndex = audioProcessor.getCurrentFileIndex();
    if (currentFileIndex < 0) return;
    auto parser = audioProcessor.parsers[currentFileIndex];
    if (parser == nullptr) return;
    int totalFrames = parser->getNumFrames();
    double frame = std::fmod(audioProcessor.animationFrame, totalFrames);
    setValue(frame / (totalFrames - 1), juce::dontSendNotification);
}

void AnimationTimelineComponent::setup()
{
    juce::SpinLock::ScopedLockType sl(audioProcessor.parsersLock);
    int currentFileIndex = audioProcessor.getCurrentFileIndex();
    
    if (currentFileIndex >= 0) {
        auto parser = audioProcessor.parsers[currentFileIndex];
        
        if (parser->isAnimatable) {
            int totalFrames = parser->getNumFrames();
            int currentFrame = parser->getCurrentFrame();
            
            if (totalFrames > 1) {
                setValue(static_cast<double>(currentFrame) / (totalFrames - 1), juce::dontSendNotification);
            } else {
                setValue(0, juce::dontSendNotification);
            }
        }
    }
    
    setPlaying(audioProcessor.animateFrames->getBoolValue());
    setRepeat(audioProcessor.loopAnimation->getBoolValue());
}

void AnimationTimelineComponent::update()
{
    setup();
}
