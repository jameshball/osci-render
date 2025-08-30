// KaleidoscopeEffect.h
// Repeats and mirrors the input around the origin to create a kaleidoscope pattern.
// The effect supports a floating point number of segments, allowing smooth morphing
// between different symmetry counts.
#pragma once

#include <JuceHeader.h>

class KaleidoscopeEffect : public osci::EffectApplication {
public:
    osci::Point apply(int /*index*/, osci::Point input, const std::vector<std::atomic<double>>& values, double /*sampleRate*/) override {
        // values[0] = segments (can be fractional)
        // values[1] = phase (0-1) selecting which segment is currently being drawn
        double segments = juce::jmax(values[0].load(), 1.0); // ensure at least 1 segment
        double phase = values.size() > 1 ? values[1].load() : 0.0;

        // Polar conversion
        double r = std::sqrt(input.x * input.x + input.y * input.y);
        if (r < 1e-12) return input;
        double theta = std::atan2(input.y, input.x);

        int fullSegments = (int)std::floor(segments);
        double fractionalPart = segments - fullSegments; // in [0,1)

        // Use 'segments' for timing so partial segment gets proportionally shorter time.
        double currentSegmentFloat = phase * segments; // [0, segments)
        int currentSegmentIndex = (int)std::floor(currentSegmentFloat);
        int maxIndex = fractionalPart > 1e-9 ? fullSegments : fullSegments - 1; // include partial index if exists
        if (currentSegmentIndex > maxIndex) currentSegmentIndex = maxIndex; // safety

        // Base full wedge angle (all full wedges) and size of partial wedge
        double baseWedgeAngle = juce::MathConstants<double>::twoPi / segments; // size of a "unit" wedge
        double partialScale = (currentSegmentIndex == fullSegments && fractionalPart > 1e-9) ? fractionalPart : 1.0;
        double wedgeAngle = baseWedgeAngle * partialScale;

        // Normalize theta to [0,1) for compression
        double thetaNorm = (theta + juce::MathConstants<double>::pi) / juce::MathConstants<double>::twoPi; // 0..1

        // Offset for this segment: each preceding full segment occupies baseWedgeAngle
        double segmentOffset = 0.0;
        if (currentSegmentIndex < fullSegments) {
            segmentOffset = currentSegmentIndex * baseWedgeAngle;
        } else { // partial segment
            segmentOffset = fullSegments * baseWedgeAngle;
        }
        // Map entire original angle range into [segmentOffset, segmentOffset + wedgeAngle) so edges line up exactly.
        double finalTheta = segmentOffset + thetaNorm * wedgeAngle - juce::MathConstants<double>::pi; // constant 180° rotation

        double newX = r * std::cos(finalTheta);
        double newY = r * std::sin(finalTheta);
        return osci::Point(newX, newY, input.z);
    }

    std::shared_ptr<osci::Effect> build() const override {
        auto eff = std::make_shared<osci::Effect>(
            std::make_shared<KaleidoscopeEffect>(),
            std::vector<osci::EffectParameter*>{
                new osci::EffectParameter(
                    "Kaleidoscope Segments",
                    "Controls how many times the image is rotationally repeated around the centre. Fractional values smoothly morph the repetition.",
                    "kaleidoscopeSegments",
                    VERSION_HINT,
                    3.0,  // default
                    1.0,  // min
                    5.0,  // max
                    0.0001f, // step
                    osci::LfoType::Sine,
                    0.25f    // LFO frequency (Hz) – slow, visible rotation
                ),
                new osci::EffectParameter(
                    "Kaleidoscope Phase",
                    "Selects which kaleidoscope segment is currently being drawn (time-multiplexed). Animate to sweep around the circle.",
                    "kaleidoscopePhase",
                    VERSION_HINT,
                    0.0,   // default
                    0.0,   // min
                    1.0,   // max
                    0.0001f, // step
                    osci::LfoType::Sawtooth,
                    55.0f    // LFO frequency (Hz) – slow, visible rotation
                ),
            }
        );
        eff->setName("Kaleidoscope");
        eff->setIcon(BinaryData::kaleidoscope_svg);
        return eff;
    }
};
