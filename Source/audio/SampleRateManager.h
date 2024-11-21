#pragma once

class SampleRateManager {
public:
    SampleRateManager() {}
    ~SampleRateManager() {}

    virtual double getSampleRate() = 0;
};
