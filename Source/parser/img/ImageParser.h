#pragma once
#include <JuceHeader.h>

#include <osci_file_import/osci_file_import.h>

#include <cstdint>

class OscirenderAudioProcessor;
class CommonPluginEditor;

class ImageParser {
public:
    ImageParser(OscirenderAudioProcessor& p, juce::String extension, juce::MemoryBlock image);
    ImageParser(OscirenderAudioProcessor& p, int initialWidth, int initialHeight);
    ~ImageParser();

    void setFrame(int index);
    void setSingleFrameFromRgba(const std::vector<std::uint8_t>& rgba, int sourceWidth, int sourceHeight, bool verticallyFlipped);
    osci::Point getSample(int blockSampleIndex = 0);
    int getNumFrames();
    int getCurrentFrame() const;

private:
    static constexpr int liveInputMaxDimension = 512;

    void replaceSingleFrame(std::vector<std::uint8_t> pixels, int frameWidth, int frameHeight);
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
    juce::SpinLock frameLock;
    juce::Random rng;
    int frameIndex = 0;
    std::vector<std::vector<uint8_t>> frames;
    std::vector<bool> visited;
    int currentX, currentY;
    int width = -1;
    int height = -1;
    int count = 0;

    juce::TemporaryFile temp{".temp"};

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

};
