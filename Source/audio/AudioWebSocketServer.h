#pragma once
#include <JuceHeader.h>
#include "../ixwebsocket/IXWebSocketServer.h"
#include "../concurrency/BufferProducer.h"

class AudioWebSocketServer : juce::Thread {
public:
	AudioWebSocketServer(BufferProducer& producer);
	~AudioWebSocketServer();

	void run() override;
private:
	ix::WebSocketServer server{ 42988 };

	BufferProducer& producer;
	std::shared_ptr<BufferConsumer> consumer = std::make_shared<BufferConsumer>(4096);
	char buffer[4096 * 2 * 2];
};