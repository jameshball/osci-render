#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <cstdint>

struct VisualiserRenderSize {
    int width = 1024;
    int height = 1024;

    bool operator==(const VisualiserRenderSize& other) const {
        return width == other.width && height == other.height;
    }

    bool operator!=(const VisualiserRenderSize& other) const {
        return !(*this == other);
    }
};

struct VisualiserGraticuleLayout {
    int xDivisions = 10;
    int yDivisions = 10;
    float cellSizePixels = 90.0f;
    float xOriginPixels = 62.0f;
    float yOriginPixels = 62.0f;
    float lineWidthPixels = 2.0f;
};

enum class VisualiserCanvasPreset : int {
    Square = 1,
    HDLandscape = 2,
    HDPortrait = 3,
    Custom = 4,
};

namespace VisualiserGeometry {
    constexpr int minCanvasDimension = 128;
    constexpr int maxCanvasDimension = 4096;

    inline int sanitiseCanvasDimension(int value) {
        int clamped = juce::jlimit(minCanvasDimension, maxCanvasDimension, value);
        return clamped - (clamped % 2);
    }

    inline VisualiserRenderSize sanitiseRenderSize(int width, int height) {
        return {sanitiseCanvasDimension(width), sanitiseCanvasDimension(height)};
    }

    inline VisualiserCanvasPreset sanitiseCanvasPreset(int value, VisualiserCanvasPreset fallback = VisualiserCanvasPreset::Square) {
        const auto preset = static_cast<VisualiserCanvasPreset>(value);
        switch (preset) {
            case VisualiserCanvasPreset::Square:
            case VisualiserCanvasPreset::HDLandscape:
            case VisualiserCanvasPreset::HDPortrait:
            case VisualiserCanvasPreset::Custom:
                return preset;
            default:
                return fallback;
        }
    }

    inline VisualiserRenderSize getRenderSizeForPreset(VisualiserCanvasPreset preset) {
        switch (preset) {
            case VisualiserCanvasPreset::Square:
                return {1024, 1024};
            case VisualiserCanvasPreset::HDLandscape:
                return {1920, 1080};
            case VisualiserCanvasPreset::HDPortrait:
                return {1080, 1920};
            case VisualiserCanvasPreset::Custom:
            default:
                return {1024, 1024};
        }
    }

    inline VisualiserCanvasPreset getPresetForRenderSize(VisualiserRenderSize size) {
        size = sanitiseRenderSize(size.width, size.height);
        if (size == getRenderSizeForPreset(VisualiserCanvasPreset::Square)) {
            return VisualiserCanvasPreset::Square;
        }
        if (size == getRenderSizeForPreset(VisualiserCanvasPreset::HDLandscape)) {
            return VisualiserCanvasPreset::HDLandscape;
        }
        if (size == getRenderSizeForPreset(VisualiserCanvasPreset::HDPortrait)) {
            return VisualiserCanvasPreset::HDPortrait;
        }
        return VisualiserCanvasPreset::Custom;
    }

    inline juce::String getPresetName(VisualiserCanvasPreset preset) {
        switch (preset) {
            case VisualiserCanvasPreset::Square:
                return "1024 x 1024";
            case VisualiserCanvasPreset::HDLandscape:
                return "1920 x 1080";
            case VisualiserCanvasPreset::HDPortrait:
                return "1080 x 1920";
            case VisualiserCanvasPreset::Custom:
            default:
                return "Custom";
        }
    }

    inline double getAspectRatio(VisualiserRenderSize size) {
        size = sanitiseRenderSize(size.width, size.height);
        return static_cast<double>(size.width) / static_cast<double>(size.height);
    }

    inline juce::Point<float> getWorldToClipScale(VisualiserRenderSize size) {
        const float aspect = static_cast<float>(getAspectRatio(size));
        if (aspect >= 1.0f) {
            return {1.0f / aspect, 1.0f};
        }
        return {1.0f, aspect};
    }

    inline int roundUpToEven(int value) {
        return value + (value % 2);
    }

    inline int fitEvenDivisionsToCanvas(int divisions, float cellSize, int dimension, int minimumDivisions) {
        divisions = roundUpToEven(juce::jmax(minimumDivisions, divisions));
        while (divisions > minimumDivisions && static_cast<float>(divisions) * cellSize > static_cast<float>(dimension)) {
            divisions -= 2;
        }
        return divisions;
    }

    inline VisualiserGraticuleLayout getGraticuleLayout(VisualiserRenderSize size) {
        size = sanitiseRenderSize(size.width, size.height);

        constexpr int baseDivisions = 10;
        constexpr float virtualTextureSize = 512.0f;
        constexpr float virtualCellSize = 45.0f;
        constexpr float virtualLineWidth = 4.0f;
        constexpr float originalTextureSize = 2048.0f;

        const float shortSide = static_cast<float>(juce::jmin(size.width, size.height));
        const float virtualScale = shortSide / virtualTextureSize;
        const float aspect = static_cast<float>(size.width) / static_cast<float>(size.height);

        const float cellSize = virtualCellSize * virtualScale;

        int xDivisions = baseDivisions;
        int yDivisions = baseDivisions;
        if (aspect >= 1.0f) {
            xDivisions = fitEvenDivisionsToCanvas(static_cast<int>(std::ceil(baseDivisions * aspect)), cellSize, size.width, baseDivisions);
        } else {
            yDivisions = fitEvenDivisionsToCanvas(static_cast<int>(std::ceil(baseDivisions / aspect)), cellSize, size.height, baseDivisions);
        }

        return {
            xDivisions,
            yDivisions,
            cellSize,
            (static_cast<float>(size.width) - static_cast<float>(xDivisions) * cellSize) * 0.5f,
            (static_cast<float>(size.height) - static_cast<float>(yDivisions) * cellSize) * 0.5f,
            juce::jmax(1.0f, virtualLineWidth * shortSide / originalTextureSize)
        };
    }

    inline float graticulePixelToClip(float pixel, int dimension) {
        return pixel / static_cast<float>(dimension) * 2.0f - 1.0f;
    }

    inline VisualiserRenderSize getAspectScaledRenderSize(VisualiserRenderSize size, int maxLongSide) {
        size = sanitiseRenderSize(size.width, size.height);
        const int longSide = juce::jmax(size.width, size.height);
        const double scale = static_cast<double>(maxLongSide) / static_cast<double>(longSide);
        auto sanitiseTextureDimension = [](int value) {
            const int clamped = juce::jmax(16, value);
            return clamped - (clamped % 2);
        };
        return {sanitiseTextureDimension(juce::roundToInt(static_cast<double>(size.width) * scale)),
                sanitiseTextureDimension(juce::roundToInt(static_cast<double>(size.height) * scale))};
    }

    inline juce::Rectangle<int> getAspectFitBounds(juce::Rectangle<int> area, VisualiserRenderSize size) {
        if (area.isEmpty()) {
            return area;
        }

        const double targetAspect = getAspectRatio(size);
        const double areaAspect = static_cast<double>(area.getWidth()) / static_cast<double>(area.getHeight());

        int fittedWidth = area.getWidth();
        int fittedHeight = area.getHeight();
        if (areaAspect > targetAspect) {
            fittedWidth = juce::roundToInt(static_cast<double>(fittedHeight) * targetAspect);
        } else {
            fittedHeight = juce::roundToInt(static_cast<double>(fittedWidth) / targetAspect);
        }

        return juce::Rectangle<int>(area.getX() + (area.getWidth() - fittedWidth) / 2,
                                    area.getY() + (area.getHeight() - fittedHeight) / 2,
                                    fittedWidth,
                                    fittedHeight);
    }

    inline std::uint64_t packRenderSize(VisualiserRenderSize size) {
        size = sanitiseRenderSize(size.width, size.height);
        return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(size.width)) << 32)
            | static_cast<std::uint32_t>(size.height);
    }

    inline VisualiserRenderSize unpackRenderSize(std::uint64_t packed) {
        if (packed == 0) {
            return {1024, 1024};
        }

        const int width = static_cast<int>((packed >> 32) & 0xffffffffu);
        const int height = static_cast<int>(packed & 0xffffffffu);
        return sanitiseRenderSize(width, height);
    }

    inline int getLegacyResolutionFromXml(const juce::XmlElement* resolutionXml) {
        if (resolutionXml == nullptr) {
            return 0;
        }

        for (const auto* parameterXml : resolutionXml->getChildIterator()) {
            if (parameterXml->getStringAttribute("id") == "resolution" && parameterXml->hasAttribute("value")) {
                return parameterXml->getIntAttribute("value");
            }
        }

        return 0;
    }

    inline VisualiserRenderSize getLegacyRenderSizeFromRecordingSettingsXml(const juce::XmlElement* settingsXml, VisualiserRenderSize fallback = {1024, 1024}) {
        if (settingsXml == nullptr) {
            return sanitiseRenderSize(fallback.width, fallback.height);
        }

        const int legacyResolution = getLegacyResolutionFromXml(settingsXml->getChildByName("resolution"));
        if (legacyResolution > 0) {
            return sanitiseRenderSize(legacyResolution, legacyResolution);
        }

        return sanitiseRenderSize(fallback.width, fallback.height);
    }
}
