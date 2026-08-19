#pragma once

#include <JuceHeader.h>
#include <vector>
#include "ModAssignment.h"

// Number of global envelopes available.
namespace env {
    inline constexpr int NUM_ENVELOPES = 5;

    // Number of envelopes active in the current build.
    inline constexpr int NUM_ACTIVE_ENVELOPES =
#if OSCI_PREMIUM
        NUM_ENVELOPES;
#else
        1;
#endif
}
using env::NUM_ENVELOPES;
using env::NUM_ACTIVE_ENVELOPES;

// EnvAssignment is now a type alias for the generic ModAssignment.
// XML serialisation uses "env" as the index attribute name.
using EnvAssignment = ModAssignment;
