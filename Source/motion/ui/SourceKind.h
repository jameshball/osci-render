#pragma once

#include <JuceHeader.h>
#include "../model/Document.h"
#include "../model/Drawing.h"
#include "../../parser/FileFormatRegistry.h"

namespace motion::ui {
// What kind of source an asset is, as the library and Properties both name it.
inline juce::String sourceKind(const Asset& asset) {
    const auto extension = asset.extension.toLowerCase();
    if (asset.liveIdentity != nullptr) { return "Blender source"; }
    if (extension == ".blender-capture") { return "Capture"; }
    const auto* generator = generatorOf(asset);
    if (generator != nullptr) { return juce::String(generatorCategoryLabel(generator->category).data()); }
    if (asset.audio != nullptr) { return "Audio"; }
    if (extension == ".txt") { return "Text"; }
    if (extension == ".lua") { return "Lua"; }
    if (extension == ".lsystem") { return "Fractal"; }
    if (osci::files::isVideo(extension)) { return "Video"; }
    if (osci::files::isImage(extension)) { return "Image"; }
    if (extension == ".svg") { return drawing::isDrawing(asset.data.toString()) ? "Drawing" : "Vector"; }
    if (extension == ".json" || extension == ".lottie") { return "Lottie"; }
    if (extension == ".svgs") { return "SVG sequence"; }
    if (extension == ".obj") { return "3D object"; }
    // Anything else by its file type.
    return asset.extension.trimCharactersAtStart(".").toUpperCase();
}
}
