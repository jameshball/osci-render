#pragma once
#include <JuceHeader.h>
#include "../ixwebsocket/IXWebSocketServer.h"

class OscirenderAudioProcessor;
class AudioWebSocketServer : juce::Thread {
public:
	AudioWebSocketServer(OscirenderAudioProcessor& audioProcessor);
	~AudioWebSocketServer();

	void run() override;
private:
	ix::WebSocketServer server{ 42988 };

    OscirenderAudioProcessor& audioProcessor;
	std::vector<float> floatBuffer = std::vector<float>(2 * 4096);
	char buffer[4096 * 2 * 2];
};
