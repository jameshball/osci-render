#pragma once
#include <JuceHeader.h>

#include "../svg/SvgParser.h"

class OscirenderAudioProcessor;
class CommonPluginEditor;

class ImageParser {
public:
    ImageParser(OscirenderAudioProcessor& p, juce::String fileName, juce::MemoryBlock image);
    // Constructor for live Syphon/Spout input
    ImageParser(OscirenderAudioProcessor& p);
    ~ImageParser();

    // Update the live frame (for Syphon/Spout)
    void updateLiveFrame(const juce::Image& newImage);

    void setFrame(int index);
    osci::Point getSample();
    int getNumFrames() { return frames.size(); }
    int getCurrentFrame() const { return frameIndex; }

private:
    void findNearestNeighbour(int searchRadius, float thresholdPow, int stride, bool invert);
    void resetPosition();
    float getPixelValue(int x, int y, bool invert);
    int getPixelIndex(int x, int y);
    void findWhite(double thresholdPow, bool invert);
    bool isOverThreshold(double pixel, double thresholdValue);
    int jumpFrequency();
    void handleError(juce::String message);
    void processGifFile(juce::File& file);
    void processImageFile(juce::File& file);
#if OSCI_PREMIUM
    void processVideoFile(juce::File& file);
    bool loadAllVideoFrames(const juce::File& file, const juce::File& ffmpegFile);
    bool isVideoFile(const juce::String& extension) const;
#endif

    const juce::String ALGORITHM = "HILLIGOSS";

    OscirenderAudioProcessor& audioProcessor;
    juce::Random rng;
    int frameIndex = 0;
    std::vector<std::vector<uint8_t>> frames;
    std::vector<bool> visited;
    int currentX, currentY;
    int width = -1;
    int height = -1;
    int count = 0;

    juce::TemporaryFile temp;

#if OSCI_PREMIUM
    // Video processing fields
    juce::ChildProcess ffmpegProcess;
    bool isVideo = false;
    std::vector<uint8_t> frameBuffer;
    int videoFrameSize = 0;
#endif

    // experiments
    double scanX = -1;
    double scanY = 1;
    int scanCount = 0;

    // Live image support
    juce::SpinLock liveImageLock;
    bool usingLiveImage = false;
    juce::Image liveImage;
};
