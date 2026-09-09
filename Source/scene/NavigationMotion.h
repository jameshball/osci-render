#pragma once

#include <osci_render_core/osci_render_core.h>

namespace scene {
// Integrate held input in seconds, independently of key-repeat and frame rate.
struct NavigationMotion {
    osci::Point velocity;

    osci::Point advance(osci::Point direction, float seconds, float speed) {
        const float length = direction.magnitude();
        if (length > 0) { direction.scale(speed / length, speed / length, speed / length); }
        const float decay = std::exp(-18.0f * seconds);
        osci::Point distance;
        for (int i = 0; i < 3; ++i) {
            const float difference = velocity[i] - direction[i];
            distance[i] = direction[i] * seconds + difference * (1.0f - decay) / 18.0f;
            velocity[i] = direction[i] + difference * decay;
        }
        return distance;
    }
};
}
