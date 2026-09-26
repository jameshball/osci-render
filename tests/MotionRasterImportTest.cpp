#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"

class MotionRasterImportTest : public juce::UnitTest {
public:
    MotionRasterImportTest() : juce::UnitTest("Motion image sources", "MotionRaster") {}
    void runTest() override {
        beginTest("Transparent PNG prepares colored outlines without tinting pure blue away");
        juce::Image image(juce::Image::ARGB, 16, 8, true);
        juce::Graphics graphics(image);
        graphics.setColour(juce::Colours::blue);
        graphics.fillRect(2, 2, 10, 4);
        juce::MemoryOutputStream encoded;
        expect(juce::PNGImageFormat().writeImageToStream(image, encoded));
        auto png = std::make_shared<motion::Asset>();
        png->id = 1;
        png->name = "blue.png";
        png->extension = ".png";
        png->data = encoded.getMemoryBlock();
        png->rasterSettings.resolution = 16;
        png->rasterSettings.pointsPerFrame = 256;
        const auto decoded = motion::Document::decodeAsset(*png);
        expect(decoded.wasOk(), decoded.getErrorMessage());
        if (decoded.failed()) { return; }
        expectEquals(static_cast<int>(png->source->frameCount()), 1);
        const auto blue = maximumColour(*png->source, 0);
        expect(blue[2] > 0.99f && blue[0] == 0 && blue[1] == 0);
        const auto clip = motion::Document::makeClip(2, *png, 0);
        expectEquals(clip.properties.at("blue").evaluate(0), 1.0);
        expectEquals(clip.properties.at("red").evaluate(0), 1.0);

        beginTest("Downsampling ignores hidden RGB and preserves straight-alpha source colour");
        juce::MemoryOutputStream alphaData;
        expect(juce::Base64::convertFromBase64(alphaData, "iVBORw0KGgoAAAANSUhEUgAAAEAAAAAgCAYAAACinX6EAAAAWklEQVR4nOXTsQ3AQAzDQJr77+wgEyTdA88D1KlQo1naJE7iJE7iJE7iJM6vwrD7hktJnMRJ3Fx77p8kTuIkTuIkTuIkTuIkTuIkTuIkTuIkTuIkTuI8PeC0B7DTBjthW5zFAAAAAElFTkSuQmCC"));
        motion::Asset alpha;
        alpha.extension = ".png";
        alpha.data = alphaData.getMemoryBlock();
        alpha.rasterSettings.resolution = 16;
        alpha.rasterSettings.pointsPerFrame = 256;
        const auto alphaResult = motion::Document::decodeAsset(alpha);
        expect(alphaResult.wasOk(), alphaResult.getErrorMessage());
        if (alphaResult.wasOk()) {
            const auto colour = maximumColour(*alpha.source, 0);
            expectEquals(colour[0], 0.0f);
            expectEquals(colour[1], 0.0f);
            expectWithinAbsoluteError(colour[2], 127.0f / 255, 0.01f);
        }

        beginTest("Variable GIF frame delays and RGB survive independent seeking");
        juce::MemoryOutputStream gifData;
        expect(juce::Base64::convertFromBase64(gifData, "R0lGODlhCAAIAIEAAAAAAP8AAAAAAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQJCgAAACwAAAAACAAIAAAIGAABCBxIsODAAAgDEEyo8GDChQ8NSiQYEAAh+QQJHgAAACwCAAIABAAEAIEAAAAA/wAAAAAAAAAICQADCBxIsGCAgAAh+QQJCgAAACwCAAIABAAEAIEAAAAAAP8AAAAAAAAICQADCBxIsGCAgAA7"));
        auto gif = std::make_shared<motion::Asset>();
        gif->id = 3;
        gif->name = "rgb.gif";
        gif->extension = ".gif";
        gif->data = gifData.getMemoryBlock();
        gif->rasterSettings.pointsPerFrame = 256;
        const auto animated = motion::Document::decodeAsset(*gif);
        expect(animated.wasOk(), animated.getErrorMessage());
        if (animated.failed()) { return; }
        expectEquals(static_cast<int>(gif->source->frameCount()), 3);
        expectEquals(gif->source->duration(), 0.5);
        for (const auto [time, channel] : {std::pair {0.0, 0}, {0.1, 1}, {0.399, 1}, {0.4, 2}, {-0.05, 2}, {0.5, 0}, {0.15, 1}}) {
            const auto colour = maximumColour(*gif->source, time);
            for (int i = 0; i < 3; ++i) { expectWithinAbsoluteError(colour[i], i == channel ? 1.0f : 0.0f, 0.001f); }
        }
        expectEquals(motion::Document::makeClip(4, *gif, 0).duration, 0.5);

        beginTest("Image settings and source timing survive textual project round trips");
        juce::UndoManager undo;
        motion::Document document(undo);
        motion::Project project;
        project.assets = {png, gif};
        document.reset(project);
        const auto xml = juce::parseXML(document.save().toString());
        expect(xml != nullptr);
        if (xml == nullptr) { return; }
        juce::UndoManager restoredUndo;
        motion::Document restored(restoredUndo);
        const auto loaded = restored.load(*xml);
        expect(loaded.wasOk(), loaded.getErrorMessage());
        if (loaded.wasOk()) {
            expectEquals(restored.project().assets[0]->rasterSettings.resolution, 16);
            expectEquals(static_cast<int>(restored.project().assets[0]->rasterSettings.pointsPerFrame), 256);
            expectEquals(restored.project().assets[1]->source->duration(), 0.5);
            expect(maximumColour(*restored.project().assets[1]->source, 0.4)[2] > 0.99f);
        }

        beginTest("PNG and animated GIF survive the standalone binary project writer");
        juce::XmlElement binaryProject("motion-project");
        binaryProject.setAttribute("schema", 1);
        binaryProject.addChildElement(new juce::XmlElement(document.save()));
        juce::MemoryBlock binary;
        juce::AudioProcessor::copyXmlToBinary(binaryProject, binary);
        const auto decodedXml = juce::AudioProcessor::getXmlFromBinary(binary.getData(), static_cast<int>(binary.getSize()));
        expect(decodedXml != nullptr);
        if (decodedXml != nullptr) {
            const auto* composition = decodedXml->getChildByName("composition");
            expect(composition != nullptr);
            if (composition != nullptr) {
                const auto result = restored.load(*composition);
                expect(result.wasOk(), result.getErrorMessage());
                if (result.wasOk()) {
                    expect(restored.project().assets[0]->data == png->data);
                    expect(restored.project().assets[1]->data == gif->data);
                    expectEquals(restored.project().assets[1]->source->duration(), 0.5);
                    expect(maximumColour(*restored.project().assets[1]->source, 0.4)[2] > 0.99f);
                }
            }
        }

        beginTest("Invalid settings and cancellation retain the prior prepared source");
        const auto previous = png->source;
        png->rasterSettings.threshold = -1;
        expect(motion::Document::decodeAsset(*png).failed());
        expect(png->source == previous);
        png->rasterSettings.threshold = 0.02;
        std::atomic<bool> cancelled {true};
        expect(motion::Document::decodeAsset(*png, &cancelled).failed());
        expect(png->source == previous);
    }
private:
    static std::array<float, 3> maximumColour(const motion::PreparedSource& source, double time) {
        std::array<float, 3> result {};
        for (int sample = 0; sample < 256; ++sample) {
            const auto point = source.sample(time, static_cast<double>(sample) / 256);
            result[0] = std::max(result[0], point.r);
            result[1] = std::max(result[1], point.g);
            result[2] = std::max(result[2], point.b);
        }
        return result;
    }
};
static MotionRasterImportTest motionRasterImportTest;
