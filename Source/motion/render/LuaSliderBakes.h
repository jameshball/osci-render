#pragma once

#include "../model/Document.h"
#include "../model/LuaClipBake.h"
#include "../model/PropertySchema.h"
#include "../import/LuaBaker.h"
#include "../import/BakedSourceArchive.h"
#include "PreparedDrivers.h"
#include <functional>
#include <map>

namespace motion {
// Message-thread owner of background slider bakes. A Lua clip with slider
// properties gets its own frames: the script runs off the audio thread with
// the clip's slider curves (keys and their own oscillators) sampled per frame,
// over the clip's content. Results install as a cache (no undo step); stale
// results are discarded. Playback and export only ever read baked frames.
class LuaSliderBakes : private juce::Timer {
public:

    explicit LuaSliderBakes(Document& owner) : document(owner) {}
    ~LuaSliderBakes() override {
        stopTimer();
        for (auto& [id, job] : jobs) { job->cancelled.store(true); }
        worker.removeAllJobs(true, 2000);
    }
    // Coalesces bursts of change messages (drags, previews) into one scan.
    void requestUpdate() { startTimer(150); }
    std::function<void(const juce::String&, bool)> onStatus;

    using Plan = LuaSliderPlan;
    static std::optional<Plan> planFor(const Asset& asset, const Clip& clip, const Tempo& tempo) { return luaSliderPlan(asset, clip, tempo); }
    // As above, with routed modulators and property links attached to the
    // slider curves, so they reach the script like any other property. The
    // key then covers the routes, their modulators (and MIDI sources) and the
    // linked curves. Soundtrack loudness is not available at bake time.
    static std::optional<Plan> planFor(const Asset& asset, const Clip& clip, const Composition& composition) {
        auto plan = luaSliderPlan(asset, clip, composition.tempo());
        if (!plan.has_value()) { return plan; }
        PreparedDrivers drivers(nullptr);
        juce::MemoryOutputStream key;
        bool driven = false;
        const auto writeCurve = [&key](const Curve& curve) {
            key.writeDouble(curve.base);
            for (const auto& point : curve.keyframes()) {
                for (const auto value : {point.time, point.value, point.incomingSlope, point.outgoingSlope, point.incomingInfluence, point.outgoingInfluence}) { key.writeDouble(value); }
                key.writeInt(static_cast<int>(point.interpolation));
            }
        };
        const auto writeClip = [&](Id id) {
            for (const auto& track : composition.tracks) {
                for (const auto& other : track.clips) {
                    if (other.id != id) { continue; }
                    const auto timing = other.timing(composition.tempo());
                    for (const auto value : {timing.start, timing.end(), timing.offset, timing.rate}) { key.writeDouble(value); }
                    // The notes themselves, so a saved bake still matches after loading.
                    if (other.midi == nullptr) { continue; }
                    for (const auto& note : other.midi->notes()) {
                        for (const auto value : {note.start, note.duration}) { key.writeDouble(value); }
                        for (const auto value : {note.pitch, note.velocity, note.channel}) { key.writeInt(value); }
                    }
                    for (const auto& control : other.midi->controls()) {
                        key.writeDouble(control.beat);
                        for (const auto value : {control.channel, control.number, control.value}) { key.writeInt(value); }
                    }
                }
            }
        };
        // Routes on (owner, property), then its link, following link sources
        // as deep as the drivers do.
        std::function<void(Id, const std::string&, const Curve*, int)> writeDrivers = [&](Id owner, const std::string& property, const Curve* curve, int depth) {
            for (const auto& route : composition.routes) {
                if (route.target != owner || route.property != property) { continue; }
                key.writeDouble(route.amount);
                key.writeInt(static_cast<int>(route.mode));
                for (const auto& modulator : composition.modulators) {
                    if (modulator.id != route.modulator) { continue; }
                    key.writeInt(static_cast<int>(modulator.kind));
                    // The shape's own depth, mode and switch are unused (and unsaved).
                    key.writeInt(static_cast<int>(modulator.shape.waveform));
                    for (const auto value : {modulator.shape.rateHz, modulator.shape.phase, modulator.shape.beatsPerCycle}) { key.writeDouble(value); }
                    key.writeBool(modulator.shape.tempoSync);
                    key.writeInt64(modulator.shape.seed);
                    for (const auto value : {modulator.attack, modulator.decay, modulator.sustain, modulator.release, modulator.velocity}) { key.writeDouble(value); }
                    for (const auto value : {modulator.lowestPitch, modulator.highestPitch, modulator.controller, modulator.controllerChannel}) { key.writeInt(value); }
                    writeClip(modulator.source);
                }
            }
            if (curve == nullptr || !curve->link.has_value() || depth > 64) { return; }
            key.writeInt64(static_cast<juce::int64>(curve->link->source));
            key.writeString(juce::String(curve->link->property));
            for (const auto value : {curve->link->scale, curve->link->offset, curve->link->delay}) { key.writeDouble(value); }
            const auto* source = findPropertyCurve(composition, curve->link->source, curve->link->property);
            if (source != nullptr) { writeCurve(*source); }
            writeClip(curve->link->source);
            writeDrivers(curve->link->source, curve->link->property, source, depth + 1);
        };
        for (auto& [name, curve] : plan->sliders) {
            const auto routed = std::any_of(composition.routes.begin(), composition.routes.end(), [&](const auto& route) { return route.target == clip.id && route.property == name; });
            if (!routed && !curve.link.has_value()) { continue; }
            driven = true;
            key.writeString(juce::String(name));
            writeDrivers(clip.id, name, &curve, 0);
            drivers.drive(curve, composition, ClipTiming {}, clip.id, name);
        }
        if (driven) {
            // Drivers run in composition time: the clip's placement and the
            // tempo map decide where they land in the bake.
            writeClip(clip.id);
            key.writeDouble(composition.bpm);
            if (composition.tempoChanges != nullptr) {
                for (const auto& change : *composition.tempoChanges) {
                    for (const auto value : {change.beat, change.bpm}) { key.writeDouble(value); }
                    key.writeBool(change.ramp);
                }
            }
            key.writeString(juce::String(plan->key));
            plan->key = juce::SHA256(key.getData(), key.getDataSize()).toHexString().toStdString();
        }
        return plan;
    }

    // Starts bakes for clips whose frames are missing or stale, and clears
    // bakes from clips that no longer have sliders.
    void update() {
        const auto& project = document.mainProject();
        std::map<Id, Plan> wanted;
        std::vector<Id> clear;
        const auto scan = [&](const Composition& composition) {
            for (const auto& track : composition.tracks) {
                for (const auto& clip : track.clips) {
                    const auto asset = std::find_if(project.assets.begin(), project.assets.end(), [&](const auto& item) { return item != nullptr && item->id == clip.asset; });
                    auto plan = asset == project.assets.end() || clip.composition != 0 ? std::nullopt : planFor(**asset, clip, composition);
                    if (!plan.has_value()) {
                        if (clip.luaBake != nullptr) { clear.push_back(clip.id); }
                        continue;
                    }
                    if (clip.luaBake == nullptr || clip.luaBake->key != plan->key) { wanted.emplace(clip.id, std::move(*plan)); }
                }
            }
        };
        scan(project);
        for (const auto& definition : project.definitions) { if (definition != nullptr) { scan(*definition); } }
        for (const auto id : clear) { document.setLuaBake(id, nullptr); }
        // Jobs whose clip no longer wants them (deleted, sliders removed)
        // stop, so they do not hold up the single worker.
        for (auto& [id, job] : jobs) {
            if (!wanted.contains(id)) { job->cancelled.store(true); }
        }
        for (auto& [id, plan] : wanted) {
            const auto failed = failedKeys.find(id);
            if (failed != failedKeys.end() && failed->second == plan.key) { continue; }
            const auto running = jobs.find(id);
            if (running != jobs.end()) {
                if (running->second->plan.key == plan.key && !running->second->cancelled.load()) { continue; }
                running->second->cancelled.store(true);
            }
            start(std::move(plan));
        }
    }
    bool busy() const { return !jobs.empty(); }

private:
    struct Job {
        Plan plan;
        std::atomic<bool> cancelled {false};
    };
    void start(Plan plan) {
        auto job = std::make_shared<Job>();
        job->plan = std::move(plan);
        const auto id = job->plan.clip;
        jobs[id] = job;
        if (onStatus) { onStatus("Baking Lua sliders for " + job->plan.name + " (" + juce::String(static_cast<int>(job->plan.settings.frameCount())) + " frames)...", false); }
        auto alive = aliveToken;
        worker.addJob([this, job, alive] {
            auto frames = LuaBaker::bake(job->plan.name, job->plan.script, job->plan.settings, &job->cancelled, nullptr, [&job](double seconds, double* values) {
                for (const auto& spec : luaSliderSpecs()) {
                    const auto found = job->plan.sliders.find(std::string(spec.id));
                    const auto index = static_cast<std::size_t>(spec.id.back() - 'a');
                    values[index] = found == job->plan.sliders.end() ? 0.0 : std::clamp(found->second.evaluate(seconds, job->plan.bpm), 0.0, 1.0);
                }
            });
            std::shared_ptr<LuaClipBake> bake;
            juce::String error;
            if (frames) {
                auto archive = BakedSourceArchive::encode(*frames.source);
                if (archive) {
                    bake = std::make_shared<LuaClipBake>();
                    bake->key = job->plan.key;
                    bake->source = std::make_shared<const PreparedSource>(frames.source);
                    bake->archive = std::move(archive.data);
                } else {
                    error = archive.error;
                }
            } else {
                error = juce::String(frames.error);
            }
            juce::MessageManager::callAsync([this, job, alive, bake, error] {
                if (alive.expired()) { return; }
                const auto current = jobs.find(job->plan.clip);
                if (current == jobs.end() || current->second != job) { return; }
                jobs.erase(current);
                if (job->cancelled.load()) { return; }
                if (bake == nullptr) {
                    // Remembered, so a broken script is not re-run on every edit.
                    failedKeys[job->plan.clip] = job->plan.key;
                    if (onStatus) { onStatus("Lua slider bake failed: " + error, true); }
                    return;
                }
                failedKeys.erase(job->plan.clip);
                if (!stillWanted(*job)) { update(); return; }
                document.setLuaBake(job->plan.clip, bake);
                if (onStatus && jobs.empty()) { onStatus("Lua sliders baked.", false); }
            });
        });
    }
    bool stillWanted(const Job& job) const {
        const auto& project = document.mainProject();
        bool wanted = false;
        const auto scan = [&](const Composition& composition) {
            for (const auto& track : composition.tracks) {
                for (const auto& clip : track.clips) {
                    if (clip.id != job.plan.clip || clip.composition != 0) { continue; }
                    const auto asset = std::find_if(project.assets.begin(), project.assets.end(), [&](const auto& item) { return item != nullptr && item->id == clip.asset; });
                    const auto plan = asset == project.assets.end() ? std::nullopt : planFor(**asset, clip, composition);
                    wanted = plan.has_value() && plan->key == job.plan.key;
                }
            }
        };
        scan(project);
        for (const auto& definition : project.definitions) { if (definition != nullptr) { scan(*definition); } }
        return wanted;
    }

    void timerCallback() override {
        stopTimer();
        update();
    }

    Document& document;
    std::map<Id, std::string> failedKeys;
    juce::ThreadPool worker {1};
    std::map<Id, std::shared_ptr<Job>> jobs;
    std::shared_ptr<int> alive = std::make_shared<int>(0);
    std::weak_ptr<int> aliveToken = alive;
};
}
