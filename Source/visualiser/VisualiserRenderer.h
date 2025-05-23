#pragma once

#include <JuceHeader.h>

#include <algorithm>

#include "VisualiserParameters.h"

struct Texture {
    GLuint id;
    int width;
    int height;
};

class VisualiserWindow;
class VisualiserRenderer : public juce::Component, public osci::AudioBackgroundThread, public juce::OpenGLRenderer, public juce::AsyncUpdater {
public:
    VisualiserRenderer(
        VisualiserParameters &parameters,
        osci::AudioBackgroundThreadManager &threadManager,
        int resolution = 1024,
        double frameRate = 60.0f,
        juce::String threadName = ""
    );
    ~VisualiserRenderer() override;

    void resized() override;
    int prepareTask(double sampleRate, int samplesPerBlock) override;
    void runTask(const std::vector<osci::Point>& points) override;
    void stopTask() override;
    void handleAsyncUpdate() override;
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void setResolution(int width);
    void setFrameRate(double frameRate);

    int getRenderWidth() const { return renderTexture.width; }
    int getRenderHeight() const { return renderTexture.height; }
    Texture getRenderTexture() const { return renderTexture; }

    void getFrame(std::vector<unsigned char>& frame);
    void drawFrame();    juce::Rectangle<int> getViewportArea() const { return viewportArea; }
    void setViewportArea(juce::Rectangle<int> area) {
        viewportArea = area;
        viewportChanged(viewportArea);
    }
    
    // Set a crop rectangle for the renderTexture when drawing to screen
    // If not set, the entire texture will be displayed in a square
    void setCropRectangle(std::optional<juce::Rectangle<float>> cropRect) {
        cropRectangle = cropRect;
        viewportChanged(viewportArea);
    }
    
    std::optional<juce::Rectangle<float>> getCropRectangle() const { return cropRectangle; }

protected:
    juce::OpenGLContext openGLContext;
    VisualiserParameters &parameters;

    osci::Semaphore renderingSemaphore{0};

    std::function<void()> preRenderCallback = nullptr;
    std::function<void()> postRenderCallback = nullptr;

    juce::AudioBuffer<float> audioOutputBuffer;
private:    juce::Rectangle<int> viewportArea;
    std::optional<juce::Rectangle<float>> cropRectangle;

    float renderScale = 1.0f;

    GLuint quadIndexBuffer = 0;
    GLuint vertexIndexBuffer = 0;
    GLuint vertexBuffer = 0;

    int nEdges = 0;

    juce::CriticalSection samplesLock;
    long sampleCount = 0;
    std::vector<float> xSamples{2};
    std::vector<float> ySamples{2};
    std::vector<float> zSamples{2};
    std::vector<float> smoothedXSamples;
    std::vector<float> smoothedYSamples;
    std::vector<float> smoothedZSamples;
    std::atomic<int> sampleBufferCount = 0;
    int prevSampleBufferCount = 0;
    long lastTriggerPosition = 0;

    std::vector<float> scratchVertices;
    std::vector<float> fullScreenQuad;

    GLuint frameBuffer = 0;

    double currentFrameRate = 60.0;
    Texture lineTexture;
    Texture blur1Texture;
    Texture blur2Texture;
    Texture blur3Texture;
    Texture blur4Texture;
    Texture glowTexture;
    Texture renderTexture;
    Texture screenTexture;
    juce::OpenGLTexture screenOpenGLTexture;
    std::optional<Texture> targetTexture = std::nullopt;

    juce::Image screenTextureImage = juce::ImageFileFormat::loadFrom(BinaryData::noise_jpg, BinaryData::noise_jpgSize);
    juce::Image emptyScreenImage = juce::ImageFileFormat::loadFrom(BinaryData::empty_jpg, BinaryData::empty_jpgSize);

#if OSCI_PREMIUM
    juce::Image oscilloscopeImage = juce::ImageFileFormat::loadFrom(BinaryData::real_png, BinaryData::real_pngSize);
    juce::Image vectorDisplayImage = juce::ImageFileFormat::loadFrom(BinaryData::vector_display_png, BinaryData::vector_display_pngSize);

    juce::Image emptyReflectionImage = juce::ImageFileFormat::loadFrom(BinaryData::no_reflection_jpg, BinaryData::no_reflection_jpgSize);
    juce::Image oscilloscopeReflectionImage = juce::ImageFileFormat::loadFrom(BinaryData::real_reflection_png, BinaryData::real_reflection_pngSize);
    juce::Image vectorDisplayReflectionImage = juce::ImageFileFormat::loadFrom(BinaryData::vector_display_reflection_png, BinaryData::vector_display_reflection_pngSize);

    osci::Point REAL_SCREEN_OFFSET = {0.02, -0.15};
    osci::Point REAL_SCREEN_SCALE = {0.6};

    osci::Point VECTOR_DISPLAY_OFFSET = {0.075, -0.045};
    osci::Point VECTOR_DISPLAY_SCALE = {0.6};
    float VECTOR_DISPLAY_FISH_EYE = 0.5;

    juce::OpenGLTexture reflectionOpenGLTexture;
    Texture reflectionTexture;

    std::unique_ptr<juce::OpenGLShaderProgram> glowShader;
    std::unique_ptr<juce::OpenGLShaderProgram> afterglowShader;
#endif

    std::unique_ptr<juce::OpenGLShaderProgram> simpleShader;
    std::unique_ptr<juce::OpenGLShaderProgram> texturedShader;
    std::unique_ptr<juce::OpenGLShaderProgram> blurShader;
    std::unique_ptr<juce::OpenGLShaderProgram> wideBlurShader;
    std::unique_ptr<juce::OpenGLShaderProgram> lineShader;
    std::unique_ptr<juce::OpenGLShaderProgram> outputShader;
    juce::OpenGLShaderProgram* currentShader;

    float fadeAmount;
    ScreenOverlay screenOverlay = ScreenOverlay::INVALID;

    int resolution;
    double frameRate;

    const double RESAMPLE_RATIO = 6.0;
    double sampleRate = -1;
    double oldSampleRate = -1;
    chowdsp::ResamplingTypes::LanczosResampler<2048, 8> xResampler;
    chowdsp::ResamplingTypes::LanczosResampler<2048, 8> yResampler;
    chowdsp::ResamplingTypes::LanczosResampler<2048, 8> zResampler;

    void setOffsetAndScale(juce::OpenGLShaderProgram* shader);
    Texture makeTexture(int width, int height, GLuint textureID = 0);
    void setupArrays(int num_points);
    void setupTextures(int resolution);
    void drawLineTexture(const std::vector<float>& xPoints, const std::vector<float>& yPoints, const std::vector<float>& zPoints);
    void saveTextureToPNG(Texture texture, const juce::File& file);
    void activateTargetTexture(std::optional<Texture> texture);
    void setShader(juce::OpenGLShaderProgram* program);
    void drawTexture(std::vector<std::optional<Texture>> textures);
    void setAdditiveBlending();
    void setNormalBlending();
    void drawLine(const std::vector<float>& xPoints, const std::vector<float>& yPoints, const std::vector<float>& zPoints);
    void fade();
    void drawCRT();
    void checkGLErrors(juce::String file, int line);
    void viewportChanged(juce::Rectangle<int> area);

    void renderScope(const std::vector<float>& xPoints, const std::vector<float>& yPoints, const std::vector<float>& zPoints);

    double getSweepIncrement();

    Texture createScreenTexture();
    Texture createReflectionTexture();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualiserRenderer)
    JUCE_DECLARE_WEAK_REFERENCEABLE(VisualiserRenderer)
};
