#pragma once
#include <JuceHeader.h>
#include <atomic>
#include <deque>
#include <functional>

// Forward-declare thorvg types so this header does not leak thorvg's globals.
namespace tvg {
    class Animation;
}

// NOTE: do NOT name this `LottieParser` — thorvg itself declares a class named
// `LottieParser` in the global namespace (see `modules/thorvg/src/loaders/lottie/
// tvgLottieParser.h`). Having two classes with identical names in the global
// namespace causes an ODR violation: the linker keeps one vtable/dtor and uses
// it for both, which led to a crash when thorvg's internal `LottieParser`
// stack-object was destroyed through our destructor.
class OsciLottieParser {
public:
    // jsonContent may be Lottie JSON or raw JSON from a .lottie archive.
    explicit OsciLottieParser(juce::String jsonContent, std::function<void(juce::String)> errorCallback = {});
    ~OsciLottieParser();

    std::vector<std::unique_ptr<osci::Shape>> draw();

    int getNumFrames() const;
    int getCurrentFrame() const;
    void setFrame(int index);
    double getFrameRate() const { return frameRate; }

private:
    void extractShapesAtCurrentFrame(std::vector<std::unique_ptr<osci::Shape>>& out);
    void showError(juce::String message);
    void fallbackShapes();

    struct CachedFrame {
        int index = 0;
        std::vector<std::unique_ptr<osci::Shape>> shapes;
    };

    const std::vector<std::unique_ptr<osci::Shape>>& getOrCreateCachedFrame(int index);

    // Retained JSON buffer (thorvg loads with copy=false so we must keep it alive).
    juce::MemoryBlock jsonBuffer;

    std::unique_ptr<tvg::Animation> animation;

    // Shape extraction runs on the FrameProducer thread. Keep a small bounded
    // cache so long or complex animations cannot retain every flattened frame.
    static constexpr size_t maxCachedFrames = 8;
    std::deque<CachedFrame> framesCache;

    std::atomic<int> totalFrames{1};
    std::atomic<int> currentFrame{-1}; // force rebuild on first setFrame
    float pictureWidth = 0.0f;
    float pictureHeight = 0.0f;
    double frameRate = 30.0;
    std::function<void(juce::String)> errorCallback;
};
