#pragma once

#include "../model/Document.h"
#include "../model/LuaClipBake.h"
#include "../model/PropertySchema.h"
#include "../import/LuaBaker.h"
#include "../import/BakedSourceArchive.h"
#include <map>

namespace motion {
// Message-thread owner of background slider bakes. A Lua clip with slider
// properties gets its own frames: the script runs off the audio thread with
// the clip's slider curves (keys and their own oscillators) sampled per frame,
// over the clip's content. Results install as a cache (no undo step); stale
// results are discarded. Playback and export only ever read baked frames.
class LuaSliderBakes {
public:
    struct Plan {
        Id clip = 0;
        std::string key;
        juce::String name, script;
        BakeSettings settings;
        std::map<std::string, Curve> sliders;
        double bpm = 120;
    };

    explicit LuaSliderBakes(Document& owner) : document(owner) {}
    ~LuaSliderBakes() {
        for (auto& [id, job] : jobs) { job->cancelled.store(true); }
        worker.removeAllJobs(true, 10000);
    }
    std::function<void(const juce::String&, bool)> onStatus;

    // The bake a Lua clip needs, or nothing when it has no slider curves.
    static std::optional<Plan> planFor(const Asset& asset, const Clip& clip, const Tempo& tempo) {
        if (!asset.extension.equalsIgnoreCase(".lua") || asset.bakeKey.isEmpty()) { return std::nullopt; }
        Plan plan;
        for (const auto& spec : luaSliderSpecs()) {
            const auto found = clip.properties.find(std::string(spec.id));
            if (found != clip.properties.end()) { plan.sliders.emplace(found->first, found->second); }
        }
        if (plan.sliders.empty()) { return std::nullopt; }
        plan.clip = clip.id;
        plan.name = asset.name;
        plan.script = juce::String::fromUTF8(static_cast<const char*>(asset.data.getData()), static_cast<int>(asset.data.getSize()));
        plan.settings = asset.bakeSettings;
        plan.bpm = clip.curveBpm(tempo);
        // Cover the clip's content, within the source frame budget.
        const auto timing = clip.timing(tempo);
        const auto contentEnd = timing.localTime(timing.end());
        const auto longest = static_cast<double>(Document::maximumSourceFrames) / plan.settings.frameRate;
        plan.settings.duration = std::clamp(std::max(plan.settings.duration, contentEnd), 1.0 / plan.settings.frameRate, longest);
        plan.settings.duration = std::floor(plan.settings.duration * plan.settings.frameRate) / plan.settings.frameRate;
        juce::MemoryOutputStream key;
        key.writeString(asset.bakeKey);
        key.writeDouble(plan.settings.duration);
        key.writeDouble(plan.bpm);
        for (const auto& [name, curve] : plan.sliders) {
            key.writeString(juce::String(name));
            key.writeDouble(curve.base);
            key.writeBool(curve.modulation.enabled);
            key.writeInt(static_cast<int>(curve.modulation.waveform));
            for (const auto value : {curve.modulation.amount, curve.modulation.rateHz, curve.modulation.phase, curve.modulation.beatsPerCycle}) { key.writeDouble(value); }
            key.writeBool(curve.modulation.tempoSync);
            key.writeInt64(curve.modulation.seed);
            key.writeInt(static_cast<int>(curve.modulation.mode));
            for (const auto& point : curve.keyframes()) {
                for (const auto value : {point.time, point.value, point.incomingSlope, point.outgoingSlope, point.incomingInfluence, point.outgoingInfluence}) { key.writeDouble(value); }
                key.writeInt(static_cast<int>(point.interpolation));
            }
        }
        plan.key = juce::SHA256(key.getData(), key.getDataSize()).toHexString().toStdString();
        return plan;
    }

    // Starts bakes for clips whose frames are missing or stale, and clears
    // bakes from clips that no longer have sliders.
    void update() {
        const auto& project = document.mainProject();
        std::map<Id, Plan> wanted;
        std::vector<Id> clear;
        const auto scan = [&](const Composition& composition) {
            const auto tempo = composition.tempo();
            for (const auto& track : composition.tracks) {
                for (const auto& clip : track.clips) {
                    const auto asset = std::find_if(project.assets.begin(), project.assets.end(), [&](const auto& item) { return item != nullptr && item->id == clip.asset; });
                    auto plan = asset == project.assets.end() || clip.composition != 0 ? std::nullopt : planFor(**asset, clip, tempo);
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
        for (auto& [id, plan] : wanted) {
            const auto running = jobs.find(id);
            if (running != jobs.end()) {
                if (running->second->plan.key == plan.key) { continue; }
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
        if (onStatus) { onStatus("Baking Lua sliders for " + job->plan.name + "...", false); }
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
                    if (onStatus) { onStatus("Lua slider bake failed: " + error, true); }
                    return;
                }
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
                    if (clip.id != job.plan.clip) { continue; }
                    const auto asset = std::find_if(project.assets.begin(), project.assets.end(), [&](const auto& item) { return item != nullptr && item->id == clip.asset; });
                    const auto plan = asset == project.assets.end() ? std::nullopt : planFor(**asset, clip, composition.tempo());
                    wanted = plan.has_value() && plan->key == job.plan.key;
                }
            }
        };
        scan(project);
        for (const auto& definition : project.definitions) { if (definition != nullptr) { scan(*definition); } }
        return wanted;
    }

    Document& document;
    juce::ThreadPool worker {1};
    std::map<Id, std::shared_ptr<Job>> jobs;
    std::shared_ptr<int> alive = std::make_shared<int>(0);
    std::weak_ptr<int> aliveToken = alive;
};
}
