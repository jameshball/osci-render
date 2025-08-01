#pragma once
#include <JuceHeader.h>
#include <cmath>

class MultiplexEffect : public osci::EffectApplication {
public:
    osci::Point apply(int index, osci::Point input, const std::vector<std::atomic<double>>& values, double sampleRate) override {
        jassert(values.size() >= 6);
        
        double gridX = values[0].load();
        double gridY = values[1].load();
        double gridZ = values[2].load();
        double interpolation = values[3].load();
        double phase = values[4].load();
        double gridDelay = values[5].load();
        
        head++;
        
        if (head >= buffer.size()) {
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
    
    const static int MAX_DELAY = 192000 * 10;
    std::vector<osci::Point> buffer = std::vector<osci::Point>(MAX_DELAY);
    int head = 0;
};
