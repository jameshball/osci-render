#pragma once

#include "../render/BeamRenderer.h"
#include <vector>

// How the beam's time is shared at a moment, as the renderer plans it: each
// object's fraction of the drawing time, for the editor's readouts. Planned on
// the editor's composition, which bakes nothing, so what mirrors show, the
// camera's hatching and lines hidden behind meshes aren't counted. Owns its
// own planner, so it never touches the audio thread's.
class MotionBeamShares {
public:
    struct Share {
        motion::Id clip; // the id the editor selects: a nested placement for what plays inside it
        double fraction;
    };

    const std::vector<Share>& at(const motion::PreparedComposition& composition, double time, const motion::LiveSourceFrames* liveFrames) {
        shares.clear();
        beam.planCycle(composition, time, composition.sampleRate, liveFrames);
        double total = 0;
        for (const auto& segment : beam.plannedSegments()) {
            if (segment.kind != motion::BeamRenderer::Kind::draw || segment.clip == nullptr) { continue; }
            const auto id = segment.clip->editorId();
            const auto found = std::find_if(shares.begin(), shares.end(), [id](const auto& share) { return share.clip == id; });
            if (found != shares.end()) {
                found->fraction += static_cast<double>(segment.count);
            } else {
                shares.push_back({id, static_cast<double>(segment.count)});
            }
            total += static_cast<double>(segment.count);
        }
        for (auto& share : shares) { share.fraction = total > 0 ? share.fraction / total : 0.0; }
        return shares;
    }

    static double of(const std::vector<Share>& shares, motion::Id clip) {
        const auto found = std::find_if(shares.begin(), shares.end(), [clip](const auto& share) { return share.clip == clip; });
        return found != shares.end() ? found->fraction : 0.0;
    }

private:
    motion::BeamRenderer beam;
    std::vector<Share> shares;
};
