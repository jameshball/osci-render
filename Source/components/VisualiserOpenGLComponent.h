#pragma once

#include <JuceHeader.h>
#include "VisualiserSettings.h"

struct Texture {
    GLuint id;
    int width;
    int height;
};

class VisualiserOpenGLComponent : public juce::Component, public juce::OpenGLRenderer {
public:
    VisualiserOpenGLComponent(VisualiserSettings& settings);

    void newOpenGLContextCreated() override;
    void renderOpenGL() override;
    void openGLContextClosing() override;
    void resized() override;

private:
    juce::OpenGLContext openGLContext;
    
    GLuint quadIndexBuffer = 0;
    GLuint vertexIndexBuffer = 0;
    GLuint vertexBuffer = 0;

    int nPoints = 0;
    int nEdges = 0;

    std::vector<float> xSamples;
    std::vector<float> ySamples;
    std::vector<float> zSamples;
    
    std::vector<float> scratchVertices;
    std::vector<float> fullScreenQuad;
    
    GLuint frameBuffer = 0;
    Texture lineTexture;
    Texture blur1Texture;
    Texture blur2Texture;
    Texture blur3Texture;
    Texture blur4Texture;
    juce::OpenGLTexture screenOpenGLTexture;
    Texture screenTexture;
    std::optional<Texture> targetTexture = std::nullopt;
    
    std::unique_ptr<juce::OpenGLShaderProgram> simpleShader;
    std::unique_ptr<juce::OpenGLShaderProgram> texturedShader;
    std::unique_ptr<juce::OpenGLShaderProgram> blurShader;
    std::unique_ptr<juce::OpenGLShaderProgram> lineShader;
    std::unique_ptr<juce::OpenGLShaderProgram> outputShader;
    juce::OpenGLShaderProgram* currentShader;
    
    // controls / inputs / settings
    VisualiserSettings& settings;
    float fadeAmount;
    
    Texture makeTexture(int width, int height);
    void setupArrays(int num_points);
    void setupTextures();
    void drawLineTexture(std::vector<float>& xPoints, std::vector<float>& yPoints, std::vector<float>& zPoints);
    void saveTextureToFile(GLuint textureID, int width, int height, const juce::File& file);
    void activateTargetTexture(std::optional<Texture> texture);
    void setShader(juce::OpenGLShaderProgram* program);
    void drawTexture(std::optional<Texture> texture0, std::optional<Texture> texture1 = std::nullopt, std::optional<Texture> texture2 = std::nullopt, std::optional<Texture> texture3 = std::nullopt);
    void setAdditiveBlending();
    void setNormalBlending();
    void drawLine(std::vector<float>& xPoints, std::vector<float>& yPoints, std::vector<float>& zPoints);
    void fade();
    void drawCRT();
    void checkGLErrors(const juce::String& location);
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VisualiserOpenGLComponent)
};

