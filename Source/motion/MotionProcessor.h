#pragma once

#include "../CommonPluginProcessor.h"
#include "model/Document.h"
#include "model/ProjectFile.h"
#include "render/SampleClock.h"
#include "render/CompositionRenderer.h"
#include "render/CompositionPreparationWorker.h"
#include "render/BeamTransitionGuard.h"
#include "render/BeamRenderer.h"
#include "render/HiddenLineBaker.h"
#include "render/PreparedState.h"
#include "live/LiveSourceExchange.h"
#include "live/LiveBlenderController.h"
#include "ScopeBeam.h"

class MotionProcessor : public CommonAudioProcessor, private juce::AsyncUpdater, private juce::AudioProcessorParameter::Listener {
public:
    // What the Scope shows across the output canvas; message thread.
    motion::hidden::Reach canvasReach() {
        const auto size = recordingParameters.getCanvasSize();
        return motion::hidden::Reach::ofCanvas(size.width, size.height, VisualiserGeometry::signalGain);
    }
    enum class OutputMode { soundtrack, xy, xyrgb };
    // Pausing the Scope or sharing its picture are view settings, not edits.
    bool isUndoExcluded(const juce::String& paramID) const override {
        return CommonAudioProcessor::isUndoExcluded(paramID) || paramID == "visualiserPaused" || paramID == "textureOutputEnabled";
    }
    OutputMode getOutputMode() const { return outputMode.load(); }
    void setOutputMode(OutputMode value) { outputMode.store(value); }
    // The live rate: exports use it so the file matches what the scope showed.
    double exportSampleRate() const {
        const auto rate = std::round(requestedSampleRate.load());
        return std::isfinite(rate) && rate >= 8000 && rate <= 768000 ? rate : 48000.0;
    }
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    MotionProcessor();
    ~MotionProcessor() override;
    void prepareToPlayInternal(double sampleRate, int samplesPerBlock) override;
    // osci-motion is an app, never a plugin: it can run faster than the
    // audio device, as osci-render's standalone does.
    bool supportsInternalSampleRateOverride() const override { return OSCI_PREMIUM; }
    motion::Document document { getUndoManager() };
    std::atomic<bool> playing { false };
    std::atomic<double> position { 0.0 };
    // Loop playback (seconds); the editor mirrors the project's loop range.
    std::atomic<double> loopStart { 0.0 }, loopEnd { 0.0 };
    std::atomic<bool> looping { false };
    void seek(double seconds) {
        seekSerial.fetch_add(1);
        requestedPosition.store(std::max(0.0, seconds));
        // Without audio callbacks the playhead moves at once (see showIdleSeek).
        if (audioIdle()) { position.store(onSampleGrid(std::max(0.0, seconds))); }
    }
    // The audio thread reports positions on its sample grid; idle seeks match it.
    double onSampleGrid(double seconds) const {
        const auto rate = exportSampleRate();
        const auto index = motion::sampleIndex(seconds, rate);
        return index.has_value() ? static_cast<double>(*index) / rate : seconds;
    }
    bool audioIdle() const { return juce::Time::getMillisecondCounterHiRes() - lastCallbackMs.load(std::memory_order_relaxed) >= 250; }
    std::uint64_t seekRevision() const { return seekSerial.load(); }
    // Without audio callbacks (no output device, or one that failed) a seek
    // still moves the playhead. The request stays queued, so the audio thread
    // applies it too if callbacks resume. Message thread only.
    void showIdleSeek(double duration) {
        if (!audioIdle()) { return; }
        const auto requested = requestedPosition.load();
        if (requested >= 0 && std::isfinite(requested)) { position.store(onSampleGrid(std::clamp(requested, 0.0, std::max(0.0, duration)))); }
    }
    void collectPreparedState() { composition.collect(); liveSources.collect(); }
    // Message-thread publication/preview; source geometry was prepared off audio.
    void publishLiveSources(std::shared_ptr<const motion::LiveSourceFrames> frames) { liveSources.publish(std::move(frames)); }
    std::shared_ptr<const motion::LiveSourceFrames> liveSourcePreview() const { return liveSources.previewSnapshot(); }
    motion::LiveBlenderController& blenderInputs() { return *blender; }
    bool isPreparingComposition() const { return acceptedPreparationRevision != preparationRevision; }
    juce::String getPreparationError() const { return preparationError; }
    // Message thread: prepares `project` for playback off the audio thread.
    // Editors pass transient previews this way too; saved state is untouched.
    void prepareComposition(const motion::Project& project);

private:
    void handleAsyncUpdate() override;
    // The canvas changing changes what hidden lines are removed across.
    void parameterValueChanged(int, float) override { triggerAsyncUpdate(); }
    void parameterGestureChanged(int, bool) override {}
    void routeSignalOutput(juce::AudioBuffer<float>& buffer);
    void writeSignal(int index, const osci::Point& point) {
        const std::array<float, 6> values {point.x, point.y, point.z, point.r, point.g, point.b};
        for (int channel = 0; channel < 6; ++channel) { signal.setSample(channel, index, values[static_cast<std::size_t>(channel)]); }
    }
    std::unique_ptr<motion::CompositionPreparationWorker> preparationWorker;
    std::uint64_t preparationRevision = 0, acceptedPreparationRevision = 0;
    std::atomic<double> requestedSampleRate {48000};
    double preparationSampleRate = 48000;
    motion::hidden::Reach preparationReach;
    juce::String preparationError;
    std::uint64_t previousRevision = 0;
    bool wasPlaying = false;
    motion::BeamTransitionGuard transitionGuard;
    motion::BeamRenderer beam;
public:
    // Last beam plan, for the status bar. Written by the audio thread.
    std::atomic<int> beamLayers {0};
    std::atomic<bool> beamSimplified {false};
private:
    // The Scope's picture at the last audio block, applied by the visualiser
    // through its external modulation hook (see ScopeBeam.h).
    std::array<std::atomic<float>, motion::beamPropertySpecs.size()> scopeBeam;
    std::unique_ptr<motion::ScopeBeamSlots> scopeBeamSlots;
    juce::int64 oscillatorSample = 0;
    std::atomic<OutputMode> outputMode { OutputMode::soundtrack };
    motion::PreparedState<motion::PreparedComposition> composition;
    motion::LiveSourceExchange liveSources;
    std::unique_ptr<motion::LiveBlenderController> blender;
    std::uint64_t previousLiveRevision = 0;
public:
    // The current composition's bake of its hidden lines, when it has one,
    // and how many of its frames are still to bake; message thread.
    std::shared_ptr<const motion::hidden::Bake> hiddenLineBake() const { return publishedBake; }
    std::optional<std::size_t> hiddenLinesRemaining() const { return publishedBake != nullptr && !publishedBake->complete() ? std::optional(publishedBake->remaining()) : std::nullopt; }
    // Bakes from the playhead's frame on, wherever it goes; message thread.
    void followPlayhead() {
        if (publishedBake != nullptr) { publishedBake->focusOn(publishedBake->frameOf(position.load())); }
    }
    bool hidesHiddenLines() const { return publishedBake != nullptr && publishedBake->hides; }
    bool lightsFrames() const { return publishedBake != nullptr && publishedBake->lights; }
    bool reflectsFrames() const { return publishedBake != nullptr && publishedBake->reflects; }
    bool restylesFrames() const { return publishedBake != nullptr && publishedBake->restyles; }
    bool hatchesFrames() const { return publishedBake != nullptr && publishedBake->hatches; }
private:
    // Removes the lines cameras can't see, off the audio thread, from the
    // playhead out. Last, so its workers stop before the rest is destroyed.
    std::shared_ptr<motion::hidden::Bake> publishedBake;
    juce::AudioBuffer<float> signal;
    std::atomic<double> requestedPosition { -1.0 };
    std::atomic<std::uint64_t> seekSerial {0};
    std::atomic<double> lastCallbackMs {0.0};
    double audioTime = 0.0;
    juce::int64 audioSample = 0;
public:
    void processBlockInternal(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    // One piece of a block, no longer than was prepared for.
    void processChunk(juce::AudioBuffer<float>& buffer);
    juce::AudioProcessorEditor* createEditor() override;
    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int size) override;
    // Message-thread publication after background preparation.
    void applyPreparedProject(motion::Project prepared, juce::XmlElement& state);
    // What reopening the last session left out, or why it couldn't, once;
    // message thread.
    juce::String takeRestoreError() { return std::exchange(restoreError, {}); }
private:
    // A last session that couldn't be reopened: handed back as it was when
    // the session is saved again, until the project is changed or replaced,
    // so a failed reopen never overwrites the user's project with an empty one.
    juce::MemoryBlock unrestoredState;
    std::uint64_t unrestoredGeneration = 0, unrestoredRevision = 0;
    juce::String restoreError;
    motion::hidden::Baker hiddenLineBaker;
};
