#include "ImageParser.h"
#include "gifdec.h"
#include "../PluginProcessor.h"
#include "../CommonPluginEditor.h"

ImageParser::ImageParser(OscirenderAudioProcessor& p, juce::String extension, juce::MemoryBlock image) : audioProcessor(p) {
    juce::File file = temp.getFile();

    {
        juce::FileOutputStream output(file);

        if (output.openedOk()) {
            output.write(image.getData(), image.getSize());
            output.flush();
        } else {
            handleError("The file could not be loaded.");
            return;
        }
    }
    
    if (extension.equalsIgnoreCase(".gif")) {
        processGifFile(file);
    }
#if OSCI_PREMIUM
    else if (isVideoFile(extension)) {
        processVideoFile(file);
    }
#endif
    else {
        processImageFile(file);
    }
    
    if (frames.size() == 0) {
        if (extension.equalsIgnoreCase(".gif")) {
            handleError("The image could not be loaded. Please try optimising the GIF with https://ezgif.com/optimize.");
        }
#if OSCI_PREMIUM
        else if (isVideoFile(extension)) {
            handleError("The video could not be loaded. Please check that ffmpeg is installed.");
        }
#endif
        else {
            handleError("The image could not be loaded.");
        }
        return;
    }

    setFrame(0);
}

// Constructor for live Syphon/Spout input
ImageParser::ImageParser(OscirenderAudioProcessor& p) : audioProcessor(p), usingLiveImage(true) {
    width = 1;
    height = 1;
}

void ImageParser::updateLiveFrame(const juce::Image& newImage)
{
    if (newImage.isValid()) {
        juce::SpinLock::ScopedLockType lock(liveImageLock);
        liveImage = newImage;
        liveImage.duplicateIfShared();
        width = liveImage.getWidth();
        height = liveImage.getHeight();
        visited.resize(width * height);
        visited.assign(width * height, false);
    }
}

void ImageParser::processGifFile(juce::File& file) {
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
        handleError("The GIF could not be loaded. Please try optimising the GIF with https://ezgif.com/optimize.");
    }
}

void ImageParser::processImageFile(juce::File& file) {
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
    }
}

#if OSCI_PREMIUM
bool ImageParser::isVideoFile(const juce::String& extension) const {
    return extension.equalsIgnoreCase(".mp4") || extension.equalsIgnoreCase(".mov");
}

void ImageParser::processVideoFile(juce::File& file) {
    // Set video processing flag
    isVideo = true;
    
    // assert on the message thread
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) {
        handleError("Could not process video file - not on the message thread.");
        return;
    }
    
    // Try to get ffmpeg 
    juce::File ffmpegFile = audioProcessor.getFFmpegFile();
    
    if (ffmpegFile.exists()) {
        // FFmpeg exists, continue with video processing
        if (!loadAllVideoFrames(file, ffmpegFile)) {
            handleError("Could not read video frames. Please ensure the video file is valid.");
        }
    } else {
        // Ask user to download ffmpeg
        audioProcessor.ensureFFmpegExists(nullptr, [this, file]() {
            // This will be called once ffmpeg is successfully downloaded
            juce::File ffmpegFile = audioProcessor.getFFmpegFile();
            if (!loadAllVideoFrames(file, ffmpegFile)) {
                handleError("Could not read video frames after downloading ffmpeg. Please ensure the video file is valid.");
            } else {
                // Successfully loaded frames after downloading ffmpeg
                if (frames.size() > 0) {
                    setFrame(0);
                }
            }
        });
    }
}

bool ImageParser::loadAllVideoFrames(const juce::File& file, const juce::File& ffmpegFile) {
    juce::String cmd = "\"" + ffmpegFile.getFullPathName() + "\" -i \"" + file.getFullPathName() + "\" -hide_banner 2>&1";
    
    ffmpegProcess.start(cmd);
    
    char buf[2048];
    memset(buf, 0, sizeof(buf));
    size_t size = ffmpegProcess.read(buf, sizeof(buf) - 1);
    ffmpegProcess.close();
    
    if (size > 0) {
        juce::String output(buf, size);
        
        // Look for resolution in format "1920x1080"
        std::regex resolutionRegex(R"((\d{2,5})x(\d{2,5}))");
        std::smatch match;
        std::string stdOut = output.toStdString();

        if (std::regex_search(stdOut, match, resolutionRegex) && match.size() == 3)
        {
            width = std::stoi(match[1].str());
            height = std::stoi(match[2].str());
        }
    }
    
    // If still no dimensions or dimensions are too large, use reasonable defaults
    if (width <= 0 || height <= 0) {
        width = 320;
        height = 240;
    } else {
        // Downscale large videos to improve performance
        const int MAX_DIMENSION = 512;
        if (width > MAX_DIMENSION || height > MAX_DIMENSION) {
            float aspectRatio = static_cast<float>(width) / height;
            if (width > height) {
                width = MAX_DIMENSION;
                height = static_cast<int>(width / aspectRatio);
            } else {
                height = MAX_DIMENSION;
                width = static_cast<int>(height * aspectRatio);
            }
        }
    }
    
    // Now prepare for frame reading
    int frameSize = width * height;
    videoFrameSize = frameSize;
    visited = std::vector<bool>(frameSize, false);
    frameBuffer.resize(frameSize);
    
    // Clear any existing frames
    frames.clear();
    
    // Cap the number of frames to prevent excessive memory usage
    const int MAX_FRAMES = 10000;
    
    // Determine available hardware acceleration options
#if JUCE_MAC
   // Try to use videotoolbox on macOS
   juce::String hwAccel = " -hwaccel videotoolbox";
#elif JUCE_WINDOWS
   // Try to use DXVA2 on Windows
   juce::String hwAccel = " -hwaccel dxva2";
#else
    juce::String hwAccel = "";
#endif
    
    // Start ffmpeg process to read frames with optimizations:
    // - Use hardware acceleration if available
    // - Lower resolution with scale filter
    // - Use multiple threads for faster processing
    // - Use gray colorspace directly to avoid extra conversion
    cmd = "\"" + ffmpegFile.getFullPathName() + "\"" +
        hwAccel +
        " -i \"" + file.getFullPathName() + "\"" +
        " -threads 8" +                                  // Use 8 threads for processing
        " -vf \"scale=" + juce::String(width) + ":" + juce::String(height) + "\"" + // Scale to target size
        " -f rawvideo -pix_fmt gray" +                   // Output format
        " -v error" +                                    // Only show errors
        " pipe:1";                                       // Output to stdout
    
    ffmpegProcess.start(cmd);
    
    // Read all frames into memory
    int framesRead = 0;
    
    while (framesRead < MAX_FRAMES) {
        size_t bytesRead = ffmpegProcess.read(frameBuffer.data(), frameBuffer.size());
        
        if (bytesRead != frameBuffer.size()) {
            break; // End of video or error
        }
        
        // Create a new frame
        frames.emplace_back(std::vector<uint8_t>(videoFrameSize));
        
        // Copy data to the current frame
        for (int i = 0; i < videoFrameSize; i++) {
            // value of 0 is reserved for transparent pixels
            frames.back()[i] = juce::jmax(1, (int)frameBuffer[i]);
        }
        
        framesRead++;
    }
    
    // Close the ffmpeg process
    ffmpegProcess.close();
    
    // Return true if we successfully loaded at least one frame
    return frames.size() > 0;
}
#endif

ImageParser::~ImageParser() {
#if OSCI_PREMIUM
    if (ffmpegProcess.isRunning()) {
        ffmpegProcess.close();
    }
#endif
}

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
    index = (frames.size() + (index % frames.size())) % frames.size();
    
    frameIndex = index;
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
    if (usingLiveImage) {
        if (liveImage.isValid()) {
            if (x < 0 || x >= width || y < 0 || y >= height) return 0;
            juce::Colour pixel = liveImage.getPixelAt(x, height - y - 1);
            float value = pixel.getBrightness();
            if (invert && value > 0) value = 1.0f - value;
            return value;
        }
        return 0;
    }
    
    int index = (height - y - 1) * width + x;
    if (index < 0 || frames.size() <= 0 || index >= frames[frameIndex].size()) {
        return 0;
    }
    float pixel = frames[frameIndex][index] / (float) std::numeric_limits<uint8_t>::max();
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

osci::Point ImageParser::getSample() {
    juce::SpinLock::ScopedLockType lock(liveImageLock);
    
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
        return osci::Point(2 * (currentX + widthDiff) / maxDim - 1, 2 * (currentY + heightDiff) / maxDim - 1);
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
                scanY -= scanIncrement;
            }
            if (scanY < -1) {
                double offset = ((scanCount % 15) / 15.0) * scanIncrement;
                scanY = 1 - offset;
                scanCount++;
            }
            
            maxIterations--;
        }
        
        return osci::Point(scanX, scanY);
    }
}
