#include "OscirenderAudioTimelineController.h"

OscirenderAudioTimelineController::OscirenderAudioTimelineController(OscirenderAudioProcessor& processor)
    : audioProcessor(processor)
{
}

std::shared_ptr<WavParser> OscirenderAudioTimelineController::getWavParser()
{
    int currentFileIndex = audioProcessor.getCurrentFileIndex();
    if (currentFileIndex >= 0 && audioProcessor.parsers[currentFileIndex] != nullptr) {
        return audioProcessor.parsers[currentFileIndex]->getWav();
    }
    return nullptr;
}

void OscirenderAudioTimelineController::onValueChange(double value)
{
    auto wav = getWavParser();
    if (wav != nullptr) {
        wav->setProgress(value);
    }
}

void OscirenderAudioTimelineController::onPlay()
{
    auto wav = getWavParser();
    if (wav != nullptr) {
        wav->setPaused(false);
    }
}

void OscirenderAudioTimelineController::onPause()
{
    auto wav = getWavParser();
    if (wav != nullptr) {
        wav->setPaused(true);
    }
}

void OscirenderAudioTimelineController::onStop()
{
    // In osci-render, stopping means clearing the file
    // This will be handled by the file controls
    auto wav = getWavParser();
    if (wav != nullptr) {
        wav->setPaused(true);
        wav->setProgress(0.0);
    }
}

void OscirenderAudioTimelineController::onRepeatChanged(bool shouldRepeat)
{
    auto wav = getWavParser();
    if (wav != nullptr) {
        wav->setLooping(shouldRepeat);
    }
}

bool OscirenderAudioTimelineController::isActive()
{
    auto wav = getWavParser();
    return wav != nullptr && wav->isInitialised();
}

double OscirenderAudioTimelineController::getCurrentPosition()
{
    auto wav = getWavParser();
    if (wav != nullptr && wav->isInitialised()) {
        return (double)wav->currentSample / wav->totalSamples;
    }
    return 0.0;
}

void OscirenderAudioTimelineController::setup(
    std::function<void(double)> setValueCallback,
    std::function<void(bool)> setPlayingCallback,
    std::function<void(bool)> setRepeatCallback)
{
    auto wav = getWavParser();
    if (wav != nullptr && wav->isInitialised()) {
        setRepeatCallback(wav->isLooping());
        setPlayingCallback(!wav->isPaused());
    } else {
        setPlayingCallback(false);
    }
}
