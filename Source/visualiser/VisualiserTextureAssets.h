#pragma once

#include <JuceHeader.h>

#include <osci_gui/visualiser/osci_VisualiserRenderer.h>

inline juce::Image loadVisualiserTextureAsset(const void* data, int size) {
    return juce::ImageFileFormat::loadFrom(data, static_cast<size_t>(size));
}

inline VisualiserRendererAssets createVisualiserTextureAssets() {
    VisualiserRendererAssets assets;
    assets.noiseScreen = [] {
        return loadVisualiserTextureAsset(BinaryData::noise_jpg, BinaryData::noise_jpgSize);
    };
    assets.emptyScreen = [] {
        return loadVisualiserTextureAsset(BinaryData::empty_jpg, BinaryData::empty_jpgSize);
    };
    assets.realScreen = [] {
        return loadVisualiserTextureAsset(BinaryData::real_png, BinaryData::real_pngSize);
    };
    assets.vectorDisplayScreen = [] {
        return loadVisualiserTextureAsset(BinaryData::vector_display_png, BinaryData::vector_display_pngSize);
    };
    assets.emptyReflection = [] {
        return loadVisualiserTextureAsset(BinaryData::no_reflection_jpg, BinaryData::no_reflection_jpgSize);
    };
    assets.realReflection = [] {
        return loadVisualiserTextureAsset(BinaryData::real_reflection_png, BinaryData::real_reflection_pngSize);
    };
    assets.vectorDisplayReflection = [] {
        return loadVisualiserTextureAsset(BinaryData::vector_display_reflection_png, BinaryData::vector_display_reflection_pngSize);
    };
    return assets;
}
