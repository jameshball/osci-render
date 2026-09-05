#include "ImageParser.h"
#include "../../../modules/gifdec/gifdec.h"
#include "../../PluginProcessor.h"
#include "../../CommonPluginEditor.h"
#include "../../components/OverlayDialogHelpers.h"
#include "../../video/FFmpegMediaInfo.h"
#include "../FileFormatRegistry.h"

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
    else if (osci::files::isVideo(extension)) {
        processVideoFile(file);
    }
#endif
    else {
        processImageFile(file);
    }
    if (frames.empty()) {
        if (extension.equalsIgnoreCase(".gif")) {
            handleError("The image could not be loaded. Please try optimising the GIF with https://ezgif.com/optimize.");
        }
#if OSCI_PREMIUM
        else if (osci::files::isVideo(extension)) {
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

ImageParser::ImageParser(OscirenderAudioProcessor& p, int initialWidth, int initialHeight) : audioProcessor(p) {
    int safeWidth = juce::jmax(1, initialWidth);
    int safeHeight = juce::jmax(1, initialHeight);
    const int largestDimension = juce::jmax(safeWidth, safeHeight);
    if (largestDimension > liveInputMaxDimension) {
        const float scale = (float)liveInputMaxDimension / (float)largestDimension;
        safeWidth = juce::jmax(1, juce::roundToInt((float)safeWidth * scale));
        safeHeight = juce::jmax(1, juce::roundToInt((float)safeHeight * scale));
    }
    initialiseLiveFrame(safeWidth, safeHeight);
}

void ImageParser::initialiseLiveFrame(int initialWidth, int initialHeight) {
    if (initialWidth <= 0 || initialHeight <= 0) {
        return;
    }

    liveInput = true;
    width = initialWidth;
    height = initialHeight;
    frameIndex = 0;
    requestedFrameIndex.store(noPendingFrameRequest, std::memory_order_release);
    reportedFrameIndex.store(0, std::memory_order_release);

    frames.clear();
    frames.reserve(1);
    frames.emplace_back();
    frames[0].reserve(liveInputMaxPixels);
    frames[0].assign((size_t)width * (size_t)height, 0);

    visited.reserve(liveInputMaxPixels);
    visited.assign((size_t)width * (size_t)height, false);

    pendingLivePixels.reserve(liveInputMaxPixels);
    resetTraversalState();
}

void ImageParser::publishLiveFrame(std::vector<std::uint8_t> pixels, int frameWidth, int frameHeight) {
    if (!liveInput || frameWidth <= 0 || frameHeight <= 0 || pixels.size() != (size_t)frameWidth * (size_t)frameHeight) {
        return;
    }

    juce::SpinLock::ScopedLockType scope(pendingLiveFrameLock);
    pendingLivePixels = std::move(pixels);
    pendingLiveWidth = frameWidth;
    pendingLiveHeight = frameHeight;
    pendingLiveFrameAvailable = true;
}

void ImageParser::consumePendingLiveFrame() {
    if (!liveInput) {
        return;
    }

    size_t frameSize = 0;

    {
        juce::SpinLock::ScopedTryLockType scope(pendingLiveFrameLock);
        if (!scope.isLocked() || !pendingLiveFrameAvailable || frames.empty()) {
            return;
        }

        frameSize = (size_t)pendingLiveWidth * (size_t)pendingLiveHeight;
        if (pendingLiveWidth <= 0 || pendingLiveHeight <= 0 || pendingLivePixels.size() != frameSize || frameSize > liveInputMaxPixels) {
            pendingLiveFrameAvailable = false;
            return;
        }

        frames[0].swap(pendingLivePixels);
        width = pendingLiveWidth;
        height = pendingLiveHeight;
        pendingLiveFrameAvailable = false;
    }

    frameIndex = 0;
    requestedFrameIndex.store(noPendingFrameRequest, std::memory_order_release);
    reportedFrameIndex.store(0, std::memory_order_release);
    visited.resize(frameSize);
    resetTraversalState();
}

void ImageParser::setSingleFrameFromRgba(const std::vector<std::uint8_t>& rgba, int sourceWidth, int sourceHeight, bool verticallyFlipped) {
    if (sourceWidth <= 0 || sourceHeight <= 0) {
        return;
    }

    const size_t requiredBytes = (size_t)sourceWidth * (size_t)sourceHeight * 4;
    if (rgba.size() < requiredBytes) {
        return;
    }

    int targetWidth = sourceWidth;
    int targetHeight = sourceHeight;
    const int largestSourceDimension = juce::jmax(sourceWidth, sourceHeight);
    if (largestSourceDimension > liveInputMaxDimension) {
        const float scale = (float)liveInputMaxDimension / (float)largestSourceDimension;
        targetWidth = juce::jmax(1, juce::roundToInt((float)sourceWidth * scale));
        targetHeight = juce::jmax(1, juce::roundToInt((float)sourceHeight * scale));
    }

    std::vector<std::uint8_t> pixels((size_t)targetWidth * (size_t)targetHeight, 0);
    for (int y = 0; y < targetHeight; y++) {
        int sourceY = juce::jlimit(0, sourceHeight - 1, (int)(((int64_t)y * sourceHeight) / targetHeight));
        if (verticallyFlipped) {
            sourceY = sourceHeight - 1 - sourceY;
        }

        for (int x = 0; x < targetWidth; x++) {
            const int sourceX = juce::jlimit(0, sourceWidth - 1, (int)(((int64_t)x * sourceWidth) / targetWidth));
            const size_t sourceIndex = ((size_t)sourceY * (size_t)sourceWidth + (size_t)sourceX) * 4;
            const int r = rgba[sourceIndex];
            const int g = rgba[sourceIndex + 1];
            const int b = rgba[sourceIndex + 2];
            const int a = rgba[sourceIndex + 3];
            const int luma = (54 * r + 183 * g + 19 * b) >> 8;
            const std::uint8_t output = a == 0 ? 0 : (std::uint8_t)juce::jmax(1, luma);
            pixels[(size_t)y * (size_t)targetWidth + (size_t)x] = output;
        }
    }

    publishLiveFrame(std::move(pixels), targetWidth, targetHeight);
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
        int totalDelayHundredths = 0;
        while (gd_get_frame(gif) > 0) {
            gd_render_frame(gif, tempBuffer.data());
            totalDelayHundredths += gif->gce.delay;

            frames.emplace_back(std::vector<uint8_t>(frameSize));

            uint8_t *pixels = tempBuffer.data();
            for (int j = 0; j < tempBuffer.size(); j += 3) {
                uint8_t avg = (pixels[j] + pixels[j + 1] + pixels[j + 2]) / 3;
                // value of 0 is reserved for transparent pixels
                frames[i][j / 3] = juce::jmax(1, (int) avg);
            }

            i++;
        }

        if (i > 0 && totalDelayHundredths > 0) {
            frameRate = (double)i * 100.0 / (double)totalDelayHundredths;
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

        const juce::Image::BitmapData pixels(image, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                juce::Colour pixel = pixels.getPixelColour(x, y);
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
void ImageParser::processVideoFile(juce::File& file) {
    // assert on the message thread
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread()) {
        handleError("Could not process video file - not on the message thread.");
        return;
    }

    const auto ffmpegFile = audioProcessor.getFFmpegFile();
    if (!ffmpegFile.existsAsFile() || !loadAllVideoFrames(file, ffmpegFile)) {
        handleError("Could not read video frames. Please ensure FFmpeg is installed and the video file is valid.");
    }
}

bool ImageParser::loadAllVideoFrames(const juce::File& file, const juce::File& ffmpegFile) {
    const auto mediaInfo = osci::video::probeFFmpegMediaInfo(ffmpegFile, file);
    if (mediaInfo.width > 0 && mediaInfo.height > 0) {
        width = mediaInfo.width;
        height = mediaInfo.height;
    }
    if (mediaInfo.frameRate > 0.0) {
        frameRate = mediaInfo.frameRate;
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
   juce::String hwAccel = "videotoolbox";
#elif JUCE_WINDOWS
   // Try to use DXVA2 on Windows
   juce::String hwAccel = "dxva2";
#else
    juce::String hwAccel = "";
#endif

    // Start ffmpeg process to read frames using StringArray
    juce::StringArray frameReadCommand;
    frameReadCommand.add(ffmpegFile.getFullPathName());
    if (hwAccel.isNotEmpty())
    {
        frameReadCommand.add("-hwaccel");
        frameReadCommand.add(hwAccel);
    }
    frameReadCommand.add("-i");
    frameReadCommand.add(file.getFullPathName());
    frameReadCommand.add("-threads");
    frameReadCommand.add("8");
    frameReadCommand.add("-vf");
    frameReadCommand.add("scale=" + juce::String(width) + ":" + juce::String(height));
    frameReadCommand.add("-f");
    frameReadCommand.add("rawvideo");
    frameReadCommand.add("-pix_fmt");
    frameReadCommand.add("gray");
    frameReadCommand.add("-v");
    frameReadCommand.add("error");
    frameReadCommand.add("pipe:1");

    if (!ffmpegProcess.start(frameReadCommand))
    {
        handleError("Failed to start ffmpeg process for frame reading.");
        return false;
    }

    // Read all frames into memory
    int framesRead = 0;

    while (framesRead < MAX_FRAMES) {
        size_t bytesRead = ffmpegProcess.readProcessOutput(frameBuffer.data(), frameBuffer.size());

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
    ffmpegProcess.kill();

    // Return true if we successfully loaded at least one frame
    return frames.size() > 0;
}
#endif

ImageParser::~ImageParser() {
#if OSCI_PREMIUM
    if (ffmpegProcess.isRunning()) {
        ffmpegProcess.kill();
    }
#endif
}

void ImageParser::handleError(juce::String message) {
    juce::Component::SafePointer<CommonPluginEditor> editor(dynamic_cast<CommonPluginEditor*>(audioProcessor.getActiveEditor()));
    juce::MessageManager::callAsync([editor, message] {
        osci::showOverlayMessageOrAlert(editor.getComponent(),
            "Error",
            message,
            osci::ErrorOverlay::Icon::Warning,
            juce::MessageBoxIconType::WarningIcon,
            { 500, 260 });
    });

    width = 1;
    height = 1;
    frames.emplace_back(std::vector<uint8_t>(1));
    setFrame(0);
}

void ImageParser::setFrame(int index) {
    if (frames.empty()) {
        return;
    }

    const int normalisedIndex = normaliseFrameIndex(index);
    reportedFrameIndex.store(normalisedIndex, std::memory_order_release);
    requestedFrameIndex.store(normalisedIndex, std::memory_order_release);
}

int ImageParser::getNumFrames() {
    return (int)frames.size();
}

int ImageParser::getCurrentFrame() const {
    return reportedFrameIndex.load(std::memory_order_acquire);
}

int ImageParser::normaliseFrameIndex(int index) const {
    if (frames.empty()) {
        return 0;
    }

    const int frameCount = static_cast<int>(frames.size());
    return (frameCount + (index % frameCount)) % frameCount;
}

void ImageParser::applyPendingFrameRequest() {
    const int requestedIndex = requestedFrameIndex.exchange(noPendingFrameRequest, std::memory_order_acq_rel);
    if (requestedIndex == noPendingFrameRequest || frames.empty()) {
        return;
    }

    const int normalisedIndex = normaliseFrameIndex(requestedIndex);
    frameIndex = normalisedIndex;
    reportedFrameIndex.store(normalisedIndex, std::memory_order_release);
    resetTraversalState();
}

void ImageParser::resetTraversalState() {
    count = 0;
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

osci::Point ImageParser::getSample(int blockSampleIndex) {
    consumePendingLiveFrame();
    applyPendingFrameRequest();

    if (frames.empty() || width <= 0 || height <= 0 || frameIndex < 0 || frameIndex >= frames.size()) {
        return osci::Point();
    }

    const int jumpInterval = juce::jmax(1, jumpFrequency());
    const int resetInterval = 10 * jumpInterval;
    if (count % jumpInterval == 0) {
        resetPosition();
    }
    if (count == 0) {
        std::fill(visited.begin(), visited.end(), false);
    }

    float thresholdPow = audioProcessor.imageThreshold->getAnimatedValue(0, static_cast<size_t>(blockSampleIndex)) * 10 + 1;
    findNearestNeighbour(10, thresholdPow, audioProcessor.imageStride->getAnimatedValue(0, static_cast<size_t>(blockSampleIndex)), audioProcessor.invertImage->getValue());
    float maxDim = juce::jmax(width, height);
    count = (count + 1) % resetInterval;
    float widthDiff = (maxDim - width) / 2;
    float heightDiff = (maxDim - height) / 2;
    return osci::Point(2 * (currentX + widthDiff) / maxDim - 1, 2 * (currentY + heightDiff) / maxDim - 1);
}
