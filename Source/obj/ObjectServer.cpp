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

                            auto objects = *json.getProperty("objects", juce::Array<juce::var>()).getArray();
                            std::vector<std::vector<double>> allMatrices;
                            std::vector<std::vector<std::vector<Vector3D>>> allVertices;

                            double focalLength = json.getProperty("focalLength", 1);

                            for (int i = 0; i < objects.size(); i++) {
                                auto verticesArray = *objects[i].getProperty("vertices", juce::Array<juce::var>()).getArray();
                                std::vector<std::vector<Vector3D>> vertices;

                                for (auto& vertexArrayVar : verticesArray) {
                                    vertices.push_back(std::vector<Vector3D>());
                                    auto& vertexArray = *vertexArrayVar.getArray();
                                    for (auto& vertex : vertexArray) {
                                        double x = vertex.getProperty("x", 0);
                                        double y = vertex.getProperty("y", 0);
                                        double z = vertex.getProperty("z", 0);
                                        vertices[vertices.size() - 1].push_back(Vector3D(x, y, z));
                                    }
                                }
                                auto matrix = *objects[i].getProperty("matrix", juce::Array<juce::var>()).getArray();

                                allMatrices.push_back(std::vector<double>());
                                for (auto& value : matrix) {
                                    allMatrices[i].push_back(value);
                                }

                                std::vector<std::vector<Vector3D>> reorderedVertices;

                                if (vertices.size() > 0 && matrix.size() == 16) {
                                    std::vector<bool> visited = std::vector<bool>(vertices.size(), false);
                                    std::vector<int> order = std::vector<int>(vertices.size(), 0);
                                    visited[0] = true;

                                    auto endPoint = vertices[0].back();

                                    for (int i = 1; i < vertices.size(); i++) {
                                        int minPath = 0;
                                        double minDistance = 9999999;
                                        for (int j = 0; j < vertices.size(); j++) {
                                            if (!visited[j]) {
                                                auto startPoint = vertices[j][0];

                                                double diffX = endPoint.x - startPoint.x;
                                                double diffY = endPoint.y - startPoint.y;
                                                double diffZ = endPoint.z - startPoint.z;

                                                double distance = std::sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ);
                                                if (distance < minDistance) {
                                                    minPath = j;
                                                    minDistance = distance;
                                                }
                                            }
                                        }
                                        visited[minPath] = true;
                                        order[i] = minPath;
                                        endPoint = vertices[minPath].back();
                                    }

                                    for (int i = 0; i < vertices.size(); i++) {
                                        std::vector<Vector3D> reorderedVertex;
                                        int index = order[i];
                                        for (int j = 0; j < vertices[index].size(); j++) {
                                            reorderedVertex.push_back(vertices[index][j]);
                                        }
                                        reorderedVertices.push_back(reorderedVertex);
                                    }
                                }

                                allVertices.push_back(reorderedVertices);
                            }

                            // generate a frame from the vertices and matrix
                            std::vector<std::unique_ptr<Shape>> frame;

                            for (int i = 0; i < objects.size(); i++) {
                                for (int j = 0; j < allVertices[i].size(); j++) {
                                    for (int k = 0; k < allVertices[i][j].size() - 1; k++) {
                                        auto start = allVertices[i][j][k];
                                        auto end = allVertices[i][j][k + 1];

                                        // multiply the start and end points by the matrix
                                        double rotatedX = start.x * allMatrices[i][0] + start.y * allMatrices[i][1] + start.z * allMatrices[i][2] + allMatrices[i][3];
                                        double rotatedY = start.x * allMatrices[i][4] + start.y * allMatrices[i][5] + start.z * allMatrices[i][6] + allMatrices[i][7];
                                        double rotatedZ = start.x * allMatrices[i][8] + start.y * allMatrices[i][9] + start.z * allMatrices[i][10] + allMatrices[i][11];

                                        double rotatedX2 = end.x * allMatrices[i][0] + end.y * allMatrices[i][1] + end.z * allMatrices[i][2] + allMatrices[i][3];
                                        double rotatedY2 = end.x * allMatrices[i][4] + end.y * allMatrices[i][5] + end.z * allMatrices[i][6] + allMatrices[i][7];
                                        double rotatedZ2 = end.x * allMatrices[i][8] + end.y * allMatrices[i][9] + end.z * allMatrices[i][10] + allMatrices[i][11];

                                        double x = rotatedX * focalLength / rotatedZ;
                                        double y = rotatedY * focalLength / rotatedZ;

                                        double x2 = rotatedX2 * focalLength / rotatedZ2;
                                        double y2 = rotatedY2 * focalLength / rotatedZ2;

                                        frame.push_back(std::make_unique<Line>(x, y, x2, y2));
                                    }
                                }
                            }

                            audioProcessor.objectServerSound->addFrame(frame, false);
                        }
                    }
                }

            }
        }
    }
}
