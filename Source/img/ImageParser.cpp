#include "ImageParser.h"
#include "gifdec.h"
#include "../PluginProcessor.h"

ImageParser::ImageParser(OscirenderAudioProcessor& p, juce::String extension, juce::MemoryBlock image) : audioProcessor(p) {
    juce::TemporaryFile temp{".gif"};
    juce::File file = temp.getFile();

    {
        juce::FileOutputStream output(file);

        if (output.openedOk()) {
            output.write(image.getData(), image.getSize());
            output.flush();
        } else {
            handleError("The image could not be loaded.");
            return;
        }
    }
    
    if (extension.equalsIgnoreCase(".gif")) {
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
                    // value of 0 is reserved for transparent pixels
                    frames[i][j / 3] = juce::jmax(1, (int) avg);
                }

                i++;
            }

            gd_close_gif(gif);
        } else {
            handleError("The image could not be loaded. Please try optimising the GIF with https://ezgif.com/optimize.");
            return;
        }
    } else {
        juce::Image image = juce::ImageFileFormat::loadFrom(file);
        if (image.isValid()) {
            image.desaturate();
            
            width = image.getWidth();
            height = image.getHeight();
            int frameSize = width * height;
            
            visited = std::vector<bool>(frameSize, false);
            frames.emplace_back(std::vector<uint8_t>(frameSize));
            
            for (int x = 0; x < width; x++) {
                for (int y = 0; y < height; y++) {
                    juce::Colour pixel = image.getPixelAt(x, y);
                    int index = y * width + x;
                    // RGB should be equal since we have desaturated
                    int value = pixel.getRed();
                    // value of 0 is reserved for transparent pixels
                    frames[0][index] = pixel.isTransparent() ? 0 : juce::jmax(1, value);
                }
            }
        } else {
            handleError("The image could not be loaded.");
            return;
        }
    }
    
    if (frames.size() == 0) {
        if (extension.equalsIgnoreCase(".gif")) {
            handleError("The image could not be loaded. Please try optimising the GIF with https://ezgif.com/optimize.");
        } else {
            handleError("The image could not be loaded.");
        }
        return;
    }

    setFrame(0);
}

ImageParser::~ImageParser() {}

void ImageParser::handleError(juce::String message) {
    juce::MessageManager::callAsync([this, message] {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::AlertIconType::WarningIcon, "Error", message);
    });
    
    width = 1;
    height = 1;
    frames.emplace_back(std::vector<uint8_t>(1));
    setFrame(0);
}

void ImageParser::setFrame(int index) {
    // Ensure that the frame number is within the bounds of the number of frames
    // This weird modulo trick is to handle negative numbers
    frameIndex = (frames.size() + (index % frames.size())) % frames.size();
    resetPosition();
    std::fill(visited.begin(), visited.end(), false);
}

bool ImageParser::isOverThreshold(double pixel, double thresholdPow) {
    float threshold = std::pow(pixel, thresholdPow);
    return pixel > 0.2 && rng.nextFloat() < threshold;
}

void ImageParser::resetPosition() {
    currentX = width > 0 ? rng.nextInt(width) : 0;
    currentY = height > 0 ? rng.nextInt(height) : 0;
}

float ImageParser::getPixelValue(int x, int y, bool invert) {
    int index = (height - y - 1) * width + x;
    if (index < 0 || index >= frames[frameIndex].size()) {
        return 0;
    }
    float pixel = frames[frameIndex][index] / (float) std::numeric_limits<uint8_t>::max();
    // never traverse transparent pixels
    if (invert && pixel > 0) {
        pixel = 1 - pixel;
    }
    return pixel;
}

void ImageParser::findWhite(double thresholdPow, bool invert) {
    for (int i = 0; i < 100; i++) {
        resetPosition();
        if (isOverThreshold(getPixelValue(currentX, currentY, invert), thresholdPow)) {
            break;
        }
    }
}

int ImageParser::jumpFrequency() {
    return audioProcessor.currentSampleRate * 0.005;
}

void ImageParser::findNearestNeighbour(int searchRadius, float thresholdPow, int stride, bool invert) {
    int spiralSteps[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
    int maxSteps = 2 * searchRadius; // Maximum steps outwards in the spiral
    int x = currentX;
    int y = currentY;
    int dir = rng.nextInt(4);

    for (int len = 1; len <= maxSteps; len++) { // Length of spiral arm
        for (int i = 0 ; i < 2; i++) { // Repeat twice for each arm length
            for (int step = 0 ; step < len; step++) { // Steps in the current direction
                x += stride * spiralSteps[dir][0];
                y += stride * spiralSteps[dir][1];

                if (x < 0 || x >= width || y < 0 || y >= height) break;
                
                float pixel = getPixelValue(x, y, invert);

                int index = (height - y - 1) * width + x;
                if (isOverThreshold(pixel, thresholdPow) && !visited[index]) {
                    visited[index] = true;
                    currentX = x;
                    currentY = y;
                    return;
                }
            }

            dir = (dir + 1) % 4; // Change direction after finishing one leg of the spiral
        }
    }

    findWhite(thresholdPow, invert);
}

OsciPoint ImageParser::getSample() {
    if (ALGORITHM == "HILLIGOSS") {
        if (count % jumpFrequency() == 0) {
            resetPosition();
        }
        
        if (count % 10 * jumpFrequency() == 0) {
            std::fill(visited.begin(), visited.end(), false);
        }
        
        float thresholdPow = audioProcessor.imageThreshold->getActualValue() * 10 + 1;
        
        findNearestNeighbour(10, thresholdPow, audioProcessor.imageStride->getActualValue(), audioProcessor.invertImage->getValue());
        float maxDim = juce::jmax(width, height);
        count++;
        float widthDiff = (maxDim - width) / 2;
        float heightDiff = (maxDim - height) / 2;
        return OsciPoint(2 * (currentX + widthDiff) / maxDim - 1, 2 * (currentY + heightDiff) / maxDim - 1);
    } else {
        double scanIncrement = audioProcessor.imageStride->getActualValue() / 100;
        
        double pixel = 0;
        int maxIterations = 10000;
        while (pixel <= audioProcessor.imageThreshold->getActualValue() && maxIterations > 0) {
            int x = (int) ((scanX + 1) * width / 2);
            int y = (int) ((scanY + 1) * height / 2);
            pixel = getPixelValue(x, y, audioProcessor.invertImage->getValue());
            
            double increment = 0.01;
            if (pixel > audioProcessor.imageThreshold->getActualValue()) {
                increment = (1 - tanh(4 * pixel)) * 0.3;
            }
            
            scanX += increment;
            if (scanX >= 1) {
                scanX = -1;
                scanY -= audioProcessor.imageStride->getActualValue() / 100;
            }
            if (scanY < -1) {
                double offset = ((scanCount % 15) / 15.0) * scanIncrement;
                scanY = 1 - offset;
                scanCount++;
            }
            
            maxIterations--;
        }
        
        return OsciPoint(scanX, scanY);
    }
}
