#pragma once

#include <JuceHeader.h>
#include "MenuIcons.h"
#include "SourcePreview.h"
#include "../import/SourceDecoding.h"

namespace motion::ui {
// The examples that ship with osci-motion, by kind, as the Add source menu
// lists them.
struct ExampleKind {
    const char* title;
    const char* extension;
};
inline constexpr std::array<ExampleKind, 5> exampleKinds {{{"3D models", ".obj"}, {"Lua scripts", ".lua"}, {"Lottie animations", ".lottie"}, {"Fractals", ".lsystem"}, {"Text", ".txt"}}};

namespace examples {
// An example's file name as it shipped: "Spinning_cube.lua".
inline juce::String fileName(const juce::String& resource) { return BinaryData::getNamedResourceOriginalFilename(resource.toRawUTF8()); }
// As menus and the start screen name it: "Spinning cube".
inline juce::String displayName(const juce::String& resource) { return fileName(resource).upToLastOccurrenceOf(".", false, false).replaceCharacter('_', ' '); }
// The resources whose file name ends with this extension, in resource order.
inline std::vector<juce::String> resources(const char* extension) {
    std::vector<juce::String> found;
    for (int index = 0; index < BinaryData::namedResourceListSize; ++index) {
        if (fileName(BinaryData::namedResourceList[index]).endsWithIgnoreCase(extension)) { found.push_back(BinaryData::namedResourceList[index]); }
    }
    return found;
}
}

// Each example's picture, made once the editor opens, on a thread of its own
// at low priority: decoded as it would be imported (a script run for just
// long enough to draw) and its middle frame traced as the library traces a
// source, a model seen turned to show its sides. Pictures not yet made, or
// that could not be, are simply missing.
class ExamplePictures final : private juce::Thread {
public:
    ExamplePictures() : juce::Thread("Example pictures") { startThread(juce::Thread::Priority::low); }
    ~ExamplePictures() override {
        cancel.store(true);
        stopThread(4000);
    }

    // Drawn white on clear, as menu_icons::pictureMask draws them.
    std::optional<juce::Image> picture(const juce::String& resource) const {
        const juce::SpinLock::ScopedLockType lock(guard);
        const auto found = pictures.find(resource);
        return found != pictures.end() ? std::optional<juce::Image>(found->second) : std::nullopt;
    }

    // A model seen from a little above and to the side, as a generated solid
    // is; y stays up.
    static juce::Point<float> turned(const osci::Point& point) {
        constexpr double turn = -0.6, tilt = 0.45;
        const auto x = point.x * std::cos(turn) + point.z * std::sin(turn);
        const auto z = -point.x * std::sin(turn) + point.z * std::cos(turn);
        return {static_cast<float>(x), static_cast<float>(point.y * std::cos(tilt) - z * std::sin(tilt))};
    }

private:
    void run() override {
        for (int index = 0; index < BinaryData::namedResourceListSize && !threadShouldExit(); ++index) {
            const juce::String resource = BinaryData::namedResourceList[index];
            const juce::String file = BinaryData::getNamedResourceOriginalFilename(resource.toRawUTF8());
            const auto kind = std::find_if(exampleKinds.begin(), exampleKinds.end(), [&](const ExampleKind& item) { return file.endsWithIgnoreCase(item.extension); });
            if (kind == exampleKinds.end()) { continue; }
            int size = 0;
            const auto* data = BinaryData::getNamedResource(resource.toRawUTF8(), size);
            if (data == nullptr || size <= 0) { continue; }
            Asset asset;
            asset.name = file;
            asset.extension = juce::String(kind->extension);
            asset.data.append(data, static_cast<std::size_t>(size));
            // A script runs for two frames a second apart: its picture is the
            // second, once it has started moving.
            asset.bakeSettings.duration = 2;
            asset.bakeSettings.frameRate = 1;
            if (decodeAsset(asset, &cancel).failed() || asset.source == nullptr || asset.source->frameCount() == 0) { continue; }
            const auto model = asset.extension == ".obj";
            auto image = menu_icons::pictureMask(traceSource(*asset.source, asset.source->frameCount() / 2, model ? 6000 : 3000, model ? &turned : nullptr), pixels);
            const juce::SpinLock::ScopedLockType lock(guard);
            pictures[resource] = std::move(image);
        }
    }

    // Twice the width a menu draws an icon at on a high density display.
    static constexpr int pixels = 64;
    std::atomic<bool> cancel {false};
    juce::SpinLock guard;
    std::map<juce::String, juce::Image> pictures;
};
}
