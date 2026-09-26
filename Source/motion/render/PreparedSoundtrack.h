#pragma once

#include "PreparedAudio.h"
#include "../model/Group.h"

namespace motion {
// Constructed with the visual snapshot on the message thread. Sampling owns no
// temporary shared pointers and performs no allocation or mutable playback work.
class PreparedSoundtrack {
public:
    template <typename ProjectType>
    explicit PreparedSoundtrack(const ProjectType& project) : bpm(project.bpm) {
        for (const auto& track : project.tracks) {
            if (track.kind != TrackKind::audio || !trackIsAudible(project, track)) {
                continue;
            }
            for (const auto& clip : track.clips) {
                const auto asset = std::find_if(project.assets.begin(), project.assets.end(),
                    [&](const auto& value) { return value->id == clip.asset; });
                if (!clip.valid() || asset == project.assets.end() || (*asset)->audio == nullptr) {
                    continue;
                }
                const auto gain = clip.properties.find("gain");
                const auto pan = clip.properties.find("pan");
                const auto timing = clip.timing(project.bpm);
                clips.push_back({ timing.start, timing.end(), timing.offset, timing.rate, (*asset)->audio,
                    gain == clip.properties.end() ? Curve(1) : gain->second,
                    pan == clip.properties.end() ? Curve(0) : pan->second, clip.curveBpm(project.bpm) });
            }
        }
    }

    PreparedAudio::Stereo sample(double projectTime) const {
        if (!std::isfinite(projectTime)) {
            return {};
        }
        double left = 0, right = 0;
        for (const auto& clip : clips) {
            if (projectTime < clip.start || projectTime >= clip.end) {
                continue;
            }
            const auto local = clip.offset + (projectTime - clip.start) * clip.rate;
            if (!std::isfinite(local)) {
                continue;
            }
            const auto gainValue = clip.gain.evaluate(local, clip.contentBpm);
            const auto panValue = clip.pan.evaluate(local, clip.contentBpm);
            if (!std::isfinite(gainValue) || !std::isfinite(panValue)) {
                continue;
            }
            const auto gain = std::clamp(gainValue, 0.0, 4.0);
            const auto pan = std::clamp(panValue, -1.0, 1.0);
            const auto value = clip.audio->sample(local);
            left += value.left * gain * (pan > 0 ? 1.0 - pan : 1.0);
            right += value.right * gain * (pan < 0 ? 1.0 + pan : 1.0);
        }
        return { finiteFloat(left), finiteFloat(right) };
    }

private:
    static float finiteFloat(double value) {
        constexpr auto limit = static_cast<double>(std::numeric_limits<float>::max());
        return std::isfinite(value) ? static_cast<float>(std::clamp(value, -limit, limit)) : 0.0f;
    }
    struct ClipSource {
        double start, end, offset, rate;
        std::shared_ptr<const PreparedAudio> audio;
        Curve gain, pan;
        double contentBpm;
    };
    double bpm;
    std::vector<ClipSource> clips;
};
}
