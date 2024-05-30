#include "LineArtParser.h"


LineArtParser::LineArtParser(juce::String fileContents, bool b64) {
    if (!b64) {
        parseJsonFrames(fileContents);
    }
    else {
        parseBinaryFrames(fileContents);
    }
}

LineArtParser::~LineArtParser() {
    frames.clear();
}

void LineArtParser::parseBinaryFrames(juce::String base64) {
    juce::MemoryOutputStream bin;

    // If json parse failed, stop and parse default fallback instead
    if (!juce::Base64::convertFromBase64(bin, base64)) {
        return parsingFailed(0);
    }

    int dataSize = bin.getDataSize();
    char * data = (char*)bin.getData();
    const char* eof = data + dataSize;

    // Make sure that the file is tagged as a binary GPLA file
    const char header[19] = "osci-render gpla v";
    bool isTagged = true;
    for (int i = 0; i < 18; i++) {
        isTagged = (data[i] == header[i]) && isTagged;
    }
    if (!isTagged) return parsingFailed(0);

    // Move the data pointer to the beginning of the actual data
    data = data + 23;

    // Make sure there is space for an integer, then read how many frames are contained
    if (data + 3 >= eof) return parsingFailed(0);
    numFrames = ((int*)data)[0];
    data += sizeof(int);

    // If json does not contain any frames, stop and parse no-frames fallback instead
    if (numFrames == 0) return parsingFailed(1);

    bool hasValidFrames = false;
    for (int f = 0; f < numFrames; f++) {
        std::vector < std::vector < std::vector<Point>>> frameVertices;
        std::vector<std::vector<double>> matrices;

        double focalLength;
        int nObjects;

        // Make sure there is space for some data, then read the focal length and number of objects
        if (data + sizeof(float) + sizeof(int) >= eof) return parsingFailed(0);
        focalLength = ((float*)data)[0];
        data += sizeof(float);
        nObjects = ((int*)data)[0];
        data += sizeof(int);

        // Ensure that the reported number of objects is not negative
        if (nObjects < 0) return parsingFailed(0);

        // Construct the set of objects
        for (int o = 0; o < nObjects; o++) {
            std::vector<std::vector<Point>> strokes;
            if (data + 3 >= eof) return parsingFailed(0);
            int nToSkip = ((int*)data)[0];
            data += sizeof(int);
            data += nToSkip;

            // Make sure there is space, then read how many strokes there are
            if (data + 3 >= eof) return parsingFailed(0);
            int nStrokes = ((int*)data)[0];
            data += 4;
            
            for (int stroke = 0; stroke < nStrokes; stroke++) {

                if (data + 3 >= eof) return parsingFailed(0);
                int nLines = ((int*)data)[0];
                data += 4;

                std::vector<Point> strokePoints;
                int linesRead = 0;
                while (linesRead < nLines / 3) {
                    if (data + 24 > eof) return parsingFailed(0);
                    float* vertexData = (float*)data;
                    strokePoints.push_back(Point(vertexData[0], vertexData[1], vertexData[2]));
                    data += 12;
                    linesRead += 1;
                }
                strokes.push_back(strokePoints);
            }
            frameVertices.push_back(strokes);

            if (data + 16 * sizeof(float) > eof) return parsingFailed(0);
            float* matrixData = (float*)data;
            std::vector<double> matrix;
            for (int m = 0; m < 16; m++) {
                matrix.push_back((double)matrixData[m]);
            }
            matrices.push_back(matrix);
            data += 16 * sizeof(float);
        }

        std::vector<Line> frame = generateFrame(frameVertices, matrices, focalLength);
        if (frame.size() > 0) {
            hasValidFrames = true;
        }
        frames.push_back(frame);
    }
    
    // If no frames were valid, stop and parse invalid fallback instead
    if (!hasValidFrames) return parsingFailed(2);
}

void LineArtParser::parseJsonFrames(juce::String jsonStr) {
    frames.clear();
    numFrames = 0;

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
    if (json.isVoid()) {
        parsingFailed(0);
        return;
    }

    auto jsonFrames = *json.getProperty("frames", juce::Array<juce::var>()).getArray();
    numFrames = jsonFrames.size();

    // If json does not contain any frames, stop and parse no-frames fallback instead
    if (numFrames == 0) {
        parsingFailed(1);
        return;
    }

    bool hasValidFrames = false;

    for (int f = 0; f < numFrames; f++) {
        juce::Array<juce::var> objects = *jsonFrames[f].getProperty("objects", juce::Array<juce::var>()).getArray();
        juce::var focalLengthVar = jsonFrames[f].getProperty("focalLength", juce::var());

        // Ensure that there actually are objects and that the focal length is defined
        if (objects.size() > 0 && !focalLengthVar.isVoid()) {
            std::vector<Line> frame = generateJsonFrame(objects, focalLengthVar);
            if (frame.size() > 0) {
                hasValidFrames = true;
            }
            frames.push_back(frame);
        }
    }

    // If no frames were valid, stop and parse invalid fallback instead
    if (!hasValidFrames) {
        parsingFailed(2);
        return;
    }
}

void LineArtParser::setFrame(int fNum) {
    // Ensure that the frame number is within the bounds of the number of frames
    // This weird modulo trick is to handle negative numbers
    frameNumber = (numFrames + (fNum % numFrames)) % numFrames;
}

std::vector<std::unique_ptr<Shape>> LineArtParser::draw() {
	std::vector<std::unique_ptr<Shape>> tempShapes;
	
	for (Line shape : frames[frameNumber]) {
		tempShapes.push_back(shape.clone());
	}
    return tempShapes;
}

std::vector<Line> LineArtParser::generateJsonFrame(juce::Array<juce::var> objects, double focalLength) {
    std::vector < std::vector < std::vector<Point>>> frameVertices;
    std::vector<std::vector<double>> matrices;

    for (int o = 0; o < objects.size(); o++) {
        auto verticesArray = *objects[o].getProperty("vertices", juce::Array<juce::var>()).getArray();
        std::vector<std::vector<Point>> vertices;

        for (auto& vertexArrayVar : verticesArray) {
            vertices.push_back(std::vector<Point>());
            auto& vertexArray = *vertexArrayVar.getArray();
            for (auto& vertex : vertexArray) {
                double x = vertex.getProperty("x", 0);
                double y = vertex.getProperty("y", 0);
                double z = vertex.getProperty("z", 0);
                vertices[vertices.size() - 1].push_back(Point(x, y, z));
            }
        }
        frameVertices.push_back(vertices);
        auto matrix = *objects[o].getProperty("matrix", juce::Array<juce::var>()).getArray();

        matrices.push_back(std::vector<double>());
        for (auto& value : matrix) {
            matrices[o].push_back(value);
        }
    }

    return generateFrame(frameVertices, matrices, focalLength);
}

std::vector<Line> LineArtParser::generateFrame(std::vector< std::vector<std::vector<Point>>> inputVertices, std::vector<std::vector<double>> inputMatrices, double focalLength)
{
    std::vector<std::vector<double>> allMatrices = inputMatrices;
    std::vector<std::vector<std::vector<Point>>> allVertices;

    for (int i = 0; i < inputVertices.size(); i++) {
        std::vector<std::vector<Point>> vertices = inputVertices[i];

        std::vector<std::vector<Point>> reorderedVertices;

        if (vertices.size() > 0 && allMatrices[i].size() == 16) {
            std::vector<bool> visited = std::vector<bool>(vertices.size(), false);
            std::vector<int> order = std::vector<int>(vertices.size(), 0);
            visited[0] = true;

            auto endPoint = vertices[0].back();

            for (int j = 1; j < vertices.size(); j++) {
                int minPath = 0;
                double minDistance = 9999999;
                for (int k = 0; k < vertices.size(); k++) {
                    if (!visited[k]) {
                        auto startPoint = vertices[k][0];

                        double diffX = endPoint.x - startPoint.x;
                        double diffY = endPoint.y - startPoint.y;
                        double diffZ = endPoint.z - startPoint.z;

                        double distance = std::sqrt(diffX * diffX + diffY * diffY + diffZ * diffZ);
                        if (distance < minDistance) {
                            minPath = k;
                            minDistance = distance;
                        }
                    }
                }
                visited[minPath] = true;
                order[j] = minPath;
                endPoint = vertices[minPath].back();
            }

            for (int j = 0; j < vertices.size(); j++) {
                std::vector<Point> reorderedVertex;
                int index = order[j];
                for (int k = 0; k < vertices[index].size(); k++) {
                    reorderedVertex.push_back(vertices[index][k]);
                }
                reorderedVertices.push_back(reorderedVertex);
            }
        }

        allVertices.push_back(reorderedVertices);
    }

    // generate a frame from the vertices and matrix
    std::vector<Line> frame;

    for (int i = 0; i < inputVertices.size(); i++) {
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

                    frame.push_back(Line(x, y, x2, y2));
                }
            }
        }
    }
    return frame;
}

// Codes: 0 - Parsing failed | 1 - No frames read | 2 - No valid frames
void LineArtParser::parsingFailed(int code) {
    switch (code) {
    case 0:
        return parseJsonFrames(juce::String(BinaryData::fallback_gpla, BinaryData::fallback_gplaSize));
    case 1:
        return parseJsonFrames(juce::String(BinaryData::noframes_gpla, BinaryData::noframes_gplaSize));
    case 2:
        return parseJsonFrames(juce::String(BinaryData::invalid_gpla, BinaryData::invalid_gplaSize));
    default:
        return parseJsonFrames(juce::String(BinaryData::fallback_gpla, BinaryData::fallback_gplaSize));
    }
}