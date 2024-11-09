#pragma once

#include <algorithm>
#include <JuceHeader.h>
#include "../LookAndFeel.h"
#include "../concurrency/AudioBackgroundThread.h"
#include "../components/SvgButton.h"
#include "VisualiserSettings.h"
#include "../components/StopwatchComponent.h"

enum class FullScreenMode {
    TOGGLE,
    FULL_SCREEN,
    MAIN_COMPONENT,
};

struct Texture {
    GLuint id;
    int width;
    int height;
};

class VisualiserWindow;
class VisualiserComponent : public juce::Component, public AudioBackgroundThread, public juce::MouseListener, public juce::OpenGLRenderer, public juce::AsyncUpdater {
public:
    VisualiserComponent(AudioBackgroundThreadManager& threadManager, VisualiserSettings& settings, VisualiserComponent* parent = nullptr, bool visualiserOnly = false);
    ~VisualiserComponent() override;

    std::function<void()> openSettings;
    std::function<void()> closeSettings;

    void enableFullScreen();
    void setFullScreenCallback(std::function<void(FullScreenMode)> callback);
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void setBuffer(const std::vector<OsciPoint>& buffer);
    void resized() override;
    void paint(juce::Graphics& g) override;
    int prepareTask(double sampleRate, int samplesPerBlock) override;
    void runTask(const std::vector<OsciPoint>& points) override;
    void setPaused(bool paused);
    void mouseDown(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void handleAsyncUpdate() override;
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void setFullScreen(bool fullScreen);
    void toggleRecording();
    void haltRecording();
    void childUpdated();

    VisualiserComponent* parent = nullptr;
    VisualiserComponent* child = nullptr;
    std::unique_ptr<VisualiserWindow> popout = nullptr;
    
    std::atomic<bool> active = true;
    
    std::function<void()> recordingHalted;

private:
    const double FRAME_RATE = 60.0;
    
    bool visualiserOnly;
    
    AudioBackgroundThreadManager& threadManager;
    
    SvgButton fullScreenButton{ "fullScreen", BinaryData::fullscreen_svg, juce::Colours::white, juce::Colours::white };
    SvgButton popOutButton{ "popOut", BinaryData::open_in_new_svg, juce::Colours::white, juce::Colours::white };
    SvgButton settingsButton{ "settings", BinaryData::cog_svg, juce::Colours::white, juce::Colours::white };

    std::function<void(FullScreenMode)> fullScreenCallback;

    VisualiserSettings& settings;
    
    std::unique_ptr<juce::FileChooser> chooser;
    juce::File tempVideoFile;
    
    StopwatchComponent stopwatch;
    SvgButton record{"Record", BinaryData::record_svg, juce::Colours::red, juce::Colours::red.withAlpha(0.01f)};
    
    void popoutWindow();
    
    // OPENGL
    
    juce::OpenGLContext openGLContext;
    
    juce::Rectangle<int> buttonRow;
    juce::Rectangle<int> viewportArea;
    
    float renderScale = 1.0f;
    
    GLuint quadIndexBuffer = 0;
    GLuint vertexIndexBuffer = 0;
    GLuint vertexBuffer = 0;

    int nPoints = 0;
    int nEdges = 0;

    juce::CriticalSection samplesLock;
    bool needsReattach = true;
    std::vector<float> xSamples{2};
    std::vector<float> ySamples;
    std::vector<float> zSamples;
    std::vector<float> smoothedXSamples;
    std::vector<float> smoothedYSamples;
    std::vector<float> smoothedZSamples;
    
    std::vector<float> scratchVertices;
    std::vector<float> fullScreenQuad;
    
    GLuint frameBuffer = 0;
    Texture lineTexture;
    Texture blur1Texture;
    Texture blur2Texture;
    Texture blur3Texture;
    Texture blur4Texture;
    juce::OpenGLTexture screenOpenGLTexture;
    juce::Image screenTextureImage = juce::ImageFileFormat::loadFrom(BinaryData::noise_jpg, BinaryData::noise_jpgSize);
    juce::Image emptyScreenImage = juce::ImageFileFormat::loadFrom(BinaryData::empty_jpg, BinaryData::empty_jpgSize);
    Texture screenTexture;
    std::optional<Texture> targetTexture = std::nullopt;
    
    std::unique_ptr<juce::OpenGLShaderProgram> simpleShader;
    std::unique_ptr<juce::OpenGLShaderProgram> texturedShader;
    std::unique_ptr<juce::OpenGLShaderProgram> blurShader;
    std::unique_ptr<juce::OpenGLShaderProgram> lineShader;
    std::unique_ptr<juce::OpenGLShaderProgram> outputShader;
    juce::OpenGLShaderProgram* currentShader;
    
    float fadeAmount;
    bool smudgesEnabled = settings.getSmudgesEnabled();
    bool graticuleEnabled = settings.getGraticuleEnabled();
    
    const double RESAMPLE_RATIO = 6.0;
    double sampleRate = -1;
    chowdsp::ResamplingTypes::LanczosResampler<2048, 8> xResampler;
    chowdsp::ResamplingTypes::LanczosResampler<2048, 8> yResampler;
    chowdsp::ResamplingTypes::LanczosResampler<2048, 8> zResampler;
    
    Texture makeTexture(int width, int height);
    void setupArrays(int num_points);
    void setupTextures();
    void drawLineTexture(const std::vector<float>& xPoints, const std::vector<float>& yPoints, const std::vector<float>& zPoints);
    void saveTextureToFile(GLuint textureID, int width, int height, const juce::File& file);
    void activateTargetTexture(std::optional<Texture> texture);
    void setShader(juce::OpenGLShaderProgram* program);
    void drawTexture(std::optional<Texture> texture0, std::optional<Texture> texture1 = std::nullopt, std::optional<Texture> texture2 = std::nullopt, std::optional<Texture> texture3 = std::nullopt);
    void setAdditiveBlending();
    void setNormalBlending();
    void drawLine(const std::vector<float>& xPoints, const std::vector<float>& yPoints, const std::vector<float>& zPoints);
    void fade();
    void drawCRT();
    void checkGLErrors(const juce::String& location);
    void viewportChanged(juce::Rectangle<int> area);
    Texture createScreenTexture();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualiserComponent)
};

class VisualiserWindow : public juce::DocumentWindow {
public:
    VisualiserWindow(juce::String name, VisualiserComponent* parent) : parent(parent), wasPaused(!parent->active), juce::DocumentWindow(name, juce::Colours::black, juce::DocumentWindow::TitleBarButtons::allButtons) {}
    
    void closeButtonPressed() override {
        parent->setPaused(wasPaused);
        parent->child = nullptr;
        parent->childUpdated();
        parent->resized();
        parent->popout.reset();
    }

private:
    VisualiserComponent* parent;
    bool wasPaused;
};
