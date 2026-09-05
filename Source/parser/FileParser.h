#pragma once

#include <JuceHeader.h>
#include "img/ImageParser.h"
#include <osci_file_import/osci_file_import.h>
#include <osci_scripting/osci_scripting.h>
#if OSCI_PREMIUM
#include "lottie/LottieParser.h"
#endif

class OscirenderAudioProcessor;
class FileParser : public FrameSource, public std::enable_shared_from_this<FileParser> {
public:
	FileParser(OscirenderAudioProcessor &p, std::function<void(int, juce::String, juce::String)> errorCallback = nullptr);

	void parse(juce::String fileId, juce::String fileName, juce::String extension, std::unique_ptr<juce::InputStream> stream, juce::Font font);
	void prepareLiveImageInput(int width, int height);
	void updateLiveImageFrame(const std::vector<std::uint8_t>& rgba, int width, int height, bool verticallyFlipped);
	std::vector<std::unique_ptr<osci::Shape>> nextFrame() override;
	osci::Point nextSample(LuaState& L, LuaVariables& vars);

	bool isSample() override;
	bool isActive() override;
	void disable() override;
	void enable() override;
	bool consumeDirty() override;
    
    int getNumFrames();
    int getCurrentFrame();
    void setFrame(int frame);
    double getFrameRate() const;
	
	std::shared_ptr<WorldObject> getObject();
	std::shared_ptr<SvgParser> getSvg();
	std::shared_ptr<TextParser> getText();
	std::shared_ptr<LineArtParser> getLineArt();
	std::shared_ptr<LuaParser> getLua();
	std::shared_ptr<ImageParser> getImg();
	std::shared_ptr<WavParser> getWav();
#if OSCI_PREMIUM
	std::shared_ptr<FractalParser> getFractal();
	std::shared_ptr<OsciLottieParser> getLottie();
#endif

	bool isAnimatable = false;

private:
	void clearLoadedSource();
	void showFileSizeWarning(juce::String fileName, int64_t totalBytes, int64_t mbLimit,
		juce::String fileType, std::function<void()> callback);

	OscirenderAudioProcessor& audioProcessor;

	bool active = true;
	bool sampleSource = false;
	std::atomic<double> frameRate{30.0};
	std::atomic<uint64_t> sourceGeneration{0};
	juce::SpinLock lock;

	std::shared_ptr<WorldObject> object;
	std::shared_ptr<SvgParser> svg;
	std::shared_ptr<TextParser> text;
	std::shared_ptr<LineArtParser> gpla;
	std::shared_ptr<LuaParser> lua;
	std::shared_ptr<ImageParser> img;
	std::shared_ptr<WavParser> wav;
#if OSCI_PREMIUM
	std::shared_ptr<FractalParser> fractal;
	std::shared_ptr<OsciLottieParser> lottie;
#endif

	juce::String fallbackLuaScript = "return { 0.0, 0.0 }";

	std::function<void(int, juce::String, juce::String)> errorCallback;

	juce::AudioBuffer<float> wavPointBuffer{3, 1};
};
