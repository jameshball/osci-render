#pragma once

#include <JuceHeader.h>
#include "Timeline.h"
#include "TimeGrid.h"
#include "Camera.h"
#include "Beam.h"
#include "Group.h"
#include "Generators.h"
#include "PreparedSource.h"
#include "LiveSourceIdentity.h"
#include "BlenderSourceSettings.h"
#include "../../../modules/osci_gui/visualiser/osci_VisualiserGeometry.h"
#include <limits>
#include "LoadNotes.h"
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
    // Its file's contents, or (when it is linked: see linksFromDisk) nothing,
    // and `linked` the file it is read from.
    juce::MemoryBlock data;
    juce::File linked;
    bool isLinked() const { return linked != juce::File(); }
    // Its linked file wasn't found: kept as it was, silent and drawing
    // nothing (or a video's frames the project holds), until it is.
    bool missing = false;
    BakeSettings bakeSettings;
    RasterSettings rasterSettings;
    TextSettings textSettings;
    int fractalDepth = 3;
    // A 3D object at its file's own size and place, rather than fitted to
    // the screen; and how much fitting scaled it (found when decoded).
    bool originalSize = false;
    double fittedScale = 1;
    juce::MemoryBlock bakedData;
    juce::String bakeKey;
    std::shared_ptr<const PreparedSource> source;
    std::shared_ptr<const PreparedAudio> audio;
};

// Composition content is independent of the project-wide media registry.
// Reusable compositions can share assets without duplicating their payloads.
struct Marker {
    // Markers closer than this share a position, which is not allowed.
    static constexpr double minimumSpacing = 1.0e-9;
    Id id = 0;
    double time = 0;
    juce::String name;
    static constexpr int longestName = 120;
    static bool validName(const juce::String& name) { return name.trim().isNotEmpty() && name.length() <= longestName && !name.containsAnyOf("\r\n"); }
    bool valid(double duration) const { return std::isfinite(time) && time >= 0 && time <= duration && validName(name); }
};

// Markers in time order; ties (which loading refuses) by identity.
inline void sortMarkers(std::vector<Marker>& markers) {
    std::sort(markers.begin(), markers.end(), [](const auto& a, const auto& b) { return a.time != b.time ? a.time < b.time : a.id < b.id; });
}

struct Composition {
    juce::String name = "Untitled";
    double duration = 180.0;
    double frameRate = 60.0;
    double bpm = 120.0; // initial tempo, from beat 0
    // Tempo changes after the initial tempo, stepped or ramped, by beat (immutable, shared).
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
    // A loop never reaches past the composition; one that no longer fits is dropped.
    void clampLoop() {
        loopEnd = std::min(loopEnd, duration);
        if (!hasLoop()) { loopStart = loopEnd = 0; looping = false; }
    }
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
    // Shared between project copies until changed (see SharedList).
    SharedList<Track> tracks;
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
inline bool holdsClip(const Track& track, Id clip) {
    return std::any_of(track.clips.begin(), track.clips.end(), [clip](const auto& item) { return item.id == clip; });
}
inline const Track* findClipTrack(const Composition& composition, Id clip) {
    const auto found = std::find_if(composition.tracks.begin(), composition.tracks.end(), [clip](const auto& track) { return holdsClip(track, clip); });
    return found != composition.tracks.end() ? &*found : nullptr;
}
inline const Clip* findClip(const Composition& composition, Id id) {
    const auto* track = findClipTrack(composition, id);
    if (track == nullptr) { return nullptr; }
    return &*std::find_if(track->clips.begin(), track->clips.end(), [id](const auto& clip) { return clip.id == id; });
}
// The same lookups, ready to write: the track is copied first if shared.
inline Track* changeClipTrack(Composition& composition, Id clip) {
    const auto found = std::find_if(composition.tracks.begin(), composition.tracks.end(), [clip](const auto& track) { return holdsClip(track, clip); });
    return found != composition.tracks.end() ? &composition.tracks.change(found) : nullptr;
}
inline Clip* changeClip(Composition& composition, Id id) {
    auto* track = changeClipTrack(composition, id);
    if (track == nullptr) { return nullptr; }
    return &*std::find_if(track->clips.begin(), track->clips.end(), [id](const auto& clip) { return clip.id == id; });
}

inline std::shared_ptr<const Asset> findAsset(const std::vector<std::shared_ptr<const Asset>>& assets, Id id) {
    const auto found = std::find_if(assets.begin(), assets.end(), [id](const auto& asset) { return asset != nullptr && asset->id == id; });
    return found != assets.end() ? *found : nullptr;
}

// Whether a source of this extension is an L-system (its clips have a depth).
inline bool isFractal(const juce::String& extension) { return extension.equalsIgnoreCase(".lsystem"); }

// The generator an asset is, if it is one: a ".generator" whose text is the
// generator's id.
inline const GeneratorDefinition* generatorOf(const Asset& asset) {
    if (!asset.extension.equalsIgnoreCase(juce::String(generatorExtension.data(), generatorExtension.size()))) { return nullptr; }
    return findGenerator(asset.data.toString().trim().toStdString());
}

// How a project is exported: its frame (the Scope's canvas too, so what is
// framed is what is exported), the video codec (as VideoCodec numbers it),
// the encoder's quality (its CRF: 1 best to 51, 0 lossless) and whether the
// soundtrack goes with it. Its frame is a size the renderer draws: even,
// within the canvas's limits.
struct ExportSettings {
    int width = 1024, height = 1024;
    int codec = 0;
    int crf = 16;
    bool soundtrack = true;
    static constexpr int codecs = 5;
    bool operator==(const ExportSettings&) const = default;
    static bool drawable(int size) { return VisualiserGeometry::sanitiseCanvasDimension(size) == size; }
    bool valid() const { return drawable(width) && drawable(height) && codec >= 0 && codec < codecs && crf >= 0 && crf <= 51; }
};

struct Project : Composition {
    // The output display belongs to the whole project, not to one composition.
    ScopeProfile scope;
    // The rate the beam is drawn at, in Hz: what the Scope shows and exports
    // are, resampled to the audio device as it plays. Higher draws more of
    // the beam each frame, for a dense scene drawn cleanly.
    double sampleRate = defaultSampleRate;
    // Whole times each frame is drawn: the beam's base frequency is the frame
    // rate times this. 0: as few as reach 45 Hz, so scopes don't flicker.
    int beamCycles = 0;
    static constexpr int mostBeamCycles = 8;
    ExportSettings exportSettings;
    // What the project is, in a sentence or two: shown under its name on
    // the start screen.
    juce::String description;
    static constexpr int longestDescription = 200;
    static constexpr double defaultSampleRate = 192000, lowestSampleRate = 22050, highestSampleRate = 768000;
    static bool validSampleRate(double rate) { return std::isfinite(rate) && rate >= lowestSampleRate && rate <= highestSampleRate; }
    // The Scope's picture: animatable beam and display properties.
    Beam beam;
    std::vector<std::shared_ptr<const Asset>> assets;
    std::vector<std::shared_ptr<const CompositionDefinition>> definitions;
};

inline std::shared_ptr<const CompositionDefinition> findDefinition(const Project& project, Id id) {
    const auto found = std::find_if(project.definitions.begin(), project.definitions.end(), [id](const auto& definition) { return definition != nullptr && definition->id == id; });
    return found != project.definitions.end() ? *found : nullptr;
}

// Editable state belongs to the message thread. Project copies (undo steps,
// previews, preparation) share media, definitions and every track an edit did
// not change; an edit copies only the tracks it changes.
class Document : public juce::ChangeBroadcaster {
public:
    explicit Document(juce::UndoManager& undo) : undo(undo) {}
    const Project& project() const { return scopeId == 0 ? state : scopeView; }
    const Project& mainProject() const { return state; }
    Id editingComposition() const { return scopeId; }
    juce::Result enterComposition(Id id);
    juce::Result setMarker(Id id, double time, juce::String name);
    juce::Result removeMarker(Id id);
    // Changes when the document is replaced (a new or loaded project): work
    // started for the old one is dropped.
    std::uint64_t generation() const { return projectGeneration; }
    // Changes as well when the composition edited changes (entering one, or
    // the one entered going away): edits in progress to the scope give up.
    std::uint64_t scopeGeneration() const { return scopeChanges; }
    std::uint64_t revision() const { return stateRevision; }
    // Whether the project has changed since it was opened, made or last
    // saved (an undo back to that point still counts as a change).
    bool unsaved() const { return contentRevision != savedRevision; }
    void markSaved() { savedRevision = contentRevision; }
    // Reopened with changes its file doesn't have (a session that had them).
    void markUnsaved() { savedRevision = std::numeric_limits<std::uint64_t>::max(); }
    // Identities stay below the Scope's reserved one, so it never collides.
    static constexpr Id maximumId = beamIdentity - 1;
    // Loaded projects leave this many identities free above their highest,
    // so editing them never runs out.
    static constexpr Id highestLoadedId = maximumId - (Id {1} << 40);
    Id newId() {
        jassert(lastId < maximumId);
        return ++lastId;
    }
    void edit(juce::String label, std::function<void(Project&)> operation);
    // Changes what the newest undo step made, as part of that step.
    void amend(std::function<void(Project&)> operation);
    // An edit of the whole project wherever the scope is, for what reaches
    // past the composition being edited (sources, and clips everywhere that
    // use them). The operation keeps references whole itself.
    void editProject(juce::String label, std::function<void(Project&)> operation);
    // Records nothing, and changes no revision, when the operation declines.
    bool tryEdit(juce::String label, std::function<bool(Project&)> operation);
    // Repeated changes to one control (wheel, arrow keys) within a second
    // join a single undo step instead of one step per increment.
    void editCoalesced(juce::String label, const juce::String& control, std::function<void(Project&)> operation);
    // As editCoalesced, recording nothing when the operation declines.
    bool tryEditCoalesced(juce::String label, const juce::String& control, std::function<bool(Project&)> operation);
    // A control dragged through edits of its own (a modulator's rate): its
    // changes join one undo step however long the drag, and cancelling
    // takes that step back.
    void beginHeld(const juce::String& control);
    void endHeld();
    void cancelHeld();
    void reset(Project project);
    juce::Result changeTempo(double bpm);
    // Replace the initial tempo and every change in one undo step.
    juce::Result setTempoMap(double initialBpm, std::shared_ptr<const std::vector<TempoChange>> changes, juce::String label);
    // A steady tempo from audio analysis, and the soundtrack clip moved so its
    // first downbeat (content seconds) lands on a bar line. One undo step.
    juce::Result setTempoFromAudio(Id soundtrackClip, double bpm, double downbeat, double& moved);
    // Tempo changes by beat, stepped or ramped; musical clips follow, seconds content stays.
    juce::Result setTempoChange(double beat, double bpm, std::optional<double> replacing = std::nullopt, std::optional<bool> ramp = std::nullopt);
    juce::Result removeTempoChange(double beat);
    juce::Result addBlenderSource(juce::String name, BlenderSourceSettings settings, Id& id);
    // A picture another app shares, by its app and name (see live/TextureSource.h).
    juce::Result addTextureSource(const juce::MemoryBlock& sender, const juce::String& name, RasterSettings settings, Id& id);
    // How it's traced, as one undo step for a run of changes to it.
    juce::Result setTextureSettings(Id id, RasterSettings settings);
    juce::Result setBlenderSource(Id id, juce::String name, BlenderSourceSettings settings);
    juce::Result setClipTiming(Id clipId, ClipTiming resolvedSeconds);
    // Several clips in one undo step; placement is checked after every change applies.
    juce::Result setClipTimings(const std::vector<std::pair<Id, ClipTiming>>& timings);
    // Clips moved by `seconds`, as a drag moves them (none onto another, none
    // on a locked track, none before the start); nudges in a row are one
    // undo step.
    juce::Result nudgeClips(const std::vector<Id>& clips, double seconds);
    juce::Result duplicateClip(Id sourceId, Id& duplicateId);
    juce::Result makeSourceUnique(Id clipId, const std::shared_ptr<const Asset>& expected, Asset copy, Id& madeId);
    std::size_t compositionReferenceCount(Id definition) const;
    juce::Result removeComposition(Id definition);
    juce::Result insertComposition(Id definition, double time, Id track, Id group, Id& clipId);
    bool canReferenceComposition(Id definition) const;
    juce::Result makeCompositionUnique(Id clipId, Id& definitionId);
    juce::Result createComposition(const std::vector<Id>& clipIds, juce::String name, Id& instanceId);
    // A new group holding `ids` (tracks or groups), where they sat when they
    // shared a parent; `made` is its id.
    juce::Result groupTracks(const std::vector<Id>& ids, Id& made);
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
    // Splits clips' sources into two prepared sources each, in one undo step:
    // each clip keeps its `rest` (named as its source was) and its own name,
    // and a copy of it (timing, keys, effects and routes) plays its `part`,
    // named `name`, on a new track just above, which keeps the track's
    // effects, mute and solo. An old source leaves the library when no other
    // clip uses it. Takes over both assets' identities and names.
    struct PartSplit { Id clip = 0; std::shared_ptr<const Asset> part, rest; juce::String name; };
    juce::Result extractParts(const std::vector<PartSplit>& splits, std::vector<Id>& partClips);
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
    // clip or composition references; one undo step.
    juce::Result removeUnusedAssets(std::vector<Id> assetIds, int& removed);
    std::size_t assetUses(Id assetId) const;
    // The live sources clips draw, anywhere in the project: Export draws
    // offline, so it needs them captured first.
    std::vector<std::shared_ptr<const Asset>> liveSourcesInUse() const;
    // Every clip of `from` draws `to` instead, keeping its keys and effects:
    // a capture in place of the live source it recorded.
    juce::Result drawSourceInstead(Id from, Id to);
    // `project` without the clips of its live sources, to export the rest.
    static Project withoutLiveSources(Project project);
    // Camera cuts form the camera track: each shows one camera over a range.
    // A cut at `time` ends the cut it falls in and lasts until the next cut.
    juce::Result cutToCamera(Id camera, double time, Id& cutId);
    juce::Result setCutCamera(Id cut, Id camera);
    // Moves or trims a cut; it may not overlap another cut or leave the
    // project, and keeps at least one frame.
    juce::Result setCutRange(Id cut, double start, double end, juce::String label = "Move camera cut");
    // Sets a cut's span in a project, if it stays inside it, lasts a frame
    // and overlaps no other; the project is unchanged otherwise.
    static juce::Result applyCutRange(Project& project, Id cut, double start, double end);
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
    // Moves an effect to another owner (0: the whole composition), or copies
    // it there, in one undo step.
    juce::Result moveEffect(Id effect, Id owner, bool copy);
    juce::Result routeModulator(Id modulator, Id target, const std::vector<std::string>& properties);
    // The project as routeModulator would leave it, recording nothing: a
    // preview while a modulator is dragged over a property. None when no new
    // route would be made.
    std::optional<Project> previewRoute(Id modulator, Id target, const std::vector<std::string>& properties) const;
    static double routeAmount(const Project& project, Id target, const std::string& property);
    // Creates a modulator already driving one property, as one undo step.
    juce::Result addRoutedModulator(Modulator modulator, ModulationRoute route, Id& modulatorId);
    juce::Result setRoute(const ModulationRoute& route);
    juce::Result removeRoute(Id id);
    // Links (or unlinks, with nullopt) a property; refuses cycles.
    juce::Result setLink(Id target, const std::string& property, std::optional<PropertyLink> link);
    // A property's animation gone in one step: its keys, each property held at
    // its value at `projectTime`. `what` names it for undo ("position").
    juce::Result removeKeyframes(Id target, const std::vector<std::string>& properties, double projectTime, const juce::String& what);
    // Shows a gesture in progress without an undo step; commit() then records
    // it from where it began.
    void preview(Project project);
    void commit(juce::String label, Project before);
    juce::XmlElement save() const;
    // Any project as save() writes the document's.
    static juce::XmlElement save(const Project& project);
    juce::Result load(const juce::XmlElement& xml, LoadNotes* notes = nullptr);
    // Worker-safe preparation; output is replaced only after full validation.
    // What a project from another version has that this one can't use is
    // left out (or put right), and said in `notes`; only a file that isn't
    // a project, or is damaged past using, fails.
    static juce::Result prepareLoad(const juce::XmlElement& xml, Project& output, const std::atomic<bool>* cancel = nullptr, LoadNotes* notes = nullptr);
    std::function<void()> onChanged;
    // Called as an edit begins its own undo step: whatever else shares the
    // undo manager (parameter drags) starts a new step after it.
    std::function<void()> onUndoStep;
    // After an edit is recorded (not an undo, redo or preview), with the
    // composition edited as it was before it, for a look at what changed.
    std::function<void(const Project& before)> onEdited;

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
    // The timings set, as one undo step (`control` merges steps in a row).
    juce::Result applyClipTimings(const std::vector<std::pair<Id, ClipTiming>>& timings, const juce::String& label, const juce::String& control);
    // The Scope belongs to the main composition: nothing inside another drives it.
    bool beamOutsideMain(Id target) const { return scopeId != 0 && target == state.beam.id; }
    Id highestId() const;
    juce::Result retempo(std::shared_ptr<const std::vector<TempoChange>> changes, juce::String label);
    // `revises`: whether edits in progress (whose revision moves on) give
    // up; a view option changing (loop, snapping) leaves them be.
    void apply(Project value, bool revises = true);
    Project mergeScope(Project view) const;
    // An edited view, made consistent and merged into the whole project.
    Project finished(Project view) const;
    void record(const juce::String& label, Project view, bool joinPrevious = false);
    juce::String coalescingControl;
    std::uint64_t coalescingRevision = 0;
    double coalescingTime = 0;
    // The control held (see beginHeld), and whether it recorded a step yet.
    std::optional<juce::String> held;
    bool heldRecorded = false;
    void refreshScope();
    struct Change;
    Project state, scopeView;
    Id scopeId = 0;
    Id lastId = 0;
    std::uint64_t projectGeneration = 0, scopeChanges = 0;
    std::uint64_t stateRevision = 0, contentRevision = 0, savedRevision = 0;
    // The project before a gesture's first preview, until a step is recorded:
    // where an edit made meanwhile undoes to.
    std::optional<Project> previewBase;
    // The project at that first preview, as it was: what view options a
    // gesture's previews started from.
    std::optional<Project> previewOrigin;
    bool carryView = true;
    // During undo and redo: the step's opposite snapshot, for carrying view options.
    const Project* carryOptionsFrom = nullptr;
    bool viewChange = false;
    juce::UndoManager& undo;
};
}
