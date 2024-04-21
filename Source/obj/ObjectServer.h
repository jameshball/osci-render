#pragma once

#include <JuceHeader.h>
#include "../gpla/LineArtParser.h"

class OscirenderAudioProcessor;
class ObjectServer : public juce::Thread {
public:
    ObjectServer(OscirenderAudioProcessor& p);
    ~ObjectServer();

    void run() override;

private:
    OscirenderAudioProcessor& audioProcessor;

    juce::StreamingSocket socket;
};