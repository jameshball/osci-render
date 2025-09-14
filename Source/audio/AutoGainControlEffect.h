#pragma once
#include <JuceHeader.h>

class AutoGainControlEffect : public osci::EffectApplication {
public:
    AutoGainControlEffect() {}

    osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<float>>& values, float sampleRate) override {
        // Extract parameters from values
        double intensity = values[0]; // How aggressively the gain is adjusted (0.0 - 1.0)
        double targetLevel = values[1]; // The target RMS level to achieve (0.0 - 1.0)
        double response = values[2]; // How quickly the gain adjusts (0.0 - 1.0)
        
        intensity *= 0.5; // Scale intensity to 0.0 - 0.5 for more subtle control

        // Base response speed adjusted by sample rate (slower at higher sample rates)
        // This ensures consistent behavior across different sample rates
        double baseResponse = 0.001 + response * 0.049; // Scale to 0.001 - 0.05 (slower response)
        double sampleRateAdjustedResponse = baseResponse * (44100.0 / sampleRate);
        
        // Calculate the current signal magnitude (RMS-like)
        double currentLevel = std::sqrt(input.x * input.x + input.y * input.y) / std::sqrt(2.0);
        
        // Always update the smoothed level, even for very quiet signals
        smoothedLevel = smoothedLevel + sampleRateAdjustedResponse * (currentLevel - smoothedLevel);
        
        // Ensure smoothedLevel doesn't get too small
        smoothedLevel = juce::jmax(smoothedLevel, 0.0001);
        
        // Calculate the gain adjustment
        double gainAdjust = targetLevel / smoothedLevel;
        
        // Apply intensity parameter to control how much gain adjustment is applied
        gainAdjust = 1.0 + intensity * (gainAdjust - 1.0);
        
        // Limit the gain adjustment to allow more amplification for quiet sounds
        gainAdjust = juce::jlimit(0.1, 40.0, gainAdjust);
        
        // Apply the gain adjustment
        return input * gainAdjust;
    }
    
    std::shared_ptr<osci::Effect> build() const override {
        auto eff = std::make_shared<osci::SimpleEffect>(
            std::make_shared<AutoGainControlEffect>(),
            std::vector<osci::EffectParameter*>{
                new osci::EffectParameter("Intensity", "Controls how aggressively the gain adjustment is applied", "agcIntensity", VERSION_HINT, 1.0, 0.0, 1.0),
                new osci::EffectParameter("Target Level", "Target output level for the automatic gain control", "agcTarget", VERSION_HINT, 0.6, 0.0, 1.0),
                new osci::EffectParameter("Response", "How quickly the effect responds to level changes (lower is slower)", "agcResponse", VERSION_HINT, 0.0001, 0.0, 1.0)
            }
        );
        return eff;
    }
    
private:
    const double EPSILON = 0.00001;
    double smoothedLevel = 0.01; // Start with a small non-zero value to avoid division by zero
};
