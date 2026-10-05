#pragma once

#include <JuceHeader.h>
#include "Timeline.h"
#include "TimeGrid.h"
#include "Camera.h"
#include "Beam.h"
#include "Group.h"
#include "PreparedSource.h"
#include "LiveSourceIdentity.h"
#include "BlenderSourceSettings.h"
#include "BakeSettings.h"
#include "RasterSettings.h"
#include "TextSettings.h"
#include "ScopeProfile.h"
#include "PreparedAudio.h"
#include <atomic>
#include "PreparedDrawing.h"

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
    std::shared_ptr<const motion::PreparedDrawing> drawing;
    std::shared_ptr<const PreparedSource> source;
    std::shared_ptr<const PreparedAudio> audio;
    std::shared_ptr<const MidiNotes> midi;
    double midiImportBpm = 120;
    double midiSuggestedBpm = 120;
    // The file's tempo changes after its first beat (runtime, re-derived on load).
    std::shared_ptr<const std::vector<TempoChange>> midiTempoChanges;
    int midiIgnoredEvents = 0;
};

// Composition content is independent of the project-wide media registry.
// Reusable compositions can share assets without duplicating their payloads.
struct Marker {
    // Markers closer than this share a position, which is not allowed.
    static constexpr double minimumSpacing = 1.0e-9;
    Id id = 0;
    double time = 0;
    juce::String name;
    static bool validName(const juce::String& name) { return name.trim().isNotEmpty() && name.length() <= 120 && !name.containsAnyOf("\r\n"); }
    bool valid(double duration) const { return std::isfinite(time) && time >= 0 && time <= duration && validName(name); }
};

// Markers in time order; ties (which loading refuses) by identity.
inline void sortMarkers(std::vector<Marker>& markers) {
    std::sort(markers.begin(), markers.end(), [](const auto& a, const auto& b) { return a.time != b.time ? a.time < b.time : a.id < b.id; });
}

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
    // Loop playback range in project seconds (like Ableton's loop brace or
    // After Effects' work area). Kept when looping is switched off.
    double loopStart = 0, loopEnd = 0;
    bool looping = false;
    bool hasLoop() const { return std::isfinite(loopStart) && std::isfinite(loopEnd) && loopStart >= 0 && loopEnd > loopStart; }
    // A time within the composition on its frame grid, where keys are set.
    double frameTime(double time) const {
        const auto inside = std::clamp(std::isfinite(time) ? time : 0.0, 0.0, duration);
        return std::isfinite(frameRate) && frameRate > 0 ? std::clamp(std::round(inside * frameRate) / frameRate, 0.0, duration) : inside;
    }
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

// Lookups by id across a composition's tracks; null when nothing matches.
template <typename CompositionType>
auto findClipTrack(CompositionType& composition, Id clip) -> std::conditional_t<std::is_const_v<CompositionType>, const Track*, Track*> {
    for (auto& track : composition.tracks) {
        for (const auto& item : track.clips) {
            if (item.id == clip) { return &track; }
        }
    }
    return nullptr;
}

template <typename CompositionType>
auto findClip(CompositionType& composition, Id id) -> std::conditional_t<std::is_const_v<CompositionType>, const Clip*, Clip*> {
    auto* track = findClipTrack(composition, id);
    if (track == nullptr) { return nullptr; }
    const auto found = std::find_if(track->clips.begin(), track->clips.end(), [id](const auto& clip) { return clip.id == id; });
    return &*found;
}

inline std::shared_ptr<const Asset> findAsset(const std::vector<std::shared_ptr<const Asset>>& assets, Id id) {
    const auto found = std::find_if(assets.begin(), assets.end(), [id](const auto& asset) { return asset != nullptr && asset->id == id; });
    return found != assets.end() ? *found : nullptr;
}

struct Project : Composition {
    // The output display belongs to the whole project, not to one composition.
    ScopeProfile scope;
    // The Scope's picture: animatable beam and display properties.
    Beam beam;
    std::vector<std::shared_ptr<const Asset>> assets;
    std::vector<std::shared_ptr<const CompositionDefinition>> definitions;
};

inline std::shared_ptr<const CompositionDefinition> findDefinition(const Project& project, Id id) {
    const auto found = std::find_if(project.definitions.begin(), project.definitions.end(), [id](const auto& definition) { return definition != nullptr && definition->id == id; });
    return found != project.definitions.end() ? *found : nullptr;
}

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
    // Identities stay below the Scope's reserved one, so it never collides.
    static constexpr Id maximumId = beamIdentity - 1;
    Id newId() {
        jassert(lastId < maximumId);
        return ++lastId;
    }
    void edit(juce::String label, std::function<void(Project&)> operation);
    // Records nothing, and changes no revision, when the operation declines.
    bool tryEdit(juce::String label, std::function<bool(Project&)> operation);
    // Repeated changes to one control (wheel, arrow keys) within a second
    // join a single undo step instead of one step per increment.
    void editCoalesced(juce::String label, const juce::String& control, std::function<void(Project&)> operation);
    void reset(Project project);
    juce::Result changeTempo(double bpm);
    // Replace the initial tempo and every change in one undo step.
    juce::Result setTempoMap(double initialBpm, std::shared_ptr<const std::vector<TempoChange>> changes, juce::String label, bool showBars = false);
    // A steady tempo from audio analysis, and the soundtrack clip moved so its
    // first downbeat (content seconds) lands on a bar line. One undo step.
    juce::Result setTempoFromAudio(Id soundtrackClip, double bpm, double downbeat, double& moved);
    // Stepped tempo changes by beat; musical clips follow, seconds content stays.
    juce::Result setTempoChange(double beat, double bpm, std::optional<double> replacing = std::nullopt, std::optional<bool> ramp = std::nullopt);
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
    // A track's row height (0: default). View state: no undo step, and undo
    // or redo keep the current heights. False when the track is missing.
    bool setTrackHeight(Id trackId, int height);
    // Several heights with one copy of the project; false when any is missing.
    bool setTrackHeights(const std::vector<std::pair<Id, int>>& heights);
    // Edits made inside a ViewChange (loop range, snapping, time display)
    // stay undoable but tell listeners nothing that playback depends on
    // changed, so the composition is not prepared again.
    struct ViewChange {
        explicit ViewChange(Document& owner) : document(owner), previous(owner.viewChange) { owner.viewChange = true; }
        ~ViewChange() { document.viewChange = previous; }
        Document& document;
        bool previous;
    };
    bool viewOnlyChange() const { return viewChange; }
    // Time display, snapping and the loop switch: applied without an undo
    // step, and kept by later undo and redo.
    void changeView(std::function<void(Composition&)> change);
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
    // Adds a camera framed like the one showing at `time`, so the output does
    // not jump. The first camera is the default view; later ones cut in at
    // `time`. One undo step.
    juce::Result addCamera(double time, Id& cameraId);
    // Removes a camera and its cuts.
    juce::Result removeCamera(Id camera);
    // Aims a camera at a clip or group origin (0: free) and/or places it in a
    // group's space (0: world).
    juce::Result setCameraRig(Id camera, Id target, Id parent);
    // Shared modulators and their routes live in the scope being edited.
    juce::Result addModulator(Modulator modulator, Id& id);
    juce::Result setModulator(Modulator modulator);
    juce::Result removeModulator(Id id);
    juce::Result addRoute(ModulationRoute route, Id& id);
    // Routes `modulator` to each of `properties` of `target` that it does not
    // drive yet, at a visible but contained default depth. One undo step.
    juce::Result routeModulator(Id modulator, Id target, const std::vector<std::string>& properties);
    static double routeAmount(const Project& project, Id target, const std::string& property);
    // Creates a modulator already driving one property, as one undo step.
    juce::Result addRoutedModulator(Modulator modulator, ModulationRoute route, Id& modulatorId);
    juce::Result setRoute(const ModulationRoute& route);
    juce::Result removeRoute(Id id);
    // Links (or unlinks, with nullopt) a property; refuses cycles.
    juce::Result setLink(Id target, const std::string& property, std::optional<PropertyLink> link);
    // Shows a gesture in progress without an undo step; commit() then records
    // it from where it began.
    void preview(Project project);
    void commit(juce::String label, Project before);
    juce::XmlElement save() const;
    juce::Result load(const juce::XmlElement& xml);
    // Worker-safe preparation; output is replaced only after full validation.
    static juce::Result prepareLoad(const juce::XmlElement& xml, Project& output, const std::atomic<bool>* cancel = nullptr);
    std::function<void()> onChanged;

    static Clip makeCompositionClip(Id id, const CompositionDefinition& definition, double time);
    static Clip makeClip(Id id, const Asset& asset, double time);
    // "Fern.lsystem" becomes "Fern 2.lsystem" when another source has the name.
    static juce::String uniqueAssetName(const Project& project, const juce::String& name) {
        const auto taken = [&project](const juce::String& candidate) {
            return std::any_of(project.assets.begin(), project.assets.end(), [&candidate](const auto& asset) { return asset != nullptr && asset->name == candidate; });
        };
        if (!taken(name)) { return name; }
        const auto dot = name.lastIndexOfChar('.');
        const auto stem = dot > 0 ? name.substring(0, dot) : name;
        const auto extension = dot > 0 ? name.substring(dot) : juce::String();
        for (int number = 2;; ++number) {
            const auto candidate = stem + " " + juce::String(number) + extension;
            if (!taken(candidate)) { return candidate; }
        }
    }

private:
    // The Scope belongs to the main composition: nothing inside another drives it.
    bool beamOutsideMain(Id target) const { return scopeId != 0 && target == state.beam.id; }
    Id highestId() const;
    juce::Result retempo(std::shared_ptr<const std::vector<TempoChange>> changes, juce::String label);
    juce::Result editMidi(Id clipId, juce::String label, const std::function<juce::Result(Clip&)>& operation);
    void apply(Project value);
    Project mergeScope(Project view) const;
    // An edited view, made consistent and merged into the whole project.
    Project finished(Project view) const;
    void record(const juce::String& label, Project view, bool joinPrevious = false);
    juce::String coalescingControl;
    std::uint64_t coalescingRevision = 0;
    double coalescingTime = 0;
    void refreshScope();
    struct Change;
    Project state, scopeView;
    Id scopeId = 0;
    Id lastId = 0;
    std::uint64_t projectGeneration = 0;
    std::uint64_t stateRevision = 0;
    bool carryView = true;
    // During undo and redo: the step's opposite snapshot, for carrying view options.
    const Project* carryOptionsFrom = nullptr;
    bool viewChange = false;
    juce::UndoManager& undo;
};
}
