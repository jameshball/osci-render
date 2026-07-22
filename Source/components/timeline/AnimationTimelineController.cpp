#include "AnimationTimelineController.h"
#include "../../PluginProcessor.h"
#include "../../parser/FileParser.h"

AnimationTimelineController::AnimationTimelineController(OscirenderAudioProcessor& processor)
    : audioProcessor(processor)
{
}

void AnimationTimelineController::onValueChange(double value)
{
    auto& files = audioProcessor.getFileController();
    juce::SpinLock::ScopedLockType sl(files.lock);
    auto parser = files.getCurrentParser();
    if (parser != nullptr) {
        audioProcessor.animationFrame = value * (parser->getNumFrames() - 1);
        parser->setFrame((int)audioProcessor.animationFrame);
    }
}

void AnimationTimelineController::onPlay()
{
    audioProcessor.animateFrames->setValueNotifyingHost(true);
}

void AnimationTimelineController::onPause()
{
    audioProcessor.animateFrames->setValueNotifyingHost(false);
}

void AnimationTimelineController::onStop()
{
    audioProcessor.animateFrames->setValueNotifyingHost(false);
}

void AnimationTimelineController::onRepeatChanged(bool shouldRepeat)
{
    audioProcessor.loopAnimation->setValueNotifyingHost(shouldRepeat);
}

bool AnimationTimelineController::isActive()
{
    return true; // Animation timeline is always active when visible
}

double AnimationTimelineController::getCurrentPosition()
{
    auto& files = audioProcessor.getFileController();
    juce::SpinLock::ScopedLockType sl(files.lock);
    auto parser = files.getCurrentParser();
    if (parser == nullptr) return 0.0;
    int totalFrames = parser->getNumFrames();
    if (totalFrames <= 1) return 0.0;
    double frame = std::fmod(audioProcessor.animationFrame, (double)totalFrames);
    if (frame < 0.0) frame += (double)totalFrames;
    return frame / (totalFrames - 1);
}

void AnimationTimelineController::setup(
    std::function<void(double)> setValueCallback,
    std::function<void(bool)> setPlayingCallback,
    std::function<void(bool)> setRepeatCallback)
{
    auto& files = audioProcessor.getFileController();
    juce::SpinLock::ScopedLockType sl(files.lock);
    auto parser = files.getCurrentParser();
    if (parser != nullptr && parser->isAnimatable) {
        int totalFrames = parser->getNumFrames();
        int currentFrame = parser->getCurrentFrame();

        if (totalFrames > 1) {
            setValueCallback(static_cast<double>(currentFrame) / (totalFrames - 1));
        } else {
            setValueCallback(0.0);
        }
    }

    setPlayingCallback(audioProcessor.animateFrames->getBoolValue());
    setRepeatCallback(audioProcessor.loopAnimation->getBoolValue());
}

void AnimationTimelineController::update()
{
    // Called when file changes - setup will be called again by TimelineComponent
}
