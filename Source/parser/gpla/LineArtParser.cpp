#include "LineArtParser.h"


LineArtParser::LineArtParser(juce::String json) {
    frames.clear();
    numFrames = 0;
    frames = parseJsonFrames(json);
    numFrames = frames.size();
}

LineArtParser::LineArtParser(char* data, int dataLength) {
    frames.clear();
    numFrames = 0;
    frames = parseBinaryFrames(data, dataLength);
    numFrames = frames.size();
    if (numFrames == 0) frames = epicFail();
}

LineArtParser::~LineArtParser() {
    frames.clear();
}

double LineArtParser::makeDouble(int64_t data) {
    return *(double*)&data;
}

void LineArtParser::makeChars(int64_t data, char* chars) {
    for (int i = 0; i < 8; i++) {
        chars[i] = (data >> (i * 8)) & 0xFF;
    }
}

std::vector<std::vector<osci::Line>> LineArtParser::epicFail() {
    return parseJsonFrames(juce::String(BinaryData::fallback_gpla, BinaryData::fallback_gplaSize));
}

std::vector<std::vector<osci::Line>> LineArtParser::parseBinaryFrames(char* bytes, int bytesLength) {
    int64_t* data = (int64_t*)bytes;
    int dataLength = bytesLength / 8;
    std::vector<std::vector<osci::Line>> tFrames;

    if (dataLength < 4) return epicFail();

    int index = 0;
    int64_t rawData = data[index];
    index++;

    char tag[9] = "        ";
    makeChars(rawData, tag);

    if (strcmp(tag, "GPLA    ") != 0) return epicFail();

    // Major
    if (index >= dataLength) return epicFail();
    rawData = data[index];
    index++;
    // Minor
    if (index >= dataLength) return epicFail();
    rawData = data[index];
    index++;
    // Patch
    if (index >= dataLength) return epicFail();
    rawData = data[index];
    index++;

    if (index >= dataLength) return epicFail();
    rawData = data[index];
    index++;
    makeChars(rawData, tag);
    if (strcmp(tag, "FILE    ") != 0) return epicFail();

    int reportedNumFrames = 0;
    int frameRate = 0;

    if (index >= dataLength) return epicFail();
    rawData = data[index];
    index++;
    makeChars(rawData, tag);

    while (strcmp(tag, "DONE    ") != 0) {
        if (index >= dataLength) return epicFail();
        rawData = data[index];
        index++;

        if (strcmp(tag, "fCount  ") == 0) {
            reportedNumFrames = rawData;
        } else if (strcmp(tag, "fRate   ") == 0) {
            frameRate = rawData;
        }

        if (index >= dataLength) return epicFail();
        rawData = data[index];
        index++;
        makeChars(rawData, tag);
    }

    if (index >= dataLength) return epicFail();
    rawData = data[index];
    index++;
    makeChars(rawData, tag);
    
    while (strcmp(tag, "END GPLA") != 0) {
        if (strcmp(tag, "FRAME   ") == 0) {
            if (index >= dataLength) return epicFail();
            rawData = data[index];
            index++;
            makeChars(rawData, tag);

            double focalLength;
            std::vector<std::vector<double>> allMatrices;
            std::vector<std::vector<std::vector<osci::Point>>> allVertices;
            while (strcmp(tag, "OBJECTS ") != 0) {
                if (index >= dataLength) return epicFail();
                rawData = data[index];
                index++;

                if (strcmp(tag, "focalLen") == 0) {
                    focalLength = makeDouble(rawData);
                }

                if (index >= dataLength) return epicFail();
                rawData = data[index];
                index++;
                makeChars(rawData, tag);
            }

            if (index >= dataLength) return epicFail();
            rawData = data[index];
            index++;
            makeChars(rawData, tag);
            
            while (strcmp(tag, "DONE    ") != 0) {
                if (strcmp(tag, "OBJECT  ") == 0) {
                    std::vector<std::vector<osci::Point>> vertices;
                    std::vector<double> matrix;
                    if (index >= dataLength) return epicFail();
                    int strokeNum = 0;
                    rawData = data[index];
                    index++;
                    makeChars(rawData, tag);
                    while (strcmp(tag, "DONE    ") != 0) {
                        if (strcmp(tag, "MATRIX  ") == 0) {
                            matrix.clear();
                            for (int i = 0; i < 16; i++) {
                                if (index >= dataLength) return epicFail();
                                rawData = data[index];
                                index++;
                                matrix.push_back(makeDouble(rawData));
                            }
                            if (index >= dataLength) return epicFail();
                            rawData = data[index];
                            index++;
                        } else if (strcmp(tag, "STROKES ") == 0) {
                            if (index >= dataLength) return epicFail();
                            rawData = data[index];
                            index++;
                            makeChars(rawData, tag);

                            while (strcmp(tag, "DONE    ") != 0) {
                                if (strcmp(tag, "STROKE  ") == 0) {
                                    vertices.push_back(std::vector<osci::Point>());
                                    if (index >= dataLength) return epicFail();
                                    rawData = data[index];
                                    index++;
                                    makeChars(rawData, tag);

                                    int vertexCount = 0;
                                    while (strcmp(tag, "DONE    ") != 0) {
                                        if (strcmp(tag, "vertexCt") == 0) {
                                            if (index >= dataLength) return epicFail();
                                            rawData = data[index];
                                            index++;
                                            vertexCount = rawData;
                                        }
                                        else if (strcmp(tag, "VERTICES") == 0) {
                                            double x = 0;
                                            double y = 0;
                                            double z = 0;
                                            for (int i = 0; i < vertexCount; i++) {
                                                if (index + 2 >= dataLength) return epicFail();
                                                rawData = data[index];
                                                index++;
                                                x = makeDouble(rawData);

                                                rawData = data[index];
                                                index++;
                                                y = makeDouble(rawData);

                                                rawData = data[index];
                                                index++;
                                                z = makeDouble(rawData);

                                                vertices[strokeNum].push_back(osci::Point(x, y, z));
                                            }
                                            if (index >= dataLength) return epicFail();
                                            rawData = data[index];
                                            index++;
                                            makeChars(rawData, tag);
                                            while (strcmp(tag, "DONE    ") != 0) {
                                                if (index >= dataLength) return epicFail();
                                                rawData = data[index];
                                                index++;
                                                makeChars(rawData, tag);
                                            }
                                        }
                                        if (index >= dataLength) return epicFail();
                                        rawData = data[index];
                                        index++;
                                        makeChars(rawData, tag);
                                    }
                                    strokeNum++;
                                }
                                if (index >= dataLength) return epicFail();
                                rawData = data[index];
                                index++;
                                makeChars(rawData, tag);
                            }
                        }
                        if (index >= dataLength) return epicFail();
                        rawData = data[index];
                        index++;
                        makeChars(rawData, tag);
                    }
                    allVertices.push_back(reorderVertices(vertices));
                    allMatrices.push_back(matrix);
                    vertices.clear();
                    matrix.clear();
                }
                if (index >= dataLength) return epicFail();
                rawData = data[index];
                index++;
                makeChars(rawData, tag);
            }
            std::vector<osci::Line> frame = assembleFrame(allVertices, allMatrices, focalLength);
            tFrames.push_back(frame);
        }
        if (index >= dataLength) return epicFail();
        rawData = data[index];
        index++;
        makeChars(rawData, tag);
    }
    return tFrames;
}

std::vector<std::vector<osci::Line>> LineArtParser::parseJsonFrames(juce::String jsonStr) {
    std::vector<std::vector<osci::Line>> frames;

    // format of json is:
    // {
    //   "frames":[
    //     "objects": [
    //       {
    //         "name": "Line Art",
    //         "vertices": [
    //           [
    //             {
    //               "x": double value,
    //               "y": double value,
    //               "z": double value
    //             },
    //             ...
    //           ],
    //           ...
    //         ],
    //         "matrix": [
    //           16 double values
    //         ]
    //       }
    //     ],
    //     "focalLength": double value
    //   },
    //   ...
    //   ]
    // }

    auto json = juce::JSON::parse(jsonStr);

    // If json parse failed, stop and parse default fallback instead
    if (json.isVoid()) return epicFail();

    auto jsonFrames = *json.getProperty("frames", juce::Array<juce::var>()).getArray();
    int numFrames = jsonFrames.size();

    // If json does not contain any frames, stop and parse no-frames fallback instead
    if (numFrames == 0) return parseJsonFrames(juce::String(BinaryData::noframes_gpla, BinaryData::noframes_gplaSize));

    bool hasValidFrames = false;

    for (int f = 0; f < numFrames; f++) {
        juce::Array<juce::var> objects = *jsonFrames[f].getProperty("objects", juce::Array<juce::var>()).getArray();
        juce::var focalLengthVar = jsonFrames[f].getProperty("focalLength", juce::var());

        // Ensure that there actually are objects and that the focal length is defined
        if (objects.size() > 0 && !focalLengthVar.isVoid()) {
            double focalLength = focalLengthVar;
            std::vector<osci::Line> frame = generateFrame(objects, focalLength);
            if (frame.size() > 0) {
                hasValidFrames = true;
            }
            frames.push_back(frame);
        }
    }

    // If no frames were valid, stop and parse invalid fallback instead
    if (!hasValidFrames) return parseJsonFrames(juce::String(BinaryData::invalid_gpla, BinaryData::invalid_gplaSize));

    return frames;
}

void LineArtParser::setFrame(int fNum) {
    // Ensure that the frame number is within the bounds of the number of frames
    // This weird modulo trick is to handle negative numbers
    frameNumber = (numFrames + (fNum % numFrames)) % numFrames;
}

std::vector<std::unique_ptr<osci::Shape>> LineArtParser::draw() {
	std::vector<std::unique_ptr<osci::Shape>> tempShapes;
	
	for (osci::Line shape : frames[frameNumber]) {
		tempShapes.push_back(shape.clone());
	}
    return tempShapes;
}

std::vector<std::vector<osci::Point>> LineArtParser::reorderVertices(std::vector<std::vector<osci::Point>> vertices) {
    std::vector<std::vector<osci::Point>> reorderedVertices;

    if (vertices.size() > 0) {
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
            std::vector<osci::Point> reorderedVertex;
            int index = order[i];
            for (int j = 0; j < vertices[index].size(); j++) {
                reorderedVertex.push_back(vertices[index][j]);
            }
            reorderedVertices.push_back(reorderedVertex);
        }
    }
    return reorderedVertices;
}

std::vector<osci::Line> LineArtParser::generateFrame(juce::Array <juce::var> objects, double focalLength)
{
    std::vector<std::vector<double>> allMatrices;
    std::vector<std::vector<std::vector<osci::Point>>> allVertices;

    for (int i = 0; i < objects.size(); i++) {
        auto verticesArray = *objects[i].getProperty("vertices", juce::Array<juce::var>()).getArray();
        std::vector<std::vector<osci::Point>> vertices;

        for (auto& vertexArrayVar : verticesArray) {
            vertices.push_back(std::vector<osci::Point>());
            auto& vertexArray = *vertexArrayVar.getArray();
            for (auto& vertex : vertexArray) {
                double x = vertex.getProperty("x", 0);
                double y = vertex.getProperty("y", 0);
                double z = vertex.getProperty("z", 0);
                vertices[vertices.size() - 1].push_back(osci::Point(x, y, z));
            }
        }
        auto matrix = *objects[i].getProperty("matrix", juce::Array<juce::var>()).getArray();

        allMatrices.push_back(std::vector<double>());
        for (auto& value : matrix) {
            allMatrices[i].push_back(value);
        }

        allVertices.push_back(reorderVertices(vertices));
    }
    return assembleFrame(allVertices, allMatrices, focalLength);
}

std::vector<osci::Line> LineArtParser::assembleFrame(std::vector<std::vector<std::vector<osci::Point>>> allVertices, std::vector<std::vector<double>> allMatrices, double focalLength) {
    // generate a frame from the vertices and matrix
    std::vector<osci::Line> frame;

    for (int i = 0; i < allVertices.size(); i++) {
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

                // I think this discards every line with a vertex behind the camera? Needs more testing.
                // - DJ_Level_3
                if (rotatedZ < 0 && rotatedZ2 < 0) {

                    double x = rotatedX * focalLength / rotatedZ;
                    double y = rotatedY * focalLength / rotatedZ;

                    double x2 = rotatedX2 * focalLength / rotatedZ2;
                    double y2 = rotatedY2 * focalLength / rotatedZ2;

                    frame.push_back(osci::Line(x, y, x2, y2));
                }
            }
        }
    }
    return frame;
}
