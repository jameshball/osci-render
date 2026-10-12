#pragma once

#include <array>
#include <string_view>

namespace motion {
// Mirrors ready to add: a generator's surface reflecting wholly. Any surface
// reflects with its Mirror property (Surface).
struct MirrorPreset {
    std::string_view name, generator;
};
inline constexpr std::array<MirrorPreset, 2> mirrorPresets {{{"Flat mirror", "rectangle"}, {"Mirror ball", "sphere"}}};
}
