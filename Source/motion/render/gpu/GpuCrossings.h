#pragma once

#include "GpuDevice.h"
#include "../HiddenLineCrossings.h"
#include <array>
#include <optional>
#include <vector>

namespace motion::gpu {
// crossing::detail::eachPair's pairs on the GPU, but only those that may
// cross: each pair is tested as crossing::between tests it, its corners
// held as whole numbers of a unit (so the test keeps the precision of the
// triangles' size), and only where a distance falls too near between's
// tolerance to be sure of is it kept regardless. They (a few more than
// cross) come back in the order crossing::detail::eachPair visits them: by
// the cell their boxes' overlap starts in, then the two triangles, for
// crossing::between to test exactly. None when the device fails.
std::optional<std::vector<std::array<std::uint32_t, 2>>> mayCross(Device& device, const std::vector<hidden::crossing::Triangle>& triangles, const hidden::crossing::detail::Pairing& pairing);

// crossing::runs with its pairs found on the GPU: just the runs it makes
// (each pair that may cross tested exactly, in the order it tests them).
// None when the device fails.
std::optional<std::vector<std::vector<hidden::crossing::Piece>>> crossingRuns(Device& device, const std::vector<hidden::crossing::Triangle>& triangles, double focalLength, hidden::Reach reach);
}
