#pragma once

#include "../CommonPluginProcessor.h"
#include "model/Document.h"
#include "render/SampleClock.h"
#include "render/CompositionRenderer.h"
#include "render/CompositionPreparationWorker.h"
#include "render/BeamTransitionGuard.h"
#include "render/BeamRenderer.h"
#include "render/LiveMidiAudition.h"
#include "render/MidiRecording.h"
#include "render/MidiRecordingSession.h"
#include "render/PreparedState.h"
#include "live/LiveSourceExchange.h"
#include "live/LiveBlenderController.h"
#include "ScopeBeam.h"

class MotionProcessor : public CommonAudioProcessor, private juce::AsyncUpdater {
public:
    enum class OutputMode { soundtrack, xy, xyrgb };
    // Pausing the Scope or sharing its picture are view settings, not edits.
    bool isUndoExcluded(const juce::String& paramID) const override {
        return CommonAudioProcessor::isUndoExcluded(paramID) || paramID == "visualiserPaused" || paramID == "textureOutputEnabled";
    }
    OutputMode getOutputMode() const { return outputMode.load(); }
    void setOutputMode(OutputMode value) { outputMode.store(value); }
    // Transient input monitoring: neither note events nor this choice are part
    // of the document/export. The audio thread resolves the ID in its snapshot.
    void setMidiAudition(motion::Id clip) { midiAuditionTarget.store(clip); }
    motion::Id getMidiAudition() const { return midiAuditionTarget.load(); }
    motion::MidiRecording& midiRecorder() { return midiRecording; }
    motion::MidiRecordingSession& midiRecordingSession() { return *midiSession; }
    // The live rate: exports use it so the file matches what the scope showed.
    double exportSampleRate() const {
        const auto rate = std::round(requestedSampleRate.load());
        return std::isfinite(rate) && rate >= 8000 && rate <= 768000 ? rate : 48000.0;
    }
    void releaseResources() override;
    void processBlockSkipped(bool unavailable) override { midiRecording.skippedBlock(unavailable); }
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    MotionProcessor();
    ~MotionProcessor() override;
    void prepareToPlayInternal(double sampleRate, int samplesPerBlock) override;
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
    void routeSignalOutput(juce::AudioBuffer<float>& buffer);
    // Audio thread: held live notes end with any jump in the clock.
    void resetLiveMidi() { liveMidi.reset(); liveInputs.reset(); }
    void writeSignal(int index, const osci::Point& point) {
        const std::array<float, 6> values {point.x, point.y, point.z, point.r, point.g, point.b};
        for (int channel = 0; channel < 6; ++channel) { signal.setSample(channel, index, values[static_cast<std::size_t>(channel)]); }
    }
    std::unique_ptr<motion::CompositionPreparationWorker> preparationWorker;
    std::uint64_t preparationRevision = 0, acceptedPreparationRevision = 0;
    std::atomic<double> requestedSampleRate {48000};
    double preparationSampleRate = 48000;
    juce::String preparationError;
    std::atomic<bool> preparationFailed {false};
    std::uint64_t previousRevision = 0;
    bool wasPlaying = false;
    motion::BeamTransitionGuard transitionGuard;
    motion::BeamRenderer beam;
    motion::LiveMidiPerformance liveMidi;
    motion::LiveMidiInputs liveInputs;
    motion::MidiRecording midiRecording;
    std::unique_ptr<motion::MidiRecordingSession> midiSession;
public:
    // Last beam plan, for the status bar. Written by the audio thread.
    std::atomic<int> beamLayers {0};
    std::atomic<int> beamInterleave {1};
private:
    bool armMidiRecording(const motion::MidiRecording::Config& config);
    void stopMidiDevice();
    void releaseRecordingTransport();
    juce::SpinLock midiLifecycleLock;
    std::atomic<bool> midiDeviceReady{false};
    bool recordingOwnsTransport = false; // Audio thread, or lifecycle with callbacks excluded.
    std::atomic<motion::Id> midiAuditionTarget {0};
    // The Scope's picture at the last audio block, applied by the visualiser
    // through its external modulation hook (see ScopeBeam.h).
    std::array<std::atomic<float>, motion::beamPropertySpecs.size()> scopeBeam;
    std::unique_ptr<motion::ScopeBeamSlots> scopeBeamSlots;
    motion::Id previousAuditionTarget = 0;
    std::uint64_t liveMidiSample = 0;
    juce::int64 oscillatorSample = 0;
    std::atomic<OutputMode> outputMode { OutputMode::soundtrack };
    motion::PreparedState<motion::PreparedComposition> composition;
    motion::LiveSourceExchange liveSources;
    std::unique_ptr<motion::LiveBlenderController> blender;
    std::uint64_t previousLiveRevision = 0;
    juce::AudioBuffer<float> signal;
    std::atomic<double> requestedPosition { -1.0 };
    std::atomic<std::uint64_t> seekSerial {0};
    std::atomic<double> lastCallbackMs {0.0};
    double audioTime = 0.0;
    juce::int64 audioSample = 0;
public:
    void processBlockInternal(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    juce::AudioProcessorEditor* createEditor() override;
    void getStateInformation(juce::MemoryBlock& destination) override;
    void setStateInformation(const void* data, int size) override;
    // Message-thread publication after standalone background preparation.
    void applyPreparedProject(motion::Project prepared, juce::XmlElement& state);
};
