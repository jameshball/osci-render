#pragma once

#include "Document.h"
#include <array>
#include <functional>
#include <string>
#include <string_view>

namespace motion {
// Ways a light can move on its own, applied in one go: a light's colour, and
// modulators routed to its intensity, colour and where it is. They are plain
// modulators and routes, named for the light, so they can be changed, keyed
// against or removed like any other.
enum class LightPreset { fire, candle, flickeringBulb, pulse, strobe };

namespace light_preset_detail {
struct Drive {
    std::string_view role;
    ModulationWaveform waveform;
    double rateHz;
    std::string_view property;
    double amount;
    ModulationMode mode;
};
struct Definition {
    LightPreset preset;
    std::string_view name, description;
    std::array<double, 3> colour;
    std::array<Drive, 6> drives;
    std::size_t driveCount;
};
using enum ModulationWaveform;
using enum ModulationMode;
// Fire surges slowly, flickers fast and crackles, yellower as it flares, and
// sways where it is so its shadows lick; a candle does the same, gently.
inline constexpr std::array<Definition, 5> definitions {{
    {LightPreset::fire, "Fire", "Surges, flickers and crackles, and sways so its shadows lick", {1, .55, .18}, {{
        {"Surge", noiseSmooth, 1.7, "light.intensity", .42, multiply},
        {"Flicker", noiseSmooth, 7.3, "light.intensity", .3, multiply},
        {"Crackle", noiseHold, 19, "light.intensity", .1, multiply},
        {"Flare", noiseSmooth, 2.9, "green", .09, add},
        {"Sway", noiseSmooth, 2.3, "position.x", .03, add},
        {"Lick", noiseSmooth, 3.1, "position.y", .04, add},
    }}, 6},
    {LightPreset::candle, "Candle", "A gentle flicker and a little sway", {1, .7, .35}, {{
        {"Flicker", noiseSmooth, .9, "light.intensity", .08, multiply},
        {"Waver", noiseSmooth, 5.5, "light.intensity", .06, multiply},
        {"Sway", noiseSmooth, 1.3, "position.x", .01, add},
    }}, 3},
    {LightPreset::flickeringBulb, "Flickering bulb", "Cuts out and stutters at random", {.9, .95, 1}, {{
        {"Stutter", noiseHold, 9, "light.intensity", .7, multiply},
        {"Dip", noiseHold, 1.1, "light.intensity", .35, multiply},
    }}, 2},
    {LightPreset::pulse, "Pulse", "Breathes slowly up and down", {1, 1, 1}, {{
        {"Pulse", sine, .5, "light.intensity", .6, multiply},
    }}, 1},
    {LightPreset::strobe, "Strobe", "Flashes on and off six times a second", {1, 1, 1}, {{
        {"Strobe", square, 6, "light.intensity", 1, multiply},
    }}, 1},
}};
}

inline const light_preset_detail::Definition& lightPresetDefinition(LightPreset preset) {
    for (const auto& definition : light_preset_detail::definitions) {
        if (definition.preset == preset) { return definition; }
    }
    return light_preset_detail::definitions.front();
}
inline std::string_view lightPresetName(LightPreset preset) { return lightPresetDefinition(preset).name; }
inline constexpr std::array<LightPreset, 5> lightPresets {LightPreset::fire, LightPreset::candle, LightPreset::flickeringBulb, LightPreset::pulse, LightPreset::strobe};

// The prefix of the modulators a preset makes for a light.
inline std::string lightPresetPrefix(const Clip& light) { return light.name + " \xc2\xb7 "; }

// The preset a light has, if any: by its modulators' names.
inline std::optional<LightPreset> currentLightPreset(const Composition& composition, Id light) {
    const auto* clip = findClip(composition, light);
    if (clip == nullptr) { return std::nullopt; }
    const auto prefix = lightPresetPrefix(*clip);
    for (const auto preset : lightPresets) {
        const auto named = prefix + std::string(lightPresetName(preset)) + " ";
        const auto has = std::any_of(composition.modulators.begin(), composition.modulators.end(), [&](const Modulator& modulator) { return modulator.name.starts_with(named); });
        if (has) { return preset; }
    }
    return std::nullopt;
}

// Takes off whatever preset a light has: its modulators' routes to it, and
// the modulators once nothing else uses them. False when it had none.
inline bool removeLightPreset(Composition& composition, Id light) {
    const auto* clip = findClip(composition, light);
    if (clip == nullptr) { return false; }
    const auto prefix = lightPresetPrefix(*clip);
    std::vector<Id> owned;
    for (const auto& modulator : composition.modulators) {
        if (modulator.name.starts_with(prefix)) { owned.push_back(modulator.id); }
    }
    const auto ownedBy = [&](Id modulator) { return std::find(owned.begin(), owned.end(), modulator) != owned.end(); };
    const auto before = composition.routes.size();
    std::erase_if(composition.routes, [&](const ModulationRoute& route) { return route.target == light && ownedBy(route.modulator); });
    const auto removed = before != composition.routes.size();
    std::erase_if(composition.modulators, [&](const Modulator& modulator) {
        return ownedBy(modulator.id) && std::none_of(composition.routes.begin(), composition.routes.end(), [&](const ModulationRoute& route) { return route.modulator == modulator.id; });
    });
    return removed;
}

// Puts a preset on a light, in place of any it had: its colour, and a
// modulator and route for each way it moves. `newId` makes ids.
inline bool applyLightPreset(Composition& composition, Id light, LightPreset preset, const std::function<Id()>& newId) {
    if (findClip(composition, light) == nullptr) { return false; }
    removeLightPreset(composition, light);
    auto* clip = changeClip(composition, light);
    const auto& definition = lightPresetDefinition(preset);
    clip->properties["red"] = Curve(definition.colour[0]);
    clip->properties["green"] = Curve(definition.colour[1]);
    clip->properties["blue"] = Curve(definition.colour[2]);
    const auto prefix = lightPresetPrefix(*clip);
    for (std::size_t index = 0; index < definition.driveCount; ++index) {
        const auto& drive = definition.drives[index];
        Modulator modulator;
        modulator.id = newId();
        modulator.name = prefix + std::string(definition.name) + " " + std::string(drive.role);
        modulator.shape.enabled = true;
        modulator.shape.waveform = drive.waveform;
        modulator.shape.rateHz = drive.rateHz;
        // Each its own noise, and each light's its own.
        modulator.shape.seed = static_cast<std::uint32_t>(light * 2654435761u + index * 40503u);
        composition.modulators.push_back(modulator);
        ModulationRoute route;
        route.id = newId();
        route.modulator = modulator.id;
        route.target = light;
        route.property = std::string(drive.property);
        route.amount = drive.amount;
        route.mode = drive.mode;
        composition.routes.push_back(route);
    }
    return true;
}

// Puts a preset on a light (none takes its preset off), as one undoable edit.
inline void editLightPreset(Document& document, Id light, std::optional<LightPreset> preset) {
    // Ids made first: the edit's copy of the project makes none of its own.
    std::vector<Id> ids;
    for (int index = 0; index < 16; ++index) { ids.push_back(document.newId()); }
    const auto name = preset.has_value() ? "Apply " + juce::String(lightPresetName(*preset).data()).toLowerCase() + " preset" : juce::String("Remove light preset");
    document.edit(name, [light, preset, ids](Project& project) {
        std::size_t used = 0;
        if (preset.has_value()) {
            applyLightPreset(project, light, *preset, [&] { return ids[used++]; });
        } else {
            removeLightPreset(project, light);
        }
    });
}
}
