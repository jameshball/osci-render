#pragma once

#include "../model/PreparedAudio.h"
#include "../model/CompositionExpansion.h"
#include "../model/PropertySpecs.h"

namespace motion {
// Built with each prepared composition, off the audio thread. Sampling owns no
// temporary shared pointers and performs no allocation or mutable playback work.
class PreparedSoundtrack {
public:
    static constexpr const PropertySpec& gainSpec = audioPropertySpecs[0];
    static constexpr const PropertySpec& panSpec = audioPropertySpecs[1];
    static_assert(gainSpec.id == "gain" && panSpec.id == "pan");

    template <typename ProjectType>
    explicit PreparedSoundtrack(const ProjectType& project, const std::atomic<bool>* cancel = nullptr) {
        // A concrete stages type and a plain loop: clang 18 (Ubuntu 24.04's
        // default compiler) crashes instantiating this generic visitor when it
        // takes generic stages or nests a lambda.
        const auto expanded = expandComposition(project, [&](const auto&, const std::vector<CompositionStage>& stages) {
            const auto& leaf = stages.back();
            if (leaf.track->kind != TrackKind::audio) { return; }
            // Generic over the project type, so not findAsset.
            const typename decltype(project.assets)::value_type* found = nullptr;
            for (const auto& candidate : project.assets) {
                if (candidate != nullptr && candidate->id == leaf.clip->asset) {
                    found = &candidate;
                    break;
                }
            }
            if (found == nullptr || (*found)->audio == nullptr) { return; }
            const auto& asset = *found;
            ClipSource source {leaf.clipClock, asset->audio, {}};
            for (const auto& stage : stages) {
                const auto gain = stage.clip->properties.find(gainSpec.id);
                const auto pan = stage.clip->properties.find(panSpec.id);
                source.mix.push_back({stage.clipClock,
                    gain == stage.clip->properties.end() ? Curve(gainSpec.defaultValue) : gain->second,
                    pan == stage.clip->properties.end() ? Curve(panSpec.defaultValue) : pan->second});
            }
            clips.push_back(std::move(source));
        }, cancel);
        preparationError = expanded.error;
        if (!expanded) { clips.clear(); }
    }

    std::string preparationError;

    PreparedAudio::Stereo sample(double projectTime) const {
        if (!std::isfinite(projectTime)) {
            return {};
        }
        double left = 0, right = 0;
        for (const auto& clip : clips) {
            if (projectTime < clip.clock.start || projectTime >= clip.clock.end()) {
                continue;
            }
            const auto local = clip.clock.localTime(projectTime);
            if (!std::isfinite(local)) {
                continue;
            }
            double leftGain = 1, rightGain = 1;
            for (const auto& stage : clip.mix) {
                const auto stageTime = stage.clock.localTime(projectTime);
                const auto gainValue = stage.gain.evaluate(stageTime);
                const auto panValue = stage.pan.evaluate(stageTime);
                if (!std::isfinite(gainValue) || !std::isfinite(panValue)) { leftGain = rightGain = 0; break; }
                const auto gain = gainSpec.clamp(gainValue);
                const auto pan = panSpec.clamp(panValue);
                leftGain *= gain * (pan > 0 ? 1.0 - pan : 1.0);
                rightGain *= gain * (pan < 0 ? 1.0 + pan : 1.0);
            }
            const auto value = clip.audio->sample(local);
            left += value.left * leftGain;
            right += value.right * rightGain;
        }
        return { finiteFloat(left), finiteFloat(right) };
    }

private:
    static float finiteFloat(double value) {
        constexpr auto limit = static_cast<double>(std::numeric_limits<float>::max());
        return std::isfinite(value) ? static_cast<float>(std::clamp(value, -limit, limit)) : 0.0f;
    }
    struct MixStage {
        ClipTiming clock;
        Curve gain, pan;
    };
    struct ClipSource {
        ClipTiming clock;
        std::shared_ptr<const PreparedAudio> audio;
        std::vector<MixStage> mix;
    };
    std::vector<ClipSource> clips;
};
}
