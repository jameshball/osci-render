#pragma once

class SampleRateManager {
public:
    virtual ~SampleRateManager() = default;
    virtual double getSampleRate() = 0;
};
