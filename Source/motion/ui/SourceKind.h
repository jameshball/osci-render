#pragma once

#include <JuceHeader.h>
#include "../model/Document.h"
#include "../model/Drawing.h"
#include "../model/SourceParts.h"
#include "../../parser/FileFormatRegistry.h"
#include "../live/TextureSource.h"

namespace motion::ui {
// An SVG drawn in osci-motion's drawing editor, which edits it again.
inline bool isDrawing(const Asset& asset) { return asset.extension.equalsIgnoreCase(".svg") && drawing::isDrawing(asset.data.toString()); }
// A 3D object as imported, not parts split from one (which keep their place).
inline bool isModel(const Asset& asset) { return asset.extension.equalsIgnoreCase(".obj") && !parts::isLineSet(asset.data.toString()); }
// What kind of source an asset is, as the library and Properties both name it.
inline juce::String sourceKind(const Asset& asset) {
    const auto extension = asset.extension.toLowerCase();
    if (texture::isSource(extension)) { return texture::technology(); }
    if (asset.liveIdentity != nullptr) { return "Blender source"; }
    if (extension == ".blender-capture" || extension == texture::captureExtension) { return "Capture"; }
    const auto* generator = generatorOf(asset);
    if (generator != nullptr) { return juce::String(generatorCategoryLabel(generator->category).data()); }
    if (asset.audio != nullptr) { return "Audio"; }
    if (extension == ".txt") { return "Text"; }
    if (extension == ".lua") { return "Lua"; }
    if (extension == ".lsystem") { return "Fractal"; }
    if (osci::files::isVideo(extension)) { return "Video"; }
    if (osci::files::isImage(extension)) { return "Image"; }
    if (extension == ".svg") { return isDrawing(asset) ? "Drawing" : "Vector"; }
    if (extension == ".json" || extension == ".lottie") { return "Lottie"; }
    if (extension == ".svgs") { return "SVG sequence"; }
    if (extension == ".obj") { return "3D object"; }
    // Anything else by its file type.
    return asset.extension.trimCharactersAtStart(".").toUpperCase();
}
}
