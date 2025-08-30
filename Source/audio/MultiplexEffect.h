#pragma once
#include <JuceHeader.h>
#include <cmath>
#include <numbers>
#include "../PluginProcessor.h"

class MultiplexEffect : public osci::EffectApplication {
public:
    explicit MultiplexEffect(OscirenderAudioProcessor &p) : audioProcessor(p) {}

    osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override {
        jassert(values.size() >= 5);

        double gridX = values[0].load() + 0.0001;
        double gridY = values[1].load() + 0.0001;
        double gridZ = values[2].load() + 0.0001;
        double interpolation = values[3].load();
        double gridDelay = values[5].load();

        head++;
        if (head >= (int)buffer.size()) {
            head = 0;
        }
        buffer[head] = input;

        osci::Point grid = osci::Point(gridX, gridY, gridZ);
        osci::Point gridFloor = osci::Point(std::floor(gridX), std::floor(gridY), std::floor(gridZ));

        gridFloor.x = std::max(gridFloor.x, 1.0);
        gridFloor.y = std::max(gridFloor.y, 1.0);
        gridFloor.z = std::max(gridFloor.z, 1.0);

        double totalPositions = gridFloor.x * gridFloor.y * gridFloor.z;
        double position = phase * totalPositions;
        double delayPosition = static_cast<int>(position) / totalPositions;

        phase = nextPhase(audioProcessor.frequency / totalPositions, sampleRate) / (2.0 * std::numbers::pi);

        int delayedIndex = head - static_cast<int>(delayPosition * gridDelay * sampleRate);
        if (delayedIndex < 0) {
            delayedIndex += buffer.size();
        }
        osci::Point delayedInput = buffer[delayedIndex % buffer.size()];

        osci::Point nextGrid = gridFloor + 1.0;

        osci::Point current = multiplex(delayedInput, position, gridFloor);
        osci::Point next = multiplex(delayedInput, position, nextGrid);

        // Calculate interpolation factors
        osci::Point gridDiff = grid - gridFloor;
        osci::Point interpolationFactor = gridDiff * interpolation;

        return (1.0 - interpolationFactor) * current + interpolationFactor * next;
    }

    std::shared_ptr<osci::Effect> build() const override {
        auto eff = std::make_shared<osci::Effect>(
            std::make_shared<MultiplexEffect>(audioProcessor),
            std::vector<osci::EffectParameter*>{
                new osci::EffectParameter("Multiplex X", "Controls the horizontal grid size for the multiplex effect.", "multiplexGridX", VERSION_HINT, 2.0, 1.0, 8.0),
                new osci::EffectParameter("Multiplex Y", "Controls the vertical grid size for the multiplex effect.", "multiplexGridY", VERSION_HINT, 2.0, 1.0, 8.0),
                new osci::EffectParameter("Multiplex Z", "Controls the depth grid size for the multiplex effect.", "multiplexGridZ", VERSION_HINT, 1.0, 1.0, 8.0),
                new osci::EffectParameter("Multiplex Smooth", "Controls the smoothness of transitions between grid sizes.", "multiplexSmooth", VERSION_HINT, 0.0, 0.0, 1.0),
                new osci::EffectParameter("Multiplex Delay", "Controls the delay of the audio samples used in the multiplex effect.", "gridDelay", VERSION_HINT, 0.0, 0.0, 1.0),
            }
        );
        eff->setName("Multiplex");
        eff->setIcon(BinaryData::multiplex_svg);
        return eff;
    }

private:
    osci::Point multiplex(osci::Point point, double position, osci::Point grid) {
        osci::Point unit = 1.0 / grid;

        point *= unit;
        point.x = -point.x;
        point.y = -point.y;

        double xPosRaw = std::floor(position);
        double yPosRaw = std::floor(position / grid.x);
        double zPosRaw = std::floor(position / (grid.x * grid.y));

        // Use fmod for positive modulo with doubles
        double xPos = std::fmod(std::fmod(xPosRaw, grid.x) + grid.x, grid.x);
        double yPos = std::fmod(std::fmod(yPosRaw, grid.y) + grid.y, grid.y);
        double zPos = std::fmod(std::fmod(zPosRaw, grid.z) + grid.z, grid.z);

        point.x -= (grid.x - 1.0) / grid.x;
        point.y += (grid.y - 1.0) / grid.y;
        point.z += (grid.z - 1.0) / grid.z;

        point.x += xPos * 2.0 * unit.x;
        point.y -= yPos * 2.0 * unit.y;
        point.z -= zPos * 2.0 * unit.z;

        return point;
    }

    OscirenderAudioProcessor &audioProcessor;
    double phase = 0.0; // Normalised 0..1 phase for multiplex traversal
    const static int MAX_DELAY = 192000 * 10;
    std::vector<osci::Point> buffer = std::vector<osci::Point>(MAX_DELAY);
    int head = 0;
};
