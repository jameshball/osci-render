#pragma once

class SampleRateManager {
public:
    virtual ~SampleRateManager() = default;
    virtual double getEffectiveSampleRate() = 0;
};
