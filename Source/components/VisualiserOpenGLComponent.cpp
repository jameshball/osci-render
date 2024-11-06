#define CPP_GLSL_INCLUDE

#include "../LookAndFeel.h"
#include "VisualiserOpenGLComponent.h"


VisualiserOpenGLComponent::VisualiserOpenGLComponent(VisualiserSettings& settings, SampleRateManager& sampleRateManager) : settings(settings), sampleRateManager(sampleRateManager) {
    openGLContext.setRenderer(this);
    openGLContext.attachTo(*this);
    setInterceptsMouseClicks(false, false);
}

VisualiserOpenGLComponent::~VisualiserOpenGLComponent() {
    openGLContext.detach();
}

void VisualiserOpenGLComponent::newOpenGLContextCreated() {
    using namespace juce::gl;
    
    juce::CriticalSection::ScopedLockType lock(samplesLock);
    
    juce::OpenGLHelpers::clear(juce::Colours::black);
    glColorMask(true, true, true, true);

    viewportChanged();
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    
    fullScreenQuad = { -1.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, -1.0f };
    
    simpleShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    simpleShader->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(BinaryData::SimpleVertexShader_glsl));
    simpleShader->addFragmentShader(BinaryData::SimpleFragmentShader_glsl);
    simpleShader->link();
    
    lineShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    lineShader->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(BinaryData::LineVertexShader_glsl));
    lineShader->addFragmentShader(BinaryData::LineFragmentShader_glsl);
    lineShader->link();
    
    outputShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    outputShader->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(BinaryData::OutputVertexShader_glsl));
    outputShader->addFragmentShader(BinaryData::OutputFragmentShader_glsl);
    outputShader->link();
    
    texturedShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    texturedShader->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(BinaryData::TexturedVertexShader_glsl));
    texturedShader->addFragmentShader(BinaryData::TexturedFragmentShader_glsl);
    texturedShader->link();
    
    blurShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    blurShader->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(BinaryData::BlurVertexShader_glsl));
    blurShader->addFragmentShader(BinaryData::BlurFragmentShader_glsl);
    blurShader->link();
    
    glGenBuffers(1, &vertexBuffer);
    setupTextures();
    
    setupArrays(smoothedXSamples.size());
}

void VisualiserOpenGLComponent::openGLContextClosing() {
    using namespace juce::gl;
    
    glDeleteBuffers(1, &quadIndexBuffer);
    glDeleteBuffers(1, &vertexIndexBuffer);
    glDeleteBuffers(1, &vertexBuffer);
    glDeleteFramebuffers(1, &frameBuffer);
    glDeleteTextures(1, &lineTexture.id);
    glDeleteTextures(1, &blur1Texture.id);
    glDeleteTextures(1, &blur2Texture.id);
    glDeleteTextures(1, &blur3Texture.id);
    glDeleteTextures(1, &blur4Texture.id);
    screenOpenGLTexture.release();
    
    simpleShader.reset();
    texturedShader.reset();
    blurShader.reset();
    lineShader.reset();
    outputShader.reset();
}

void VisualiserOpenGLComponent::updateBuffer(const std::vector<OsciPoint>& buffer) {
    juce::CriticalSection::ScopedLockType lock(samplesLock);
    
    if (xSamples.size() != buffer.size()) {
        needsReattach = true;
    }
    xSamples.clear();
    ySamples.clear();
    zSamples.clear();
    for (auto& point : buffer) {
        xSamples.push_back(point.x);
        ySamples.push_back(point.y);
        zSamples.push_back(point.z);
    }
    
    triggerAsyncUpdate();
}

void VisualiserOpenGLComponent::handleAsyncUpdate() {
    juce::CriticalSection::ScopedLockType lock(samplesLock);
    
    int newResampledSize = xSamples.size() * RESAMPLE_RATIO;
    
    smoothedXSamples.resize(newResampledSize);
    smoothedYSamples.resize(newResampledSize);
    smoothedZSamples.resize(newResampledSize);
    
    if (sampleRate != sampleRateManager.getSampleRate()) {
        sampleRate = sampleRateManager.getSampleRate();
        xResampler.prepare(sampleRate, RESAMPLE_RATIO);
        yResampler.prepare(sampleRate, RESAMPLE_RATIO);
        zResampler.prepare(sampleRate, RESAMPLE_RATIO);
    }
    
    xResampler.process(xSamples.data(), smoothedXSamples.data(), xSamples.size());
    yResampler.process(ySamples.data(), smoothedYSamples.data(), ySamples.size());
    zResampler.process(zSamples.data(), smoothedZSamples.data(), zSamples.size());
    
    if (needsReattach) {
        openGLContext.detach();
        openGLContext.attachTo(*this);
        needsReattach = false;
    }
    repaint();
}

void VisualiserOpenGLComponent::renderOpenGL() {
    if (openGLContext.isActive()) {
        juce::CriticalSection::ScopedLockType lock(samplesLock);
        
        if (graticuleEnabled != settings.getGraticuleEnabled() || smudgesEnabled != settings.getSmudgesEnabled()) {
            graticuleEnabled = settings.getGraticuleEnabled();
            smudgesEnabled = settings.getSmudgesEnabled();
            screenTexture = createScreenTexture();
        }
        
        renderScale = (float) openGLContext.getRenderingScale();
        
        drawLineTexture(smoothedXSamples, smoothedYSamples, smoothedZSamples);
        checkGLErrors("drawLineTexture");
        drawCRT();
        checkGLErrors("drawCRT");
    }
}

void VisualiserOpenGLComponent::resized() {
    viewportChanged();
}

void VisualiserOpenGLComponent::viewportChanged() {
    using namespace juce::gl;
    
    if (openGLContext.isAttached()) {
        float realWidth = getWidth() * renderScale;
        float realHeight = getHeight() * renderScale;
        float minDim = juce::jmin(realWidth, realHeight);
        float x = (realWidth - minDim) / 2;
        float y = (realHeight - minDim) / 2;
        
        glViewport(juce::roundToInt(x), juce::roundToInt(y), juce::roundToInt(minDim), juce::roundToInt(minDim));
    }
}

void VisualiserOpenGLComponent::setupArrays(int nPoints) {
    using namespace juce::gl;
    
    if (nPoints == 0) {
        return;
    }
    
    this->nPoints = nPoints;
    this->nEdges = this->nPoints - 1;

    // Create the quad index buffer
    glGenBuffers(1, &quadIndexBuffer);
    std::vector<float> indices(4 * nEdges);
    for (size_t i = 0; i < indices.size(); ++i) {
        indices[i] = static_cast<float>(i);
    }

    glBindBuffer(GL_ARRAY_BUFFER, quadIndexBuffer);
    glBufferData(GL_ARRAY_BUFFER, indices.size() * sizeof(float), indices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0); // Unbind

    // Create the vertex index buffer
    glGenBuffers(1, &vertexIndexBuffer);
    int len = nEdges * 2 * 3;
    std::vector<uint16_t> vertexIndices(len);

    for (int i = 0, pos = 0; i < len;) {
        vertexIndices[i++] = pos;
        vertexIndices[i++] = pos + 2;
        vertexIndices[i++] = pos + 1;
        vertexIndices[i++] = pos + 1;
        vertexIndices[i++] = pos + 2;
        vertexIndices[i++] = pos + 3;
        pos += 4;
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vertexIndexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, vertexIndices.size() * sizeof(uint16_t), vertexIndices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); // Unbind

    // Initialize scratch vertices
    scratchVertices.resize(12 * nPoints);
}

void VisualiserOpenGLComponent::setupTextures() {
    using namespace juce::gl;
    
    // Create the framebuffer
    glGenFramebuffers(1, &frameBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);

    // Create textures
    lineTexture = makeTexture(1024, 1024);
    blur1Texture = makeTexture(256, 256);
    blur2Texture = makeTexture(256, 256);
    blur3Texture = makeTexture(32, 32);
    blur4Texture = makeTexture(32, 32);
    
    screenTexture = createScreenTexture();

    glBindFramebuffer(GL_FRAMEBUFFER, 0); // Unbind
}

Texture VisualiserOpenGLComponent::makeTexture(int width, int height) {
    using namespace juce::gl;
    
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);

    // Set texture filtering and wrapping
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glBindTexture(GL_TEXTURE_2D, 0); // Unbind
    
    return { textureID, width, height };
}

void VisualiserOpenGLComponent::drawLineTexture(const std::vector<float>& xPoints, const std::vector<float>& yPoints, const std::vector<float>& zPoints) {
    using namespace juce::gl;
    
    fadeAmount = juce::jmin(1.0, std::pow(0.5, settings.getPersistence()) * 0.4);
    activateTargetTexture(lineTexture);
    fade();
    drawLine(xPoints, yPoints, zPoints);
    glBindTexture(GL_TEXTURE_2D, targetTexture.value().id);
}

void VisualiserOpenGLComponent::saveTextureToFile(GLuint textureID, int width, int height, const juce::File& file) {
    using namespace juce::gl;
    
    // Bind the texture to read its data
    glBindTexture(GL_TEXTURE_2D, textureID);

    // Create a vector to store the pixel data (RGBA)
    std::vector<unsigned char> pixels(width * height * 4);

    // Read the pixels from the texture
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // Convert raw pixel data to JUCE Image
    juce::Image image(juce::Image::PixelFormat::ARGB, width, height, true);  // Create a JUCE image

    // Lock the image to get access to its pixel data
    juce::Image::BitmapData bitmapData(image, juce::Image::BitmapData::writeOnly);

    // Copy the pixel data to the JUCE image (and swap R and B channels)
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int srcIndex = (y * width + x) * 4; // RGBA format
            juce::uint8 r = pixels[srcIndex];     // Red
            juce::uint8 g = pixels[srcIndex + 1]; // Green
            juce::uint8 b = pixels[srcIndex + 2]; // Blue
            juce::uint8 a = pixels[srcIndex + 3]; // Alpha

            // JUCE stores colors in ARGB, so we need to adjust the channel order
            bitmapData.setPixelColour(x, y, juce::Colour(a, r, g, b));
        }
    }

    // Unbind the texture
    glBindTexture(GL_TEXTURE_2D, 0);

    // Save the JUCE image to file (PNG in this case)
    juce::PNGImageFormat pngFormat;
    std::unique_ptr<juce::FileOutputStream> outputStream(file.createOutputStream());
    if (outputStream != nullptr) {
        outputStream->setPosition(0);
        pngFormat.writeImageToStream(image, *outputStream);
        outputStream->flush();
    }
}


void VisualiserOpenGLComponent::activateTargetTexture(std::optional<Texture> texture) {
    using namespace juce::gl;
    
    if (texture.has_value()) {
        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.value().id, 0);
        glViewport(0, 0, texture.value().width, texture.value().height);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        viewportChanged();
    }
    targetTexture = texture;
}

void VisualiserOpenGLComponent::setShader(juce::OpenGLShaderProgram* program) {
    currentShader = program;
    program->use();
}

void VisualiserOpenGLComponent::drawTexture(std::optional<Texture> texture0, std::optional<Texture> texture1, std::optional<Texture> texture2, std::optional<Texture> texture3) {
    using namespace juce::gl;
    
    glEnableVertexAttribArray(glGetAttribLocation(currentShader->getProgramID(), "aPos"));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture0.value().id);
    currentShader->setUniform("uTexture0", 0);

    if (texture1.has_value()) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, texture1.value().id);
        currentShader->setUniform("uTexture1", 1);
    }

    if (texture2.has_value()) {
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, texture2.value().id);
        currentShader->setUniform("uTexture2", 2);
    }

    if (texture3.has_value()) {
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, texture3.value().id);
        currentShader->setUniform("uTexture3", 3);
    }

    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * fullScreenQuad.size(), fullScreenQuad.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(glGetAttribLocation(currentShader->getProgramID(), "aPos"), 2, GL_FLOAT, GL_FALSE, 0, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(glGetAttribLocation(currentShader->getProgramID(), "aPos"));

    if (targetTexture.has_value()) {
        glBindTexture(GL_TEXTURE_2D, targetTexture.value().id);
    }
}

void VisualiserOpenGLComponent::setAdditiveBlending() {
    using namespace juce::gl;
    
    glBlendFunc(GL_ONE, GL_ONE);
}

void VisualiserOpenGLComponent::setNormalBlending() {
    using namespace juce::gl;
    
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void VisualiserOpenGLComponent::drawLine(const std::vector<float>& xPoints, const std::vector<float>& yPoints, const std::vector<float>& zPoints) {
    using namespace juce::gl;
    
    setAdditiveBlending();

    int nPoints = xPoints.size();
    
    for (int i = 0; i < nPoints; ++i) {
        int p = i * 12;
        scratchVertices[p]     = scratchVertices[p + 3] = scratchVertices[p + 6] = scratchVertices[p + 9]  = xPoints[i];
        scratchVertices[p + 1] = scratchVertices[p + 4] = scratchVertices[p + 7] = scratchVertices[p + 10] = yPoints[i];
        scratchVertices[p + 2] = scratchVertices[p + 5] = scratchVertices[p + 8] = scratchVertices[p + 11] = zPoints[i];
    }

    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, scratchVertices.size() * sizeof(float), scratchVertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    lineShader->use();
    glEnableVertexAttribArray(glGetAttribLocation(lineShader->getProgramID(), "aStart"));
    glEnableVertexAttribArray(glGetAttribLocation(lineShader->getProgramID(), "aEnd"));
    glEnableVertexAttribArray(glGetAttribLocation(lineShader->getProgramID(), "aIdx"));

    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glVertexAttribPointer(glGetAttribLocation(lineShader->getProgramID(), "aStart"), 3, GL_FLOAT, GL_FALSE, 0, 0);
    glVertexAttribPointer(glGetAttribLocation(lineShader->getProgramID(), "aEnd"), 3, GL_FLOAT, GL_FALSE, 0, (void*)(12 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, quadIndexBuffer);
    glVertexAttribPointer(glGetAttribLocation(lineShader->getProgramID(), "aIdx"), 1, GL_FLOAT, GL_FALSE, 0, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, screenTexture.id);
    lineShader->setUniform("uScreen", 0);
    lineShader->setUniform("uSize", (GLfloat) settings.getFocus());
    lineShader->setUniform("uGain", 450.0f / 512.0f);
    lineShader->setUniform("uInvert", 1.0f);

    float intensity = settings.getIntensity() * (41000.0f / sampleRateManager.getSampleRate());
    if (settings.getUpsamplingEnabled()) {
        lineShader->setUniform("uIntensity", intensity);
    } else {
        // TODO: filter steps
        int steps = 6;
        lineShader->setUniform("uIntensity", (GLfloat) (intensity * (steps + 1.5)));
    }
    
    lineShader->setUniform("uFadeAmount", fadeAmount);
    lineShader->setUniform("uNEdges", (GLfloat) nEdges);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vertexIndexBuffer);
    int nEdgesThisTime = xPoints.size() - 1;
    glDrawElements(GL_TRIANGLES, nEdgesThisTime * 6, GL_UNSIGNED_SHORT, 0);

    glDisableVertexAttribArray(glGetAttribLocation(lineShader->getProgramID(), "aStart"));
    glDisableVertexAttribArray(glGetAttribLocation(lineShader->getProgramID(), "aEnd"));
    glDisableVertexAttribArray(glGetAttribLocation(lineShader->getProgramID(), "aIdx"));
}

void VisualiserOpenGLComponent::fade() {
    using namespace juce::gl;
    
    setNormalBlending();
    
    simpleShader->use();
    glEnableVertexAttribArray(glGetAttribLocation(simpleShader->getProgramID(), "vertexPosition"));
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * fullScreenQuad.size(), fullScreenQuad.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(glGetAttribLocation(simpleShader->getProgramID(), "vertexPosition"), 2, GL_FLOAT, GL_FALSE, 0, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    
    simpleShader->setUniform("colour", 0.0f, 0.0f, 0.0f, fadeAmount);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(glGetAttribLocation(simpleShader->getProgramID(), "vertexPosition"));
}

void VisualiserOpenGLComponent::drawCRT() {
    setNormalBlending();
    
    activateTargetTexture(blur1Texture);
    setShader(texturedShader.get());
    texturedShader->setUniform("uResizeForCanvas", lineTexture.width / 1024.0f);
    drawTexture(lineTexture);
    
    //horizontal blur 256x256
    activateTargetTexture(blur2Texture);
    setShader(blurShader.get());
    blurShader->setUniform("uOffset", 1.0f / 256.0f, 0.0f);
    drawTexture(blur1Texture);
    
    //vertical blur 256x256
    activateTargetTexture(blur1Texture);
    blurShader->setUniform("uOffset", 0.0f, 1.0f / 256.0f);
    drawTexture(blur2Texture);
    
    //preserve blur1 for later
    activateTargetTexture(blur3Texture);
    setShader(texturedShader.get());
    texturedShader->setUniform("uResizeForCanvas", 1.0f);
    drawTexture(blur1Texture);
    
    //horizontal blur 64x64
    activateTargetTexture(blur4Texture);
    setShader(blurShader.get());
    blurShader->setUniform("uOffset", 1.0f / 32.0f, 1.0f / 60.0f);
    drawTexture(blur3Texture);

    //vertical blur 64x64
    activateTargetTexture(blur3Texture);
    blurShader->setUniform("uOffset", -1.0f / 60.0f, 1.0f / 32.0f);
    drawTexture(blur4Texture);
    
    activateTargetTexture(std::nullopt);
    setShader(outputShader.get());
    float brightness = std::pow(2, settings.getBrightness() - 2);
    outputShader->setUniform("uExposure", brightness);
    outputShader->setUniform("uSaturation", (float) settings.getSaturation());
    outputShader->setUniform("uResizeForCanvas", lineTexture.width / 1024.0f);
    juce::Colour colour = juce::Colour::fromHSV(settings.getHue() / 360.0f, 1.0, 1.0, 1.0);
    outputShader->setUniform("uColour", colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue());
    drawTexture(lineTexture, blur1Texture, blur3Texture, screenTexture);
}

Texture VisualiserOpenGLComponent::createScreenTexture() {
    using namespace juce::gl;
    
    if (settings.getSmudgesEnabled()) {
        screenOpenGLTexture.loadImage(screenTextureImage);
    } else {
        screenOpenGLTexture.loadImage(emptyScreenImage);
    }
    Texture texture = { screenOpenGLTexture.getTextureID(), screenTextureImage.getWidth(), screenTextureImage.getHeight() };
    
    if (settings.getGraticuleEnabled()) {
        activateTargetTexture(texture);
        setNormalBlending();
        setShader(simpleShader.get());
        glColorMask(true, false, false, true);
        
        std::vector<float> data;
        
        int step = 45;
        
        for (int i = 0; i < 11; i++) {
            float s = i * step;
            
            // Inserting at the beginning of the vector (equivalent to splice(0,0,...))
            data.insert(data.begin(), {0, s, 10.0f * step, s});
            data.insert(data.begin(), {s, 0, s, 10.0f * step});
            
            if (i != 0 && i != 10) {
                for (int j = 0; j < 51; j++) {
                    float t = j * step / 5;
                    if (i != 5) {
                        data.insert(data.begin(), {t, s - 2, t, s + 1});
                        data.insert(data.begin(), {s - 2, t, s + 1, t});
                    } else {
                        data.insert(data.begin(), {t, s - 5, t, s + 4});
                        data.insert(data.begin(), {s - 5, t, s + 4, t});
                    }
                }
            }
        }
        
        for (int j = 0; j < 51; j++) {
            float t = j * step / 5;
            if (static_cast<int>(t) % 5 == 0) continue;
            
            data.insert(data.begin(), {t - 2, 2.5f * step, t + 2, 2.5f * step});
            data.insert(data.begin(), {t - 2, 7.5f * step, t + 2, 7.5f * step});
        }
        
        // Normalize the data
        for (size_t i = 0; i < data.size(); i++) {
            data[i] = (data[i] + 31.0f) / 256.0f - 1;
        }
        
        glEnableVertexAttribArray(glGetAttribLocation(simpleShader->getProgramID(), "vertexPosition"));
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * data.size(), data.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(glGetAttribLocation(simpleShader->getProgramID(), "vertexPosition"), 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        simpleShader->setUniform("colour", 0.01f, 0.1f, 0.01f, 1.0f);
        glLineWidth(1.0f);
        glDrawArrays(GL_LINES, 0, data.size());
        glBindTexture(GL_TEXTURE_2D, targetTexture.value().id);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    
    return texture;
}

void VisualiserOpenGLComponent::checkGLErrors(const juce::String& location) {
    using namespace juce::gl;
    
    GLenum error;
    while ((error = glGetError()) != GL_NO_ERROR) {
        juce::String errorMessage;
        switch (error) {
            case GL_INVALID_ENUM:      errorMessage = "GL_INVALID_ENUM"; break;
            case GL_INVALID_VALUE:     errorMessage = "GL_INVALID_VALUE"; break;
            case GL_INVALID_OPERATION: errorMessage = "GL_INVALID_OPERATION"; break;
            case GL_STACK_OVERFLOW:    errorMessage = "GL_STACK_OVERFLOW"; break;
            case GL_STACK_UNDERFLOW:   errorMessage = "GL_STACK_UNDERFLOW"; break;
            case GL_OUT_OF_MEMORY:     errorMessage = "GL_OUT_OF_MEMORY"; break;
            case GL_INVALID_FRAMEBUFFER_OPERATION: errorMessage = "GL_INVALID_FRAMEBUFFER_OPERATION"; break;
            default: errorMessage = "Unknown OpenGL error"; break;
        }
        DBG("OpenGL error at " + location + ": " + errorMessage);
    }
}

void VisualiserOpenGLComponent::setPaused(bool paused) {
    this->paused = paused;
    repaint();
}

void VisualiserOpenGLComponent::paint(juce::Graphics& g) {
    if (paused) {
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRect(getLocalBounds());
        
        g.setColour(juce::Colours::white);
        g.setFont(30.0f);
        g.drawText("Paused", getLocalBounds(), juce::Justification::centred);
    }
}
