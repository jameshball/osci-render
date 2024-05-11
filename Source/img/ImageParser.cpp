#include "ImageParser.h"
#include "gifdec.h"
#include "../PluginProcessor.h"

ImageParser::ImageParser(OscirenderAudioProcessor& p, juce::String extension, juce::MemoryBlock image) : audioProcessor(p) {
    if (extension.equalsIgnoreCase(".gif")) {
        juce::TemporaryFile temp{".gif"};
        juce::File file = temp.getFile();

        {
            juce::FileOutputStream output(file);

            if (output.openedOk()) {
                output.write(image.getData(), image.getSize());
                output.flush();
            }
        }

        juce::String fileName = file.getFullPathName();
        gd_GIF *gif = gd_open_gif(fileName.toRawUTF8());

        if (gif != nullptr) {
            width = gif->width;
            height = gif->height;
            int frameSize = width * height;
            std::vector<uint8_t> tempBuffer = std::vector<uint8_t>(frameSize * 3);
            visited = std::vector<bool>(frameSize, false);

            int i = 0;
            while (gd_get_frame(gif) > 0) {
                gd_render_frame(gif, tempBuffer.data());

                frames.emplace_back(std::vector<uint8_t>(frameSize));

                uint8_t *pixels = tempBuffer.data();
                for (int j = 0; j < tempBuffer.size(); j += 3) {
                    uint8_t avg = (pixels[j] + pixels[j + 1] + pixels[j + 2]) / 3;
                    frames[i][j / 3] = avg;
                }

                i++;
            }

            gd_close_gif(gif);
        }
    }

    setFrame(0);
}

ImageParser::~ImageParser() {}

void ImageParser::setFrame(int index) {
    // Ensure that the frame number is within the bounds of the number of frames
    // This weird modulo trick is to handle negative numbers
    frameIndex = (frames.size() + (index % frames.size())) % frames.size();
    resetPosition();
    std::fill(visited.begin(), visited.end(), false);
}

void ImageParser::resetPosition() {
    currentX = rng.nextInt(width);
    currentY = rng.nextInt(height);
}

void ImageParser::findNearestNeighbour(int searchRadius, float thresholdPow, int stride, bool invert) {
    int spiralSteps[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    int maxSteps = 2 * searchRadius; // Maximum steps outwards in the spiral
    int x = currentX;
    int y = currentY;
    int dir = rng.nextInt(4);

    float maxValue = std::numeric_limits<uint8_t>::max();

    for (int len = 1; len <= maxSteps; len++) { // Length of spiral arm
        for (int i = 0 ; i < 2; i++) { // Repeat twice for each arm length
            for (int step = 0 ; step < len; step++) { // Steps in the current direction
                x += stride * spiralSteps[dir][0];
                y += stride * spiralSteps[dir][1];

                if (x < 0 || x >= width || y < 0 || y >= height) break;

                int index = (height - y - 1) * height + x;
                float pixel = frames[frameIndex][index] / maxValue;
                if (invert) {
                    pixel = 1 - pixel;
                }
                float threshold = std::pow(pixel, thresholdPow);

                if (pixel > 0.2 && rng.nextFloat() < threshold && !visited[index]) {
                    visited[index] = true;
                    currentX = x;
                    currentY = y;
                    return;
                }
            }

            dir = (dir + 1) % 4; // Change direction after finishing one leg of the spiral
        }
    }

    resetPosition();
}

Point ImageParser::getSample() {
    if (count % 100 == 0) {
        resetPosition();
    }

    float thresholdPow = audioProcessor.imageThreshold->getValue() * 10 + 1;

    findNearestNeighbour(50, thresholdPow, audioProcessor.imageStride->getValue(), audioProcessor.invertImage->getValue());
    float maxDim = juce::jmax(width, height);
    count++;
    return Point(2 * currentX / maxDim - 1, 2 * currentY / maxDim - 1);
}
