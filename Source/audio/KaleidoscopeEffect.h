#pragma once
#include <JuceHeader.h>
#include "../PluginProcessor.h"

class KaleidoscopeEffect : public osci::EffectApplication {
public:
    KaleidoscopeEffect(OscirenderAudioProcessor& p) : audioProcessor(p) {}

    osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override {
        const double pi = juce::MathConstants<double>::pi;
        const double twoPi = juce::MathConstants<double>::twoPi;
        double segments = juce::jmax(1.0, values[0].load());
        double mirror = values[1].load();
        double spread = juce::jlimit(0.0, 1.0, values[2].load());
        double clip = juce::jlimit(0.0, 1.0, values[3].load());

        // To start, treat everything as if we are on the +X (theta = 0) segment
        // Rotate input shape 90 deg CW so the shape is always "upright" 
        // relative to the radius
        // Then apply spread
        input = osci::Point(input.y, -input.x, input.z);
        osci::Point output = (1 - spread) * input;
        output.x += spread;

        // Mirror the y of every other segment if enabled
        double currentSegment = std::floor(framePhase * segments);
        if ((int)currentSegment % 2 == 1) {
            output.y *= (1 - 2 * mirror);
        }

        // Clip the shape to remain within this radial segment
        double segmentSize = twoPi / segments; // Angular size
        osci::Point upperPlaneNormal(std::sin(0.5 * segmentSize), -std::cos(0.5 * segmentSize), 0);
        osci::Point lowerPlaneNormal(upperPlaneNormal.x, -upperPlaneNormal.y, 0);
        osci::Point clippedOutput;
        if (segmentSize < pi) {
            clippedOutput = clipToPlane(output, upperPlaneNormal);
            clippedOutput = clipToPlane(clippedOutput, lowerPlaneNormal);
            // Point could still lie left of the origin along the lower plane
            if (clippedOutput.x < 0) {
                clippedOutput.x = 0;
                clippedOutput.y = 0;
            }
        } else {
            // segmentSize is wider than 180 degrees
            // If the point is clipped to both planes like above, the actual result region
            // will be less than 180 degrees
            // So only clip to one plane at a time
            if (output.y > 0) {
                clippedOutput = clipToPlane(output, upperPlaneNormal);
            } else {
                clippedOutput = clipToPlane(output, lowerPlaneNormal);
            }
        }
        output = (1 - clip) * output + clip * clippedOutput;

        // Finally, rotate this radial segment to its actual location
        double rotTheta = (currentSegment / segments) * twoPi;
        output.rotate(0, 0, rotTheta);

        double freqDivisor = std::ceil(segments - 1e-3);
        framePhase += audioProcessor.frequency / freqDivisor / sampleRate;
        framePhase = framePhase - std::floor(framePhase);

        return output;
    }

    std::shared_ptr<osci::Effect> build() const override {
        auto eff = std::make_shared<osci::SimpleEffect>(
            std::make_shared<KaleidoscopeEffect>(audioProcessor),
            std::vector<osci::EffectParameter*>{
                new osci::EffectParameter("Kaeidoscope Segments",
                                          "Controls how many times the input shape is rotationally duplicated around the centre.",
                                          "kaleidoscopeSegments", VERSION_HINT, 6.0, 1.0, 6.0, 0.0001, osci::LfoType::Sine, 0.2), 
                new osci::EffectParameter("Mirror",
                                          "Mirrors every other segment like a real kaleidoscope. Best used in combination with an even number of segments.",
                                          "kaleidoscopeMirror", VERSION_HINT, 1.0, 0.0, 1.0),
                new osci::EffectParameter("Spread",
                                          "Controls the radial spread of each segment.",
                                          "kaleidoscopeSpread", VERSION_HINT, 0.4, 0.0, 1.0),
                new osci::EffectParameter("Clip",
                                          "Clips each copy of the input shape within its own segment.",
                                          "kaleidoscopeClip", VERSION_HINT, 1.0, 0.0, 1.0)
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
