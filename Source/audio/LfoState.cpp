#include "LfoState.h"

LfoWaveform createLfoPreset(LfoPreset preset) {
    LfoWaveform waveform;
    switch (preset) {
        case LfoPreset::Sine: {
            // Quadratic bezier sine approximation.
            // For B(0.5) to match sin(pi/4) at the quarter-wave midpoints,
            // the curve offset is kBezierCurveScale*(sqrt(2)-1) ≈ 20.71.
            const float kSineCurve = osci_audio::kBezierCurveScale * (std::sqrt(2.0f) - 1.0f);
            waveform.useBezier = true;
            waveform.nodes = {
                { 0.0,  0.5,  0.0f },
                { 0.25, 1.0,  kSineCurve },
                { 0.5,  0.5,  kSineCurve },
                { 0.75, 0.0, -kSineCurve },
                { 1.0,  0.5, -kSineCurve },
            };
            break;
        }
        case LfoPreset::Triangle:
            waveform.useBezier = false;
            waveform.nodes = {
                { 0.0,  0.0, 0.0f },
                { 0.5,  1.0, 0.0f },
                { 1.0,  0.0, 0.0f },
            };
            break;
        case LfoPreset::Sawtooth:
            waveform.useBezier = false;
            waveform.nodes = {
                { 0.0,  0.0, 0.0f },
                { 1.0,  1.0, 0.0f },
            };
            break;
        case LfoPreset::ReverseSawtooth:
            waveform.useBezier = false;
            waveform.nodes = {
                { 0.0,  1.0, 0.0f },
                { 1.0,  0.0, 0.0f },
            };
            break;
        case LfoPreset::Square:
            waveform.useBezier = false;
            waveform.nodes = {
                { 0.0,    1.0, 0.0f },
                { 0.499,  1.0, 0.0f },
                { 0.5,    0.0, 0.0f },
                { 0.999,  0.0, 0.0f },
                { 1.0,    1.0, 0.0f },
            };
            break;
        case LfoPreset::Exponential:
            waveform.useBezier = true;
            waveform.nodes = {
                { 0.0, 0.0, 0.0f },
                { 1.0, 1.0, -45.0f },
            };
            break;
        case LfoPreset::Logarithmic:
            waveform.useBezier = true;
            waveform.nodes = {
                { 0.0, 0.0, 0.0f },
                { 1.0, 1.0, 45.0f },
            };
            break;
        case LfoPreset::Pulse: {
            constexpr double pw = 0.15;
            waveform.useBezier = false;
            waveform.nodes = {
                { 0.0,        1.0, 0.0f },
                { pw - 0.001, 1.0, 0.0f },
                { pw,         0.0, 0.0f },
                { 0.999,      0.0, 0.0f },
                { 1.0,        1.0, 0.0f },
            };
            break;
        }
        case LfoPreset::Staircase:
            waveform.useBezier = false;
            waveform.nodes = {
                { 0.0,     0.0,       0.0f },
                { 0.1249,  0.0,       0.0f },
                { 0.125,   0.142857,  0.0f },
                { 0.2499,  0.142857,  0.0f },
                { 0.25,    0.285714,  0.0f },
                { 0.3749,  0.285714,  0.0f },
                { 0.375,   0.428571,  0.0f },
                { 0.4999,  0.428571,  0.0f },
                { 0.5,     0.571429,  0.0f },
                { 0.6249,  0.571429,  0.0f },
                { 0.625,   0.714286,  0.0f },
                { 0.7499,  0.714286,  0.0f },
                { 0.75,    0.857143,  0.0f },
                { 0.8749,  0.857143,  0.0f },
                { 0.875,   1.0,       0.0f },
                { 1.0,     1.0,       0.0f },
            };
            break;
        case LfoPreset::SmoothRandom: {
            waveform.useBezier = true;
            waveform.nodes = {
                { 0.0,   0.5,  0.0f },
                { 0.12,  0.85, 15.0f },
                { 0.25,  0.2, -20.0f },
                { 0.38,  0.95, 25.0f },
                { 0.5,   0.1, -15.0f },
                { 0.62,  0.7,  30.0f },
                { 0.75,  0.35,-25.0f },
                { 0.88,  0.8,  10.0f },
                { 1.0,   0.5, -20.0f },
            };
            break;
        }
        case LfoPreset::Bounce: {
            const float k = 400.0f * (std::sqrt(2.0f) - 1.0f);
            waveform.useBezier = true;
            waveform.nodes = {
                { 0.0,   0.0, 0.0f },
                { 0.5,   0.0, k },
                { 0.75,  0.0, k * 0.6f },
                { 0.9,   0.0, k * 0.3f },
                { 1.0,   0.0, k * 0.15f },
            };
            break;
        }
        case LfoPreset::Elastic: {
            waveform.useBezier = false;
            waveform.nodes = {
                { 0.0,   0.0,  0.0f },
                { 0.12,  1.0,  0.0f },
                { 0.24,  0.65, 0.0f },
                { 0.36,  1.0,  0.0f },
                { 0.48,  0.78, 0.0f },
                { 0.58,  1.0,  0.0f },
                { 0.68,  0.88, 0.0f },
                { 0.78,  1.0,  0.0f },
                { 0.88,  0.94, 0.0f },
                { 1.0,   1.0,  0.0f },
            };
            break;
        }
        case LfoPreset::WarmSaw: {
            const float k = osci_audio::kBezierCurveScale * (std::sqrt(2.0f) - 1.0f);
            waveform.useBezier = true;
            waveform.nodes = {
                { 0.0,    0.0,  0.0f },
                { 0.9,    1.0,  0.0f },
                { 0.999,  0.0, -k },
                { 1.0,    0.0,  0.0f },
            };
            break;
        }
        case LfoPreset::Shark: {
            waveform.useBezier = true;
            waveform.nodes = {
                { 0.0,  0.0,  0.0f },
                { 0.15, 1.0,  30.0f },
                { 1.0,  0.0, -35.0f },
            };
            break;
        }
        case LfoPreset::PulseTrain: {
            waveform.useBezier = true;
            waveform.nodes = {
                { 0.0,    0.0,   0.0f },
                { 0.02,   1.0,   20.0f },
                { 0.25,   0.0,  -30.0f },
                { 0.27,   1.0,   20.0f },
                { 0.5,    0.0,  -30.0f },
                { 0.52,   1.0,   20.0f },
                { 0.75,   0.0,  -30.0f },
                { 0.77,   1.0,   20.0f },
                { 1.0,    0.0,  -30.0f },
            };
            break;
        }
        case LfoPreset::Sidechain: {
            waveform.useBezier = true;
            waveform.nodes = {
                { 0.0,   1.0,   0.0f },
                { 0.03,  0.0,  -25.0f },
                { 1.0,   1.0,  -35.0f },
            };
            break;
        }
        case LfoPreset::TranceGate: {
            const float k = osci_audio::kBezierCurveScale * (std::sqrt(2.0f) - 1.0f);
            waveform.useBezier = true;
            waveform.nodes = {
                { 0.0,    0.0,   0.0f },
                { 0.04,   1.0,   k },
                { 0.2,    1.0,   0.0f },
                { 0.24,   0.0,  -k },
                { 0.3,    0.0,   0.0f },
                { 0.34,   0.75,  k * 0.7f },
                { 0.5,    0.75,  0.0f },
                { 0.54,   0.0,  -k * 0.7f },
                { 0.6,    0.0,   0.0f },
                { 0.64,   1.0,   k },
                { 0.95,   1.0,   0.0f },
                { 1.0,    0.0,  -k },
            };
            break;
        }
        case LfoPreset::Custom:
            waveform.useBezier = false;
            waveform.nodes = {
                { 0.0,  0.0, 0.0f },
                { 0.5,  1.0, 0.0f },
                { 1.0,  0.0, 0.0f },
            };
            break;
    }
    return waveform;
}
