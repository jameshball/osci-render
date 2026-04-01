#include "LfoState.h"

LfoWaveform createLfoPreset(LfoPreset preset) {
    LfoWaveform waveform;
    switch (preset) {
        case LfoPreset::Sine: {
            waveform.smooth = true;
            waveform.nodes = {
                { 0.0,  0.0,  0.0f },
                { 0.5,  1.0,  0.0f },
                { 1.0,  0.0,  0.0f },
            };
            break;
        }
        case LfoPreset::Triangle:
            waveform.smooth = false;
            waveform.nodes = {
                { 0.0,  0.0, 0.0f },
                { 0.5,  1.0, 0.0f },
                { 1.0,  0.0, 0.0f },
            };
            break;
        case LfoPreset::Sawtooth:
            waveform.smooth = false;
            waveform.nodes = {
                { 0.0,  0.0, 0.0f },
                { 1.0,  1.0, 0.0f },
            };
            break;
        case LfoPreset::ReverseSawtooth:
            waveform.smooth = false;
            waveform.nodes = {
                { 0.0,  1.0, 0.0f },
                { 1.0,  0.0, 0.0f },
            };
            break;
        case LfoPreset::Square:
            waveform.smooth = false;
            waveform.nodes = {
                { 0.0,    1.0, 0.0f },
                { 0.499,  1.0, 0.0f },
                { 0.5,    0.0, 0.0f },
                { 0.999,  0.0, 0.0f },
                { 1.0,    1.0, 0.0f },
            };
            break;
        case LfoPreset::Exponential:
            waveform.smooth = true;
            waveform.nodes = {
                { 0.0, 0.0, 0.0f },
                { 1.0, 1.0, 8.0f },
            };
            break;
        case LfoPreset::Logarithmic:
            waveform.smooth = true;
            waveform.nodes = {
                { 0.0, 0.0, 0.0f },
                { 1.0, 1.0, -8.0f },
            };
            break;
        case LfoPreset::Pulse: {
            constexpr double pw = 0.15;
            waveform.smooth = false;
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
            waveform.smooth = false;
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
            waveform.smooth = true;
            waveform.nodes = {
                { 0.0,   0.5,  0.0f },
                { 0.12,  0.85, 3.0f },
                { 0.25,  0.2, -4.0f },
                { 0.38,  0.95, 5.0f },
                { 0.5,   0.1, -3.0f },
                { 0.62,  0.7,  6.0f },
                { 0.75,  0.35,-5.0f },
                { 0.88,  0.8,  2.0f },
                { 1.0,   0.5, -4.0f },
            };
            break;
        }
        case LfoPreset::Bounce: {
            // Bounce uses explicit peak nodes since power curves can't create
            // humps between same-valued nodes.
            waveform.smooth = true;
            waveform.nodes = {
                { 0.0,   0.0,  0.0f },
                { 0.25,  1.0,  0.0f },
                { 0.5,   0.0,  0.0f },
                { 0.65,  0.5,  0.0f },
                { 0.8,   0.0,  0.0f },
                { 0.9,   0.2,  0.0f },
                { 1.0,   0.0,  0.0f },
            };
            break;
        }
        case LfoPreset::Elastic: {
            waveform.smooth = false;
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
            waveform.smooth = true;
            waveform.nodes = {
                { 0.0,    0.0,  0.0f },
                { 0.9,    1.0,  0.0f },
                { 0.999,  0.0, -5.0f },
                { 1.0,    0.0,  0.0f },
            };
            break;
        }
        case LfoPreset::Shark: {
            waveform.smooth = true;
            waveform.nodes = {
                { 0.0,  0.0,  0.0f },
                { 0.15, 1.0,  6.0f },
                { 1.0,  0.0, -7.0f },
            };
            break;
        }
        case LfoPreset::PulseTrain: {
            waveform.smooth = true;
            waveform.nodes = {
                { 0.0,    0.0,   0.0f },
                { 0.02,   1.0,   5.0f },
                { 0.25,   0.0,  -6.0f },
                { 0.27,   1.0,   5.0f },
                { 0.5,    0.0,  -6.0f },
                { 0.52,   1.0,   5.0f },
                { 0.75,   0.0,  -6.0f },
                { 0.77,   1.0,   5.0f },
                { 1.0,    0.0,  -6.0f },
            };
            break;
        }
        case LfoPreset::Sidechain: {
            waveform.smooth = true;
            waveform.nodes = {
                { 0.0,   1.0,   0.0f },
                { 0.03,  0.0,  -5.0f },
                { 1.0,   1.0,  -7.0f },
            };
            break;
        }
        case LfoPreset::TranceGate: {
            waveform.smooth = true;
            waveform.nodes = {
                { 0.0,    0.0,   0.0f },
                { 0.04,   1.0,   5.0f },
                { 0.2,    1.0,   0.0f },
                { 0.24,   0.0,  -5.0f },
                { 0.3,    0.0,   0.0f },
                { 0.34,   0.75,  3.5f },
                { 0.5,    0.75,  0.0f },
                { 0.54,   0.0,  -3.5f },
                { 0.6,    0.0,   0.0f },
                { 0.64,   1.0,   5.0f },
                { 0.95,   1.0,   0.0f },
                { 1.0,    0.0,  -5.0f },
            };
            break;
        }
        case LfoPreset::Custom:
            waveform.smooth = false;
            waveform.nodes = {
                { 0.0,  0.0, 0.0f },
                { 0.5,  1.0, 0.0f },
                { 1.0,  0.0, 0.0f },
            };
            break;
    }
    return waveform;
}
