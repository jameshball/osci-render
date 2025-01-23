#pragma once

#include <JuceHeader.h>
#include "../gpla/LineArtParser.h"

class OscirenderAudioProcessor;
class ObjectServer : public juce::Thread {
public:
    ObjectServer(OscirenderAudioProcessor& p);
    ~ObjectServer();

    void run() override;
    void reload();

    int port = 51677;

private:
    OscirenderAudioProcessor& audioProcessor;

    juce::StreamingSocket socket;
};