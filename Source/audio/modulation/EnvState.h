#pragma once

#include <JuceHeader.h>
#include <vector>
#include "ModAssignment.h"

// Number of global envelopes available.
namespace env {
    inline constexpr int NUM_ENVELOPES = 5;
}
using env::NUM_ENVELOPES;

// EnvAssignment is now a type alias for the generic ModAssignment.
// XML serialisation uses "env" as the index attribute name.
using EnvAssignment = ModAssignment;
