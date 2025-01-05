#pragma once

#include <algorithm>
#include <JuceHeader.h>
#include "../LookAndFeel.h"
#include "../concurrency/AudioBackgroundThread.h"
#include "../components/SvgButton.h"
#include "VisualiserSettings.h"
#include "RecordingSettings.h"
#include "../components/StopwatchComponent.h"
#include "../img/qoixx.hpp"
#include "../components/DownloaderComponent.h"
#include "../concurrency/WriteProcess.h"
#include "../audio/AudioRecorder.h"

#define FILE_RENDER_DUMMY 0
#define FILE_RENDER_PNG 1
#define FILE_RENDER_QOI 2

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
    VisualiserComponent(
        juce::File& lastOpenedDirectory,
#if SOSCI_FEATURES
        SharedTextureManager& sharedTextureManager,
#endif
        juce::File ffmpegFile,
        std::function<void()>& haltRecording,
        AudioBackgroundThreadManager& threadManager,
        VisualiserSettings& settings,
        RecordingParameters& recordingParameters,
        VisualiserComponent* parent = nullptr,
        bool visualiserOnly = false
    );
    ~VisualiserComponent() override;

    std::function<void()> openSettings;
    std::function<void()> closeSettings;

    void enableFullScreen();
    void setFullScreenCallback(std::function<void(FullScreenMode)> callback);
    void mouseDoubleClick(const juce::MouseEvent& event) override;
    void resized() override;
    void paint(juce::Graphics& g) override;
    int prepareTask(double sampleRate, int samplesPerBlock) override;
    void runTask(const std::vector<OsciPoint>& points) override;
    void stopTask() override;
    void setPaused(bool paused);
    void mouseDown(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;
    void handleAsyncUpdate() override;
    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void setFullScreen(bool fullScreen);
    void setRecording(bool recording);
    void childUpdated();

    VisualiserComponent* parent = nullptr;
    VisualiserComponent* child = nullptr;
    std::unique_ptr<VisualiserWindow> popout = nullptr;
    
    std::atomic<bool> active = true;

private:
    float intensity;
    const double FRAME_RATE = 60.0;
    
    bool visualiserOnly;
    std::function<void()>& haltRecording;
    
    AudioBackgroundThreadManager& threadManager;
    
    SvgButton fullScreenButton{ "fullScreen", BinaryData::fullscreen_svg, juce::Colours::white, juce::Colours::white };
    SvgButton popOutButton{ "popOut", BinaryData::open_in_new_svg, juce::Colours::white, juce::Colours::white };
    SvgButton settingsButton{ "settings", BinaryData::cog_svg, juce::Colours::white, juce::Colours::white };
    
#if SOSCI_FEATURES
    SvgButton sharedTextureButton{ "sharedTexture", BinaryData::spout_svg, juce::Colours::white, juce::Colours::red };
    SharedTextureManager& sharedTextureManager;
    SharedTextureSender* sharedTextureSender = nullptr;
#endif

    std::function<void(FullScreenMode)> fullScreenCallback;

    VisualiserSettings& settings;
    RecordingParameters& recordingParameters;
    juce::File ffmpegFile;
    bool recordingAudio = true;
    
#if SOSCI_FEATURES
    bool recordingVideo = true;
    
    long numFrames = 0;
    std::vector<unsigned char> framePixels;
    WriteProcess ffmpegProcess;
    std::unique_ptr<juce::TemporaryFile> tempVideoFile;
    
    juce::String ffmpegURL = juce::String("https://github.com/eugeneware/ffmpeg-static/releases/download/b6.0/") +
#if JUCE_WINDOWS
    #if JUCE_64BIT
        "ffmpeg-win32-x64"
    #elif JUCE_32BIT
        "ffmpeg-win32-ia32"
    #endif
#elif JUCE_MAC
    #if JUCE_ARM
        "ffmpeg-darwin-arm64"
    #elif JUCE_INTEL
        "ffmpeg-darwin-x64"
    #endif
#elif JUCE_LINUX
    #if JUCE_ARM
        #if JUCE_64BIT
            "ffmpeg-linux-arm64"
        #elif JUCE_32BIT
            "ffmpeg-linux-arm"
        #endif
    #elif JUCE_INTEL
        #if JUCE_64BIT
            "ffmpeg-linux-x64"
        #elif JUCE_32BIT
            "ffmpeg-linux-ia32"
        #endif
    #endif
#endif
    + ".gz";
    DownloaderComponent ffmpegDownloader{ffmpegURL, ffmpegFile};
#endif
    
    StopwatchComponent stopwatch;
    SvgButton record{"Record", BinaryData::record_svg, juce::Colours::red, juce::Colours::red.withAlpha(0.01f)};
    
    juce::File& lastOpenedDirectory;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::TemporaryFile> tempAudioFile;
    AudioRecorder audioRecorder;
    
    Semaphore renderingSemaphore{0};
    
    void popoutWindow();
    
    // OPENGL
    
    juce::OpenGLContext openGLContext;
    
    float time = 0;
    
    juce::Rectangle<int> buttonRow;
    juce::Rectangle<int> viewportArea;
    
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
    int sampleBufferCount = 0;
    int prevSampleBufferCount = 0;
    long lastTriggerPosition = 0;
    
    std::vector<float> scratchVertices;
    std::vector<float> fullScreenQuad;
    
    GLuint frameBuffer = 0;
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
    
#if SOSCI_FEATURES
    juce::Image oscilloscopeImage = juce::ImageFileFormat::loadFrom(BinaryData::real_jpg, BinaryData::real_jpgSize);
    juce::Image vectorDisplayImage = juce::ImageFileFormat::loadFrom(BinaryData::vector_display_jpg, BinaryData::vector_display_jpgSize);
    
    juce::Image emptyReflectionImage = juce::ImageFileFormat::loadFrom(BinaryData::no_reflection_jpg, BinaryData::no_reflection_jpgSize);
    juce::Image oscilloscopeReflectionImage = juce::ImageFileFormat::loadFrom(BinaryData::real_reflection_jpg, BinaryData::real_reflection_jpgSize);
    juce::Image vectorDisplayReflectionImage = juce::ImageFileFormat::loadFrom(BinaryData::vector_display_reflection_jpg, BinaryData::vector_display_reflection_jpgSize);
    
    OsciPoint REAL_SCREEN_OFFSET = { 0.02, -0.15 };
    OsciPoint REAL_SCREEN_SCALE = { 0.6 };
    
    OsciPoint VECTOR_DISPLAY_OFFSET = { 0.075, -0.045 };
    OsciPoint VECTOR_DISPLAY_SCALE = { 0.6 };
    float VECTOR_DISPLAY_FISH_EYE = 0.5;
    
    juce::OpenGLTexture reflectionOpenGLTexture;
    Texture reflectionTexture;
    
    std::unique_ptr<juce::OpenGLShaderProgram> glowShader;
#endif
    
    std::unique_ptr<juce::OpenGLShaderProgram> simpleShader;
    std::unique_ptr<juce::OpenGLShaderProgram> texturedShader;
    std::unique_ptr<juce::OpenGLShaderProgram> blurShader;
    std::unique_ptr<juce::OpenGLShaderProgram> wideBlurShader;
    std::unique_ptr<juce::OpenGLShaderProgram> lineShader;
    std::unique_ptr<juce::OpenGLShaderProgram> outputShader;
    juce::OpenGLShaderProgram* currentShader;
    
    float fadeAmount;
    ScreenType screenType = settings.getScreenType();
    
    const double RESAMPLE_RATIO = 6.0;
    double sampleRate = -1;
    double oldSampleRate = -1;
    chowdsp::ResamplingTypes::LanczosResampler<2048, 8> xResampler;
    chowdsp::ResamplingTypes::LanczosResampler<2048, 8> yResampler;
    chowdsp::ResamplingTypes::LanczosResampler<2048, 8> zResampler;

    void setOffsetAndScale(juce::OpenGLShaderProgram* shader);
#if SOSCI_FEATURES
    void initialiseSharedTexture();
    void closeSharedTexture();
#endif
    Texture makeTexture(int width, int height);
    void setupArrays(int num_points);
    void setupTextures();
    void drawLineTexture(const std::vector<float>& xPoints, const std::vector<float>& yPoints, const std::vector<float>& zPoints);
    void saveTextureToPNG(Texture texture, const juce::File& file);
    void saveTextureToQOI(Texture texture, const juce::File& file);
    void activateTargetTexture(std::optional<Texture> texture);
    void setShader(juce::OpenGLShaderProgram* program);
    void drawTexture(std::vector<std::optional<Texture>> textures);
    void setAdditiveBlending();
    void setNormalBlending();
    void drawLine(const std::vector<float>& xPoints, const std::vector<float>& yPoints, const std::vector<float>& zPoints);
    void fade();
    void drawCRT();
    void checkGLErrors(const juce::String& location);
    void viewportChanged(juce::Rectangle<int> area);

    void renderScope(const std::vector<float>& xPoints, const std::vector<float>& yPoints, const std::vector<float>& zPoints);
    int renderAudioFile(juce::File& sourceAudio, int method = 1, int width = 1024, int height = 1024);
    
    double getSweepIncrement();

    Texture createScreenTexture();
    Texture createReflectionTexture();

    juce::File audioFile;

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
