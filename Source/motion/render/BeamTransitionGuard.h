#pragma once

#include <JuceHeader.h>

namespace motion {
// Audio-thread-only state for discontinuities that cannot be predicted from
// neighbouring timeline samples (seek, snapshot replacement, resume, loop).
class BeamTransitionGuard {
public:
    void begin() { remaining = 2; }
    osci::Point apply(osci::Point point) {
        if (remaining > 0) {
            if (remaining == 2) { point.x = last.x; point.y = last.y; point.z = last.z; }
            point.r = point.g = point.b = 0;
            --remaining;
        }
        last = point;
        return point;
    }
private:
    osci::Point last {0, 0, 0, 0, 0, 0};
    int remaining = 2;
};
}
