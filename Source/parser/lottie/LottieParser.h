#pragma once
#include <JuceHeader.h>

// Forward-declare thorvg types so this header does not leak thorvg's globals.
namespace tvg {
    struct Animation;
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
    OsciLottieParser(juce::String jsonContent);
    ~OsciLottieParser();

    std::vector<std::unique_ptr<osci::Shape>> draw();

    int getNumFrames() const;
    int getCurrentFrame() const;
    void setFrame(int index);

private:
    void extractShapesAtCurrentFrame();
    void showError(juce::String message);
    void fallbackShapes();

    // Retained JSON buffer (thorvg loads with copy=false so we must keep it alive).
    juce::MemoryBlock jsonBuffer;

    // Custom deleter keeps thorvg types out of this header.
    struct AnimationDeleter { void operator()(tvg::Animation* a) const noexcept; };
    std::unique_ptr<tvg::Animation, AnimationDeleter> animation;

    std::vector<std::unique_ptr<osci::Shape>> currentShapes;

    int totalFrames = 1;
    int currentFrame = -1; // force rebuild on first setFrame
    float pictureWidth = 0.0f;
    float pictureHeight = 0.0f;
};
