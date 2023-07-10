#include "AudioWebSocketServer.h"

AudioWebSocketServer::AudioWebSocketServer(BufferProducer& producer) : juce::Thread("AudioWebSocketServer"), producer(producer) {
    server.setOnClientMessageCallback([](std::shared_ptr<ix::ConnectionState> connectionState, ix::WebSocket & webSocket, const ix::WebSocketMessagePtr & msg) {
        // The ConnectionState object contains information about the connection,
        // at this point only the client ip address and the port.
        DBG("Remote ip: " << connectionState->getRemoteIp());

        if (msg->type == ix::WebSocketMessageType::Open) {
            DBG("New connection");

            // A connection state object is available, and has a default id
            // You can subclass ConnectionState and pass an alternate factory
            // to override it. It is useful if you want to store custom
            // attributes per connection (authenticated bool flag, attributes, etc...)
            DBG("id: " << connectionState->getId());

            // The uri the client did connect to.
            DBG("Uri: " << msg->openInfo.uri);

            DBG("Headers:");
            for (auto it : msg->openInfo.headers) {
                DBG("\t" << it.first << ": " << it.second);
            }
        }
    });

    ix::initNetSystem();

    auto res = server.listen();
    if (res.first) {
        server.disablePerMessageDeflate();
        server.start();
        startThread();
    }

    // TODO: don't silently fail
}

AudioWebSocketServer::~AudioWebSocketServer() {
    server.stop();
    ix::uninitNetSystem();
    producer.unregisterConsumer(consumer);
    stopThread(1000);
}

void AudioWebSocketServer::run() {
    producer.registerConsumer(consumer);

    while (!threadShouldExit()) {
        auto floatBuffer = consumer->startProcessing();

        for (int i = 0; i < floatBuffer->size(); i++) {
            short sample = floatBuffer->at(i) * 32767;
            char b0 = sample & 0xff;
            char b1 = (sample >> 8) & 0xff;
            buffer[2 * i] = b0;
            buffer[2 * i + 1] = b1;
        }
        
        for (auto&& client : server.getClients()) {
            ix::IXWebSocketSendData data{buffer, sizeof(buffer)};
            client->sendBinary(data);
        }
        
        consumer->finishedProcessing();
    }
    
}
