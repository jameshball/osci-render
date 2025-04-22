#pragma once
#include "../shape/OsciPoint.h"
#include <JuceHeader.h>
#include "../shape/Shape.h"
#include "../svg/SvgParser.h"
#include "../shape/Line.h"
#include "../concurrency/ReadProcess.h"

class OscirenderAudioProcessor;
class CommonPluginEditor;

class ImageParser {
public:
	ImageParser(OscirenderAudioProcessor& p, juce::String fileName, juce::MemoryBlock image);
	~ImageParser();

	void setFrame(int index);
	OsciPoint getSample();
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
    void processVideoFile(juce::File& file);
    bool loadAllVideoFrames(const juce::File& file, const juce::File& ffmpegFile);
    bool isVideoFile(const juce::String& extension) const;
    
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
    
    // Video processing fields
    ReadProcess ffmpegProcess;
    bool isVideo = false;
    juce::TemporaryFile temp;
    std::vector<uint8_t> frameBuffer;
    int videoFrameSize = 0;
    
    // experiments
    double scanX = -1;
    double scanY = 1;
    int scanCount = 0;
};
