#pragma once

#include <JuceHeader.h>
#include "Timeline.h"
#include "TimeGrid.h"
#include "Camera.h"
#include "Group.h"
#include "PreparedSource.h"
#include "BakeSettings.h"
#include "RasterSettings.h"
#include "../render/PreparedAudio.h"
#include <atomic>
#include "../../audio/synth/PreparedDrawing.h"

namespace motion {
struct Asset {
    Id id = 0;
    juce::String name;
    juce::String extension;
    juce::MemoryBlock data;
    BakeSettings bakeSettings;
    RasterSettings rasterSettings;
    juce::MemoryBlock bakedData;
    juce::String bakeKey;
    std::shared_ptr<const osci::PreparedDrawing> drawing;
    std::shared_ptr<const PreparedSource> source;
    std::shared_ptr<const PreparedAudio> audio;
    std::shared_ptr<const MidiNotes> midi;
    double midiImportBpm = 120;
    double midiSuggestedBpm = 120;
    int midiIgnoredEvents = 0;
};

struct Project {
    juce::String name = "Untitled";
    double duration = 180.0;
    double frameRate = 30.0;
    double bpm = 120.0;
    TimeDisplay timeDisplay = TimeDisplay::seconds;
    int beatsPerBar = 4;
    double snapBeats = 0.25;
    bool gridSnap = true;
    TimeGrid timeGrid() const {
        TimeGrid grid;
        grid.display = timeDisplay;
        grid.bpm = bpm;
        grid.frameRate = frameRate;
        grid.beatsPerBar = beatsPerBar;
        grid.snapBeats = snapBeats;
        grid.snapping = gridSnap;
        return grid;
    }
    std::vector<std::shared_ptr<const Asset>> assets;
    std::vector<Track> tracks;
    std::vector<Camera> cameras;
    std::vector<CameraCut> cameraCuts;
    std::vector<EffectInstance> effects;
    std::vector<Group> groups;
};

// Editable state belongs to the message thread. Undo copies clip/curve values
// but shares immutable asset payloads, so a drag never copies imported media.
class Document : public juce::ChangeBroadcaster {
public:
    explicit Document(juce::UndoManager& undo) : undo(undo) {}
    const Project& project() const { return state; }
    std::uint64_t generation() const { return projectGeneration; }
    std::uint64_t revision() const { return stateRevision; }
    Id newId() { return ++lastId; }
    void edit(juce::String label, std::function<void(Project&)> operation);
    void reset(Project project);
    juce::Result changeTempo(double bpm);
    juce::Result setClipTiming(Id clipId, ClipTiming resolvedSeconds);
    juce::Result duplicateClip(Id sourceId, Id& duplicateId);
    juce::Result assignMidi(Id clipId, Id assetId);
    juce::Result setMidiNotes(Id clipId, std::shared_ptr<const MidiNotes> notes, juce::String undoLabel);
    juce::Result clearMidi(Id clipId);
    void preview(Project project) { apply(std::move(project)); }
    void commit(juce::String label, Project before);
    juce::XmlElement save() const;
    juce::Result load(const juce::XmlElement& xml);
    std::function<void()> onChanged;

    static constexpr std::size_t maximumSourceBytes = 64 * 1024 * 1024;
    static constexpr std::size_t maximumSourceFrames = 3600;
    static constexpr std::size_t maximumShapesPerFrame = 100000;
    static constexpr std::size_t maximumSourceShapes = 1000000;
    static juce::Result decodeAsset(Asset& asset, const std::atomic<bool>* cancel = nullptr, std::atomic<double>* progress = nullptr);
    static bool isRasterSource(const juce::String& extension) {
        return extension.equalsIgnoreCase(".png") || extension.equalsIgnoreCase(".jpg") || extension.equalsIgnoreCase(".jpeg") || extension.equalsIgnoreCase(".gif");
    }
    static bool isMidiSource(const juce::String& extension) {
        return extension.equalsIgnoreCase(".mid") || extension.equalsIgnoreCase(".midi");
    }
    static Clip makeClip(Id id, const Asset& asset, double time);

private:
    juce::Result editMidi(Id clipId, juce::String label, const std::function<juce::Result(Clip&)>& operation);
    void apply(Project value);
    struct Change;
    Project state;
    Id lastId = 0;
    std::uint64_t projectGeneration = 0;
    std::uint64_t stateRevision = 0;
    juce::UndoManager& undo;
};
}
