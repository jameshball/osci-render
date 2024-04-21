#include "ObjectServer.h"
#include "../PluginProcessor.h"
#include "../shape/Line.h"

ObjectServer::ObjectServer(OscirenderAudioProcessor& p) : audioProcessor(p), juce::Thread("Object Server") {
    startThread();
}

ObjectServer::~ObjectServer() {
    stopThread(1000);
}

void ObjectServer::run() {
    if (socket.createListener(51677, "127.0.0.1")) {
        // preallocating a large buffer to avoid allocations in the loop
        std::unique_ptr<char[]> message{ new char[10 * 1024 * 1024] };

        while (!threadShouldExit()) {
            if (socket.waitUntilReady(true, 200)) {
                std::unique_ptr<juce::StreamingSocket> connection(socket.waitForNextConnection());

                if (connection != nullptr) {
                    audioProcessor.setObjectServerRendering(true);

                    while (!threadShouldExit() && connection->isConnected()) {
                        if (connection->waitUntilReady(true, 200) == 1) {
                            int i = 0;

                            // read until we get a newline
                            while (!threadShouldExit()) {
                                char buffer[1024];
                                int bytesRead = connection->read(buffer, sizeof(buffer), false);

                                if (bytesRead <= 0) {
                                    break;
                                }

                                std::memcpy(message.get() + i, buffer, bytesRead);
                                i += bytesRead;

                                for (int j = i - bytesRead; j < i; j++) {
                                    if (message[j] == '\n') {
                                        message[j] = '\0';
                                        break;
                                    }
                                }
                            }

                            if (strncmp(message.get(), "CLOSE", 5) == 0) {
                                connection->close();
                                audioProcessor.setObjectServerRendering(false);
                                break;
                            }

                            // format of json is:
                            // {
                            //   "objects": [
                            //     {
                            //       "name": "Line Art",
                            //       "vertices": [
                            //         [
                            //           {
                            //             "x": double value,
                            //             "y": double value,
                            //             "z": double value
                            //           },
                            //           ...
                            //         ],
                            //         ...
                            //       ],
                            //       "matrix": [
                            //         16 double values
                            //       ]
                            //     }
                            //   ],
                            //   "focalLength": double value
                            // }

                            auto json = juce::JSON::parse(message.get());

                            juce::Array<juce::var> objects = *json.getProperty("objects", juce::Array<juce::var>()).getArray();
                            double focalLength = json.getProperty("focalLength", 1);

                            std::vector<Line> frameContainer = LineArtParser::generateFrame(objects, focalLength);

                            std::vector<std::unique_ptr<Shape>> frame;

                            for (int i = 0; i < frameContainer.size(); i++) {
                                Line l = frameContainer[i];
                                frame.push_back(std::make_unique<Line>(l.x1, l.y1, l.x2, l.y2));
                            }

                            audioProcessor.objectServerSound->addFrame(frame, false);
                        }
                    }
                }

            }
        }
    }
}
