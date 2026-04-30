#include "VisualiserSvgExporter.h"

namespace {
constexpr int svgExportResolution = 1024;
constexpr float lineGain = 450.0f / 512.0f;

juce::String svgNumber(double value)
{
    return juce::String(value, 3);
}

float clampUnit(float value)
{
    return juce::jlimit(0.0f, 1.0f, value);
}

bool isFinite(float value)
{
    return std::isfinite(value);
}

float toSvgX(float value)
{
    return (value * lineGain * 0.5f + 0.5f) * (float)svgExportResolution;
}

float toSvgY(float value)
{
    return (0.5f - value * lineGain * 0.5f) * (float)svgExportResolution;
}

juce::Colour applyLineSaturation(juce::Colour colour, float saturation)
{
    const float red = colour.getFloatRed();
    const float green = colour.getFloatGreen();
    const float blue = colour.getFloatBlue();
    const float luma = 0.299f * red + 0.587f * green + 0.114f * blue;

    return juce::Colour::fromFloatRGBA(
        clampUnit(luma + (red - luma) * saturation),
        clampUnit(luma + (green - luma) * saturation),
        clampUnit(luma + (blue - luma) * saturation),
        colour.getFloatAlpha());
}

juce::String colourToSvg(juce::Colour colour)
{
    return "#" + colour.toDisplayString(false);
}

int getPointCount(const VisualiserSvgExporter::FrameData& frameData)
{
    if (frameData.xPoints == nullptr || frameData.yPoints == nullptr)
        return 0;

    return juce::jmin((int)frameData.xPoints->size(), (int)frameData.yPoints->size());
}

float getBrightness(const VisualiserSvgExporter::FrameData& frameData, int index)
{
    if (frameData.mode == VisualiserSvgExporter::RenderMode::XYZ
        && frameData.zPoints != nullptr
        && index < (int)frameData.zPoints->size())
        return clampUnit((*frameData.zPoints)[(size_t)index]);

    if (frameData.mode == VisualiserSvgExporter::RenderMode::XYRGB
        && frameData.rPoints != nullptr
        && frameData.gPoints != nullptr
        && frameData.bPoints != nullptr
        && index < (int)frameData.rPoints->size()
        && index < (int)frameData.gPoints->size()
        && index < (int)frameData.bPoints->size()
        && (*frameData.rPoints)[(size_t)index] >= 0.0f)
        return clampUnit(std::max((*frameData.rPoints)[(size_t)index],
                                  std::max((*frameData.gPoints)[(size_t)index], (*frameData.bPoints)[(size_t)index])));

    return 1.0f;
}

juce::Colour getColour(const VisualiserSvgExporter::FrameData& frameData,
                       int startIndex,
                       int endIndex,
                       juce::Colour fallbackColour,
                       float lineSaturation)
{
    if (frameData.mode == VisualiserSvgExporter::RenderMode::XYRGB
        && frameData.rPoints != nullptr
        && frameData.gPoints != nullptr
        && frameData.bPoints != nullptr
        && startIndex < (int)frameData.rPoints->size()
        && startIndex < (int)frameData.gPoints->size()
        && startIndex < (int)frameData.bPoints->size()
        && endIndex < (int)frameData.rPoints->size()
        && endIndex < (int)frameData.gPoints->size()
        && endIndex < (int)frameData.bPoints->size()
        && (*frameData.rPoints)[(size_t)startIndex] >= 0.0f
        && (*frameData.rPoints)[(size_t)endIndex] >= 0.0f)
    {
        const auto red = 0.5f * (clampUnit((*frameData.rPoints)[(size_t)startIndex]) + clampUnit((*frameData.rPoints)[(size_t)endIndex]));
        const auto green = 0.5f * (clampUnit((*frameData.gPoints)[(size_t)startIndex]) + clampUnit((*frameData.gPoints)[(size_t)endIndex]));
        const auto blue = 0.5f * (clampUnit((*frameData.bPoints)[(size_t)startIndex]) + clampUnit((*frameData.bPoints)[(size_t)endIndex]));
        return applyLineSaturation(juce::Colour::fromFloatRGBA(red, green, blue, 1.0f), lineSaturation);
    }

    return fallbackColour;
}

struct SegmentGeometry {
    bool valid = false;
    bool dot = false;
    float svgX1 = 0.0f;
    float svgY1 = 0.0f;
    float svgX2 = 0.0f;
    float svgY2 = 0.0f;
    float length = 0.0f;
    float audioLength = 0.0f;
};

SegmentGeometry getSegmentGeometry(const VisualiserSvgExporter::FrameData& frameData, int segmentIndex)
{
    const auto& xPoints = *frameData.xPoints;
    const auto& yPoints = *frameData.yPoints;
    const float x1 = xPoints[(size_t)segmentIndex];
    const float y1 = yPoints[(size_t)segmentIndex];
    const float x2 = xPoints[(size_t)segmentIndex + 1];
    const float y2 = yPoints[(size_t)segmentIndex + 1];

    if (!isFinite(x1) || !isFinite(y1) || !isFinite(x2) || !isFinite(y2))
        return {};

    SegmentGeometry geometry;
    geometry.valid = true;
    geometry.svgX1 = toSvgX(x1);
    geometry.svgY1 = toSvgY(y1);
    geometry.svgX2 = toSvgX(x2);
    geometry.svgY2 = toSvgY(y2);

    const auto svgDx = geometry.svgX2 - geometry.svgX1;
    const auto svgDy = geometry.svgY2 - geometry.svgY1;
    geometry.length = std::sqrt(svgDx * svgDx + svgDy * svgDy);
    geometry.dot = geometry.length < 0.001f;

    const float audioDx = (x2 - x1) * lineGain;
    const float audioDy = (y2 - y1) * lineGain;
    geometry.audioLength = std::sqrt(audioDx * audioDx + audioDy * audioDy);
    return geometry;
}
}

juce::String VisualiserSvgExporter::createSvg(const FrameData& frameData, VisualiserParameters& parameters)
{
    const int nPoints = getPointCount(frameData);
    if (nPoints < 2)
        return {};

    const int nEdges = nPoints - 1;
    const float focus = juce::jmax(0.0001f, (float)parameters.getFocus());
    const float strokeWidth = juce::jmax(0.35f, focus * (float)svgExportResolution * 0.25f);
    const float renderIntensityScale = frameData.usesUpsampledSamples ? 1.0f : (float)(frameData.resampleRatio * 1.5);
    const float sampleRateIntensityScale = 41000.0f / juce::jmax(1.0f, (float)frameData.sampleRate);
    const float globalOpacity = juce::jlimit(0.0f, 1.0f, (float)parameters.getIntensity() * 12.0f * renderIntensityScale * sampleRateIntensityScale);
    const float persistence = (float)(std::pow(0.5, parameters.getPersistence()) * 0.4 * 60.0 / juce::jmax(1.0, frameData.frameRate));
    const float frameFadeAmount = juce::jmin(1.0f, persistence);
    const float lineSaturation = (float)parameters.getLineSaturation();
    const auto fallbackColour = applyLineSaturation(
        juce::Colour::fromHSV((float)(parameters.getHue() / 360.0), 1.0f, 1.0f, 1.0f),
        lineSaturation);
    const float speedReference = focus * 3.0f;

    juce::String pathData;
    float totalPathLength = 0.0f;
    float lastSvgX = 0.0f;
    float lastSvgY = 0.0f;
    bool pathOpen = false;

    for (int segmentIndex = 0; segmentIndex < nEdges; ++segmentIndex) {
        const auto geometry = getSegmentGeometry(frameData, segmentIndex);
        if (!geometry.valid) {
            pathOpen = false;
            continue;
        }

        if (geometry.dot)
            continue;

        if (!pathOpen || std::abs(lastSvgX - geometry.svgX1) > 0.001f || std::abs(lastSvgY - geometry.svgY1) > 0.001f)
            pathData << "M " << svgNumber(geometry.svgX1) << " " << svgNumber(geometry.svgY1) << " ";

        pathData << "L " << svgNumber(geometry.svgX2) << " " << svgNumber(geometry.svgY2) << " ";
        pathOpen = true;
        lastSvgX = geometry.svgX2;
        lastSvgY = geometry.svgY2;
        totalPathLength += geometry.length;
    }

    juce::String elements;
    float startDistance = 0.0f;

    for (int segmentIndex = 0; segmentIndex < nEdges; ++segmentIndex) {
        const auto geometry = getSegmentGeometry(frameData, segmentIndex);
        if (!geometry.valid)
            continue;

        float intensityScale = nEdges > 0 ? (float)segmentIndex / (float)nEdges : 1.0f;
#if OSCI_PREMIUM
        if (parameters.getShutterSync())
            intensityScale = 0.25f;
#endif
        const float intensityFade = (1.0f - frameFadeAmount) + frameFadeAmount * intensityScale;
        const float brightness = 0.5f * (getBrightness(frameData, segmentIndex) + getBrightness(frameData, segmentIndex + 1));
        const float speedBrightness = juce::jmin(1.0f, speedReference / juce::jmax(geometry.audioLength, speedReference * 0.1f));
        const float opacity = juce::jlimit(0.0f, 1.0f, brightness * speedBrightness * intensityFade * globalOpacity);

        if (opacity > 0.001f) {
            const auto colour = colourToSvg(getColour(frameData, segmentIndex, segmentIndex + 1, fallbackColour, lineSaturation));

            if (geometry.dot) {
                elements << "    <circle cx=\"" << svgNumber(geometry.svgX1) << "\" cy=\"" << svgNumber(geometry.svgY1)
                    << "\" r=\"" << svgNumber(strokeWidth * 0.5f) << "\" fill=\"" << colour
                    << "\" fill-opacity=\"" << svgNumber(opacity) << "\"/>\n";
            } else {
                elements << "    <use href=\"#osci-render-trace\" stroke=\"" << colour
                    << "\" stroke-opacity=\"" << svgNumber(opacity)
                    << "\" stroke-dasharray=\"" << svgNumber(geometry.length) << " " << svgNumber(totalPathLength + 1.0f)
                    << "\" stroke-dashoffset=\"" << svgNumber(-startDistance) << "\"/>\n";
            }
        }

        if (!geometry.dot)
            startDistance += geometry.length;
    }

    if (elements.isEmpty())
        return {};

    juce::String svg;
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    svg << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"" << svgExportResolution << "\" height=\"" << svgExportResolution
        << "\" viewBox=\"0 0 " << svgExportResolution << " " << svgExportResolution << "\" overflow=\"hidden\">\n";

    if (pathData.isNotEmpty()) {
        svg << "  <defs>\n";
        svg << "    <path id=\"osci-render-trace\" pathLength=\"" << svgNumber(totalPathLength)
            << "\" d=\"" << pathData.trim() << "\"/>\n";
        svg << "  </defs>\n";
    }

    svg << "  <g fill=\"none\" stroke-linecap=\"butt\" stroke-linejoin=\"round\" stroke-width=\"" << svgNumber(strokeWidth) << "\">\n";
    svg << elements;
    svg << "  </g>\n";
    svg << "</svg>\n";
    return svg;
}
