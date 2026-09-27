#pragma once

#include "../CommonPluginProcessor.h"
#include "model/Document.h"
#include "render/CompositionRenderer.h"
#include "render/CompositionPreparationWorker.h"
#include "render/BeamTransitionGuard.h"
#include "render/LiveMidiAudition.h"
#include "../audio/PreparedState.h"
#include "live/LiveSourceExchange.h"

class MotionProcessor : public CommonAudioProcessor, private juce::AsyncUpdater {
public:
    enum class OutputMode { soundtrack, xy, xyrgb };
    OutputMode getOutputMode() const { return outputMode.load(); }
    void setOutputMode(OutputMode value) { outputMode.store(value); }
    // Transient input monitoring: neither note events nor this choice are part
    // of the document/export. The audio thread resolves the ID in its snapshot.
    void setMidiAudition(motion::Id clip) { midiAuditionTarget.store(clip); }
    motion::Id getMidiAudition() const { return midiAuditionTarget.load(); }
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    MotionProcessor();
    ~MotionProcessor() override;
    void prepareToPlayInternal(double sampleRate, int samplesPerBlock) override;
    motion::Document document { getUndoManager() };
    std::atomic<bool> playing { false };
    std::atomic<bool> freezeWhenStopped { true };
    std::atomic<double> position { 0.0 };
    void seek(double seconds) { seekSerial.fetch_add(1); requestedPosition.store(std::max(0.0, seconds)); }
    std::uint64_t seekRevision() const { return seekSerial.load(); }
    void collectPreparedState() { composition.collect(); liveSources.collect(); }
    // Message-thread publication/preview; source geometry was prepared off audio.
    void publishLiveSources(std::shared_ptr<const motion::LiveSourceFrames> frames) { liveSources.publish(std::move(frames)); }
    std::shared_ptr<const motion::LiveSourceFrames> liveSourcePreview() const { return liveSources.previewSnapshot(); }
    bool isPreparingComposition() const { return acceptedPreparationRevision != preparationRevision; }
    juce::String getPreparationError() const { return preparationError; }
    // Message-thread-only transient editing preview; never alters saved state.
    void previewComposition(const motion::Project& project) {
        requestComposition(project);
    }

private:
    void requestComposition(const motion::Project& project);
    void handleAsyncUpdate() override;
    void routeSignalOutput(juce::AudioBuffer<float>& buffer);
    std::unique_ptr<motion::CompositionPreparationWorker> preparationWorker;
    std::uint64_t preparationRevision = 0, acceptedPreparationRevision = 0;
    std::atomic<double> requestedSampleRate {48000};
    double preparationSampleRate = 48000;
    juce::String preparationError;
    std::atomic<bool> preparationFailed {false};
    std::uint64_t previousRevision = 0;
    bool wasPlaying = false, wasDrawing = false;
    motion::BeamTransitionGuard transitionGuard;
    motion::LiveMidiPerformance liveMidi;
    std::atomic<motion::Id> midiAuditionTarget {0};
    motion::Id previousAuditionTarget = 0;
    std::uint64_t liveMidiSample = 0;
    juce::int64 oscillatorSample = 0;
    std::atomic<OutputMode> outputMode { OutputMode::soundtrack };
    osci::PreparedState<motion::PreparedComposition> composition;
    motion::LiveSourceExchange liveSources;
    std::uint64_t previousLiveRevision = 0;
    juce::AudioBuffer<float> signal;
    std::atomic<double> requestedPosition { -1.0 };
    std::atomic<std::uint64_t> seekSerial {0};
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
