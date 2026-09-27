#pragma once

#include "PreparedAudio.h"
#include "../model/CompositionExpansion.h"

namespace motion {
// Constructed with the visual snapshot on the message thread. Sampling owns no
// temporary shared pointers and performs no allocation or mutable playback work.
class PreparedSoundtrack {
public:
    template <typename ProjectType>
    explicit PreparedSoundtrack(const ProjectType& project, const std::atomic<bool>* cancel = nullptr) {
        const auto expanded = expandComposition(project, [&](const auto&, const auto& stages) {
            const auto& leaf = stages.back();
            if (leaf.track->kind != TrackKind::audio) { return; }
            const auto asset = std::find_if(project.assets.begin(), project.assets.end(),
                [&](const auto& value) { return value != nullptr && value->id == leaf.clip->asset; });
            if (asset == project.assets.end() || (*asset)->audio == nullptr) { return; }
            ClipSource source {leaf.clipClock, (*asset)->audio, {}};
            for (const auto& stage : stages) {
                const auto gain = stage.clip->properties.find("gain");
                const auto pan = stage.clip->properties.find("pan");
                source.mix.push_back({stage.clipClock,
                    gain == stage.clip->properties.end() ? Curve(1) : gain->second,
                    pan == stage.clip->properties.end() ? Curve(0) : pan->second,
                    stage.clip->curveBpm(stage.bpm)});
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
                const auto gainValue = stage.gain.evaluate(stageTime, stage.contentBpm);
                const auto panValue = stage.pan.evaluate(stageTime, stage.contentBpm);
                if (!std::isfinite(gainValue) || !std::isfinite(panValue)) { leftGain = rightGain = 0; break; }
                const auto gain = std::clamp(gainValue, 0.0, 4.0);
                const auto pan = std::clamp(panValue, -1.0, 1.0);
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
        double contentBpm;
    };
    struct ClipSource {
        ClipTiming clock;
        std::shared_ptr<const PreparedAudio> audio;
        std::vector<MixStage> mix;
    };
    std::vector<ClipSource> clips;
};
}
