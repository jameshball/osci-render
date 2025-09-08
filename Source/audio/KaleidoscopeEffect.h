#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"

class KaleidoscopeEffect : public osci::EffectApplication {
public:
    KaleidoscopeEffect(OscirenderAudioProcessor& p) : audioProcessor(p) {}

    osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override {
        const double twoPi = juce::MathConstants<double>::twoPi;
        double copies = juce::jmax(1.0, values[0].load());
        bool mirror = (bool)(values[1].load() > 0.5);
        double spread = juce::jlimit(0.0, 1.0, values[2].load());
        double clip = juce::jlimit(0.0, 1.0, values[3].load());
        double angleOffset = values[4].load() * juce::MathConstants<double>::twoPi;

        // Ensure values extremely close to integer don't get rounded up
        double fractionalPart = copies - std::floor(copies);
        double ceilCopies = fractionalPart > 1e-4 ? std::ceil(copies) : std::floor(copies);

        double currentRegion = std::floor(framePhase * copies);
        double polarity = (int)currentRegion % 2 == 0 ? 1 : -1;

        // TODO try splitting midway
        double theta = currentRegion / copies * twoPi + angleOffset;
        double rotTheta = theta;
        osci::Point output((1 - spread) * input.x + spread, (1 - spread) * input.y, input.z);
        output = osci::Point(output.y, -output.x, output.z);
        if (mirror && (int)currentRegion % 2 == 1) {
            output.y = -output.y;
        }

        double regionSize = twoPi / copies;
        osci::Point upperPlaneNormal(std::sin(0.5 * regionSize), -std::cos(0.5 * regionSize), 0);
        osci::Point lowerPlaneNormal(upperPlaneNormal.x, -upperPlaneNormal.y, 0);
        osci::Point clippedOutput = clipToPlane(output, upperPlaneNormal);

        clippedOutput = clipToPlane(output, lowerPlaneNormal);
        if (clippedOutput.x < 0) {
            clippedOutput.x = 0;
            clippedOutput.y = 0;
        }

        output = (1 - clip) * output + clip * clippedOutput;

        //osci::Point offset(std::cos(theta), std::sin(theta), 0.0);

        output.rotate(0, 0, rotTheta);

        framePhase += audioProcessor.frequency / ceilCopies / sampleRate;
        framePhase = framePhase - std::floor(framePhase);

        return output;
    }

    std::shared_ptr<osci::Effect> build() const override {
        auto eff = std::make_shared<osci::Effect>(
            std::make_shared<KaleidoscopeEffect>(audioProcessor),
            std::vector<osci::EffectParameter*>{
                // TODO ID strings
                new osci::EffectParameter("Copies",
                                          "Controls the number of copies of the input shape to draw. Splitting the shape into multiple copies creates audible harmony.",
                                          "kaleidoscopeCopies", VERSION_HINT, 3.0, 1.0, 6.0),
                new osci::EffectParameter("Mirror",
                                          "Controls how much the input shape is scaled in the kaleidoscope image.",
                                          "kaleidoscopeMirror", VERSION_HINT, 0.0, 0.0, 1.0),
                new osci::EffectParameter("Spread",
                                          "Controls how much the input shape is scaled in the kaleidoscope image.",
                                          "kaleidoscopeSpread", VERSION_HINT, 0.5, 0.0, 1.0),
                //new osci::EffectParameter("Offset",
                //                          "Controls how much the input shape is scaled in the kaleidoscope image.",
                //                          "harmonicDuplicatorSpread", VERSION_HINT, 0.5, 0.0, 1.0),
                new osci::EffectParameter("Clip",
                                          "Controls whether each copy of the shape is clipped within its own region.",
                                          "kaleidoscopeClip", VERSION_HINT, 1.0, 0.0, 1.0),
                new osci::EffectParameter("Angle Offset",
                                          "Rotates the offsets between copies without rotating the input shape.",
                                          "kaleidoscopeAngle", VERSION_HINT, 0.0, 0.0, 1.0, 0.0001, osci::LfoType::Sawtooth, 0.1)
        }
        );
        eff->setName("Kaleidoscope");
        eff->setIcon(BinaryData::kaleidoscope_svg);
        return eff;
    }

private:
    OscirenderAudioProcessor &audioProcessor;
    double framePhase = 0.0; // [0, 1]

    // Clips points behind the plane to the plane
    // Leaves points in front of the plane untouched
    osci::Point clipToPlane(osci::Point input, osci::Point planeNormal)
    {
        double distToPlane = input.innerProduct(planeNormal);
        double clippedDist = std::max(0.0, distToPlane);
        double distAdjustment = clippedDist - distToPlane;
        return input + distAdjustment * planeNormal;
    }
};
