#pragma once

#include "Generators.h"
#include <algorithm>
#include <array>
#include <string_view>

namespace motion {
// Mirrors ready to add: a generator's surface reflecting wholly. Any surface
// reflects with its Mirror property (Surface).
struct MirrorPreset {
    std::string_view name, generator, description;
};
inline constexpr std::array<MirrorPreset, 2> mirrorPresets {{
    {"Flat mirror", "rectangle", "Add a flat mirror: a pane to stand things before"},
    {"Mirror ball", "sphere", "Add a mirror ball: the whole scene, round on its surface"}}};
static_assert(std::all_of(mirrorPresets.begin(), mirrorPresets.end(), [](const MirrorPreset& preset) { return findGenerator(preset.generator) != nullptr; }), "Every mirror preset is a generator's");
}
