#pragma once

#include <JuceHeader.h>
#include "Timeline.h"
#include "TimeGrid.h"
#include "Camera.h"
#include "Group.h"
#include "PreparedSource.h"
#include "../live/LiveSourceFrames.h"
#include "../live/BlenderSourceSettings.h"
#include "BakeSettings.h"
#include "RasterSettings.h"
#include "TextSettings.h"
#include "ScopeProfile.h"
#include "../render/PreparedAudio.h"
#include <atomic>
#include "../../audio/synth/PreparedDrawing.h"

namespace motion {
struct Asset {
    Id id = 0;
    std::shared_ptr<const LiveSourceIdentity> liveIdentity;
    BlenderSourceSettings blenderSettings;
    juce::String name;
    juce::String extension;
    juce::MemoryBlock data;
    BakeSettings bakeSettings;
    RasterSettings rasterSettings;
    TextSettings textSettings;
    int fractalDepth = 3;
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

// Composition content is independent of the project-wide media registry.
// Reusable compositions can share assets without duplicating their payloads.
struct Marker {
    Id id = 0;
    double time = 0;
    juce::String name;
};

struct Composition {
    juce::String name = "Untitled";
    double duration = 180.0;
    double frameRate = 30.0;
    double bpm = 120.0; // initial tempo, from beat 0
    // Stepped tempo changes after the initial tempo, by beat (immutable, shared).
    std::shared_ptr<const std::vector<TempoChange>> tempoChanges;
    Tempo tempo() const { return Tempo(bpm, tempoChanges); }
    TimeDisplay timeDisplay = TimeDisplay::seconds;
    int beatsPerBar = 4;
    double snapBeats = 0.25;
    bool gridSnap = true;
    TimeGrid timeGrid() const {
        TimeGrid grid;
        grid.display = timeDisplay;
        grid.bpm = bpm;
        grid.tempoChanges = tempoChanges;
        grid.frameRate = frameRate;
        grid.beatsPerBar = beatsPerBar;
        grid.snapBeats = snapBeats;
        grid.snapping = gridSnap;
        return grid;
    }
    std::vector<Track> tracks;
    std::vector<Camera> cameras;
    std::vector<CameraCut> cameraCuts;
    std::vector<Marker> markers;
    std::vector<EffectInstance> effects;
    std::vector<Group> groups;
    std::vector<Modulator> modulators;
    std::vector<ModulationRoute> routes;
};

struct CompositionDefinition : Composition {
    Id id = 0;
};

struct Project : Composition {
    // The output display belongs to the whole project, not to one composition.
    ScopeProfile scope;
    std::vector<std::shared_ptr<const Asset>> assets;
    std::vector<std::shared_ptr<const CompositionDefinition>> definitions;
};

// Editable state belongs to the message thread. Undo copies clip/curve values
// but shares immutable asset payloads, so a drag never copies imported media.
class Document : public juce::ChangeBroadcaster {
public:
    explicit Document(juce::UndoManager& undo) : undo(undo) {}
    const Project& project() const { return scopeId == 0 ? state : scopeView; }
    const Project& mainProject() const { return state; }
    Id editingComposition() const { return scopeId; }
    juce::Result enterComposition(Id id);
    juce::Result setMarker(Id id, double time, juce::String name);
    juce::Result removeMarker(Id id);
    std::uint64_t generation() const { return projectGeneration; }
    std::uint64_t revision() const { return stateRevision; }
    Id newId() { return ++lastId; }
    void edit(juce::String label, std::function<void(Project&)> operation);
    // Records nothing, and changes no revision, when the operation declines.
    bool tryEdit(juce::String label, std::function<bool(Project&)> operation);
    // Repeated changes to one control (wheel, arrow keys) within a second
    // join a single undo step instead of one step per increment.
    void editCoalesced(juce::String label, const juce::String& control, std::function<void(Project&)> operation);
    void reset(Project project);
    juce::Result changeTempo(double bpm);
    // Stepped tempo changes by beat; musical clips follow, seconds content stays.
    juce::Result setTempoChange(double beat, double bpm, std::optional<double> replacing = std::nullopt);
    juce::Result removeTempoChange(double beat);
    juce::Result addBlenderSource(juce::String name, BlenderSourceSettings settings, Id& id);
    juce::Result setBlenderSource(Id id, juce::String name, BlenderSourceSettings settings);
    juce::Result setClipTiming(Id clipId, ClipTiming resolvedSeconds);
    // Several clips in one undo step; placement is checked after every change applies.
    juce::Result setClipTimings(const std::vector<std::pair<Id, ClipTiming>>& timings);
    juce::Result duplicateClip(Id sourceId, Id& duplicateId);
    juce::Result makeSourceUnique(Id clipId, const std::shared_ptr<const Asset>& expected, const std::shared_ptr<Asset>& copy);
    std::size_t compositionReferenceCount(Id definition) const;
    juce::Result removeComposition(Id definition);
    juce::Result insertComposition(Id definition, double time, Id track, Id group, Id& clipId);
    bool canReferenceComposition(Id definition) const;
    juce::Result makeCompositionUnique(Id clipId, Id& definitionId);
    juce::Result createComposition(const std::vector<Id>& clipIds, juce::String name, Id& instanceId);
    juce::Result duplicateClips(const std::vector<Id>& sourceIds, std::vector<Id>& duplicateIds);
    // Clipboard contents are value copies, so pasting survives source deletion.
    struct CopiedClip { Id track = 0; TrackKind kind = TrackKind::visual; std::string trackName; Clip clip; };
    struct CopiedKey { std::string property; double offset = 0; Keyframe key; };
    // Places copies so the earliest starts at `time`, on their original track
    // when it is free and unlocked, otherwise on a new track below it.
    juce::Result pasteClips(const std::vector<CopiedClip>& clips, double time, std::vector<Id>& pastedIds);
    // Pastes keys with their relative timing so the earliest lands at `time`.
    juce::Result pasteKeys(Id clipId, const std::vector<CopiedKey>& keys, double time);
    juce::Result removeClips(const std::vector<Id>& clipIds, bool ripple = false);
    juce::Result setMidiInstrument(Id clipId, MidiInstrument settings);
    juce::Result assignMidi(Id clipId, Id assetId);
    juce::Result renameAsset(Id assetId, juce::String name);
    // Swaps a source's media in place (same identity, so every clip keeps its
    // timing, keys and effects). The kind must match: visual for visual,
    // audio for audio.
    juce::Result replaceAsset(Id assetId, std::shared_ptr<const Asset> replacement);
    // Installs a Lua clip's slider bake (a cache: no undo step). False when the
    // clip no longer exists.
    bool setLuaBake(Id clipId, std::shared_ptr<const LuaClipBake> bake);
    // Removes the listed sources (or every unused source when empty) that no
    // clip, composition or MIDI assignment references; one undo step.
    juce::Result removeUnusedAssets(std::vector<Id> assetIds, int& removed);
    std::size_t assetUses(Id assetId) const;
    juce::Result setMidiNotes(Id clipId, std::shared_ptr<const MidiNotes> notes, juce::String undoLabel);
    // Guarded by the clip's current notes, not the global revision, so edits
    // elsewhere during a take do not discard it.
    juce::Result recordMidiNotes(Id clipId, std::shared_ptr<const MidiNotes> expected, std::shared_ptr<const MidiNotes> merged, std::uint64_t expectedGeneration);
    juce::Result clearMidi(Id clipId);
    // Camera cuts form the camera track: each shows one camera over a range.
    // A cut at `time` ends the cut it falls in and lasts until the next cut.
    juce::Result cutToCamera(Id camera, double time, Id& cutId);
    juce::Result setCutCamera(Id cut, Id camera);
    // Moves or trims a cut; it may not overlap another cut or leave the
    // project, and keeps at least one frame.
    juce::Result setCutRange(Id cut, double start, double end, juce::String label = "Move camera cut");
    juce::Result removeCut(Id cut);
    // Aims a camera at a clip or group origin (0: free) and/or places it in a
    // group's space (0: world).
    juce::Result setCameraRig(Id camera, Id target, Id parent);
    // Shared modulators and their routes live in the scope being edited.
    juce::Result addModulator(Modulator modulator, Id& id);
    juce::Result setModulator(Modulator modulator);
    juce::Result removeModulator(Id id);
    juce::Result addRoute(ModulationRoute route, Id& id);
    // Creates a modulator already driving one property, as one undo step.
    juce::Result addRoutedModulator(Modulator modulator, ModulationRoute route, Id& modulatorId);
    juce::Result setRoute(const ModulationRoute& route);
    juce::Result removeRoute(Id id);
    // Links (or unlinks, with nullopt) a property; refuses cycles.
    juce::Result setLink(Id target, const std::string& property, std::optional<PropertyLink> link);
    void preview(Project project);
    juce::String coalescingControl;
    std::uint64_t coalescingRevision = 0;
    double coalescingTime = 0;
    void commit(juce::String label, Project before);
    juce::XmlElement save() const;
    juce::Result load(const juce::XmlElement& xml);
    // Worker-safe preparation; output is replaced only after full validation.
    static juce::Result prepareLoad(const juce::XmlElement& xml, Project& output, const std::atomic<bool>* cancel = nullptr);
    std::function<void()> onChanged;

    static constexpr std::size_t maximumSourceBytes = 64 * 1024 * 1024;
    static constexpr std::size_t maximumSourceFrames = 3600;
    static constexpr std::size_t maximumShapesPerFrame = 100000;
    static constexpr std::size_t maximumSourceShapes = 1000000;
    static juce::Result decodeAsset(Asset& asset, const std::atomic<bool>* cancel = nullptr, std::atomic<double>* progress = nullptr, const juce::File& videoDecoder = {});
    static bool isVideoSource(const juce::String& extension) { return extension.equalsIgnoreCase(".mp4") || extension.equalsIgnoreCase(".mov"); }
    static bool isRasterSource(const juce::String& extension) {
        return isVideoSource(extension) || extension.equalsIgnoreCase(".png") || extension.equalsIgnoreCase(".jpg") || extension.equalsIgnoreCase(".jpeg") || extension.equalsIgnoreCase(".gif");
    }
    static bool isMidiSource(const juce::String& extension) {
        return extension.equalsIgnoreCase(".mid") || extension.equalsIgnoreCase(".midi");
    }
    static Clip makeCompositionClip(Id id, const CompositionDefinition& definition, double time);
    static Clip makeClip(Id id, const Asset& asset, double time);

private:
    Id highestId() const;
    juce::Result retempo(std::shared_ptr<const std::vector<TempoChange>> changes, juce::String label);
    juce::Result editMidi(Id clipId, juce::String label, const std::function<juce::Result(Clip&)>& operation);
    void apply(Project value);
    Project mergeScope(Project view) const;
    void refreshScope();
    struct Change;
    Project state, scopeView;
    Id scopeId = 0;
    Id lastId = 0;
    std::uint64_t projectGeneration = 0;
    std::uint64_t stateRevision = 0;
    juce::UndoManager& undo;
};
}
