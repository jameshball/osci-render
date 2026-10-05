#pragma once

#include <cstdint>

namespace motion {
// A document object's identity (clip, track, effect, modulator, asset...);
// 0 is none.
using Id = std::uint64_t;
}
