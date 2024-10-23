#pragma once

#include <JuceHeader.h>
#include "VisualiserSettings.h"
#include "../audio/SampleRateManager.h"

struct Texture {
    GLuint id;
    int width;
    int height;
};

class VisualiserOpenGLComponent : public juce::Component, public juce::OpenGLRenderer {
public:
    VisualiserOpenGLComponent(VisualiserSettings& settings, SampleRateManager& sampleRateManager);
    ~VisualiserOpenGLComponent() override;

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void resized() override;
    void paint(juce::Graphics& g) override;
    void updateBuffer(std::vector<Point>& buffer);
    void setPaused(bool paused);

private:
    juce::OpenGLContext openGLContext;
    
    float renderScale = 1.0f;
    
    GLuint quadIndexBuffer = 0;
    GLuint vertexIndexBuffer = 0;
    GLuint vertexBuffer = 0;

    int nPoints = 0;
    int nEdges = 0;

    juce::CriticalSection samplesLock;
    bool needsReattach = true;
    std::vector<Point> samples = std::vector<Point>(2);
    
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
    
    VisualiserSettings& settings;
    SampleRateManager& sampleRateManager;
    float fadeAmount;
    bool smudgesEnabled = settings.getSmudgesEnabled();
    bool graticuleEnabled = settings.getGraticuleEnabled();
    
    bool paused = false;
    
    Texture makeTexture(int width, int height);
    void setupArrays(int num_points);
    void setupTextures();
    void drawLineTexture(std::vector<Point>& points);
    void saveTextureToFile(GLuint textureID, int width, int height, const juce::File& file);
    void activateTargetTexture(std::optional<Texture> texture);
    void setShader(juce::OpenGLShaderProgram* program);
    void drawTexture(std::optional<Texture> texture0, std::optional<Texture> texture1 = std::nullopt, std::optional<Texture> texture2 = std::nullopt, std::optional<Texture> texture3 = std::nullopt);
    void setAdditiveBlending();
    void setNormalBlending();
    void drawLine(std::vector<Point>& points);
    void fade();
    void drawCRT();
    void checkGLErrors(const juce::String& location);
    void viewportChanged();
    Texture createScreenTexture();
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualiserOpenGLComponent)
};

