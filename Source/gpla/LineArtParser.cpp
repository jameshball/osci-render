#include "LineArtParser.h"


LineArtParser::LineArtParser(juce::String json) {
    parseJsonFrames(json);
}

LineArtParser::~LineArtParser() {
    frames.clear();
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
        parseJsonFrames(juce::String(BinaryData::fallback_gpla, BinaryData::fallback_gplaSize));
        return;
    }

    auto jsonFrames = *json.getProperty("frames", juce::Array<juce::var>()).getArray();
    numFrames = jsonFrames.size();

    // If json does not contain any frames, stop and parse no-frames fallback instead
    if (numFrames == 0) {
        parseJsonFrames(juce::String(BinaryData::noframes_gpla, BinaryData::noframes_gplaSize));
        return;
    }

    bool hasValidFrames = false;

    for (int f = 0; f < numFrames; f++) {
        juce::Array<juce::var> objects = *jsonFrames[f].getProperty("objects", juce::Array<juce::var>()).getArray();
        juce::var focalLengthVar = jsonFrames[f].getProperty("focalLength", juce::var());

        // Ensure that there actually are objects and that the focal length is defined
        if (objects.size() > 0 && !focalLengthVar.isVoid()) {
            double focalLength = focalLengthVar;
            std::vector<Line> frame = generateFrame(objects, focalLength);
            if (frame.size() > 0) {
                hasValidFrames = true;
            }
            frames.push_back(frame);
        }
    }

    // If no frames were valid, stop and parse invalid fallback instead
    if (!hasValidFrames) {
        parseJsonFrames(juce::String(BinaryData::invalid_gpla, BinaryData::invalid_gplaSize));
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


std::vector<Line> LineArtParser::generateFrame(juce::Array <juce::var> objects, double focalLength)
{
    std::vector<std::vector<double>> allMatrices;
    std::vector<std::vector<std::vector<OsciPoint>>> allVertices;

    for (int i = 0; i < objects.size(); i++) {
        auto verticesArray = *objects[i].getProperty("vertices", juce::Array<juce::var>()).getArray();
        std::vector<std::vector<OsciPoint>> vertices;

        for (auto& vertexArrayVar : verticesArray) {
            vertices.push_back(std::vector<OsciPoint>());
            auto& vertexArray = *vertexArrayVar.getArray();
            for (auto& vertex : vertexArray) {
                double x = vertex.getProperty("x", 0);
                double y = vertex.getProperty("y", 0);
                double z = vertex.getProperty("z", 0);
                vertices[vertices.size() - 1].push_back(OsciPoint(x, y, z));
            }
        }
        auto matrix = *objects[i].getProperty("matrix", juce::Array<juce::var>()).getArray();

        allMatrices.push_back(std::vector<double>());
        for (auto& value : matrix) {
            allMatrices[i].push_back(value);
        }

        std::vector<std::vector<OsciPoint>> reorderedVertices;

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
                std::vector<OsciPoint> reorderedVertex;
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
    std::vector<Line> frame;

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
