#include "VisualiserRenderer.h"

#include "AfterglowFragmentShader.glsl"
#include "AfterglowVertexShader.glsl"
#include "BlurFragmentShader.glsl"
#include "BlurVertexShader.glsl"
#include "GlowFragmentShader.glsl"
#include "GlowVertexShader.glsl"
#include "LineFragmentShader.glsl"
#include "LineVertexShader.glsl"
#include "OutputFragmentShader.glsl"
#include "OutputVertexShader.glsl"
#include "SimpleFragmentShader.glsl"
#include "SimpleVertexShader.glsl"
#include "TexturedFragmentShader.glsl"
#include "TexturedVertexShader.glsl"
#include "WideBlurFragmentShader.glsl"
#include "WideBlurVertexShader.glsl"

VisualiserRenderer::VisualiserRenderer(
    VisualiserParameters &parameters,
    osci::AudioBackgroundThreadManager &threadManager,
    int resolution,
    double frameRate,
    juce::String threadName
) : parameters(parameters),
    osci::AudioBackgroundThread("VisualiserRenderer" + threadName, threadManager),
    resolution(resolution),
    frameRate(frameRate)
{
    openGLContext.setRenderer(this);
    openGLContext.attachTo(*this);
    setShouldBeRunning(true);
}

VisualiserRenderer::~VisualiserRenderer() {
    openGLContext.detach();
    setShouldBeRunning(false, [this] { renderingSemaphore.release(); });
}

void VisualiserRenderer::runTask(const std::vector<osci::Point> &points) {
    {
        juce::CriticalSection::ScopedLockType lock(samplesLock);

        // copy the points before applying effects
        audioOutputBuffer.setSize(2, points.size(), false, true, true);
        for (int i = 0; i < points.size(); ++i) {
            audioOutputBuffer.setSample(0, i, points[i].x);
            audioOutputBuffer.setSample(1, i, points[i].y);
        }

        xSamples.clear();
        ySamples.clear();
        zSamples.clear();

        auto applyEffects = [&](osci::Point point) {
            for (auto &effect : parameters.audioEffects) {
                point = effect->apply(0, point);
            }
#if OSCI_PREMIUM
            if (parameters.isFlippedHorizontal()) {
                point.x = -point.x;
            }
            if (parameters.isFlippedVertical()) {
                point.y = -point.y;
            }
#endif
            return point;
        };

        if (parameters.isSweepEnabled()) {
            double sweepIncrement = getSweepIncrement();
            long samplesPerSweep = sampleRate * parameters.getSweepSeconds();

            double triggerValue = parameters.getTriggerValue();
            bool belowTrigger = false;

            for (const osci::Point &point : points) {
                long samplePosition = sampleCount - lastTriggerPosition;
                double startPoint = 1.135;
                double sweep = samplePosition * sweepIncrement * 2 * startPoint - startPoint;

                double value = point.x;

                if (sweep > startPoint && belowTrigger && value >= triggerValue) {
                    lastTriggerPosition = sampleCount;
                }

                belowTrigger = value < triggerValue;

                osci::Point sweepPoint = {sweep, value, 1};
                sweepPoint = applyEffects(sweepPoint);

                xSamples.push_back(sweepPoint.x);
                ySamples.push_back(sweepPoint.y);
                zSamples.push_back(1);

                sampleCount++;
            }
        } else {
            for (const osci::Point &rawPoint : points) {
                osci::Point point = applyEffects(rawPoint);

#if OSCI_PREMIUM
                if (parameters.isGoniometer()) {
                    // x and y go to a diagonal currently, so we need to scale them down, and rotate them
                    point.scale(1.0 / std::sqrt(2.0), 1.0 / std::sqrt(2.0), 1.0);
                    point.rotate(0, 0, -juce::MathConstants<double>::pi / 4);
                }
#endif

                xSamples.push_back(point.x);
                ySamples.push_back(point.y);
                zSamples.push_back(point.z);
            }
        }

        sampleBufferCount++;

        if (parameters.upsamplingEnabled->getBoolValue()) {
            int newResampledSize = xSamples.size() * RESAMPLE_RATIO;

            smoothedXSamples.resize(newResampledSize);
            smoothedYSamples.resize(newResampledSize);
            smoothedZSamples.resize(newResampledSize);
            smoothedZSamples.resize(newResampledSize);

            if (parameters.isSweepEnabled()) {
                // interpolate between sweep values to avoid any artifacts from quickly going from one sweep to the next
                for (int i = 0; i < newResampledSize; ++i) {
                    int index = i / RESAMPLE_RATIO;
                    if (index < xSamples.size() - 1) {
                        double thisSample = xSamples[index];
                        double nextSample = xSamples[index + 1];
                        if (nextSample > thisSample) {
                            smoothedXSamples[i] = xSamples[index] + (i % (int)RESAMPLE_RATIO) * (nextSample - thisSample) / RESAMPLE_RATIO;
                        } else {
                            smoothedXSamples[i] = xSamples[index];
                        }
                    } else {
                        smoothedXSamples[i] = xSamples[index];
                    }
                }
            } else {
                xResampler.process(xSamples.data(), smoothedXSamples.data(), xSamples.size());
            }
            yResampler.process(ySamples.data(), smoothedYSamples.data(), ySamples.size());
            zResampler.process(zSamples.data(), smoothedZSamples.data(), zSamples.size());
        }
    }

    // this just triggers a repaint
    triggerAsyncUpdate();
    // wait for rendering on the OpenGLRenderer thread to complete
    if (!renderingSemaphore.acquire()) {
        // If acquire times out, log a message or handle it as appropriate
        juce::Logger::writeToLog("Rendering semaphore acquisition timed out");
    }
}

int VisualiserRenderer::prepareTask(double sampleRate, int bufferSize) {
    this->sampleRate = sampleRate;
    xResampler.prepare(sampleRate, RESAMPLE_RATIO);
    yResampler.prepare(sampleRate, RESAMPLE_RATIO);
    zResampler.prepare(sampleRate, RESAMPLE_RATIO);

    int desiredBufferSize = sampleRate / frameRate;

    return desiredBufferSize;
}

void VisualiserRenderer::stopTask() {
    renderingSemaphore.release();
}

double VisualiserRenderer::getSweepIncrement() {
    return 1.0 / (sampleRate * parameters.getSweepSeconds());
}

void VisualiserRenderer::resized() {
    auto area = getLocalBounds();
    viewportArea = area;
    viewportChanged(viewportArea);
}

void VisualiserRenderer::getFrame(std::vector<unsigned char>& frame) {
    using namespace juce::gl;

    glBindTexture(GL_TEXTURE_2D, renderTexture.id);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, frame.data());
}

void VisualiserRenderer::drawFrame() {
    using namespace juce::gl;

    // The crop rectangle will be applied in drawTexture if it's set
    setShader(texturedShader.get());
    drawTexture({renderTexture});
}

void VisualiserRenderer::newOpenGLContextCreated() {
    using namespace juce::gl;

    juce::CriticalSection::ScopedLockType lock(samplesLock);

    glColorMask(true, true, true, true);

    viewportChanged(viewportArea);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);

    fullScreenQuad = {-1.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, -1.0f};

    simpleShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    simpleShader->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(simpleVertexShader));
    simpleShader->addFragmentShader(simpleFragmentShader);
    simpleShader->link();

    lineShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    lineShader->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(lineVertexShader));
    lineShader->addFragmentShader(lineFragmentShader);
    lineShader->link();

    outputShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    outputShader->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(outputVertexShader));
    outputShader->addFragmentShader(outputFragmentShader);
    outputShader->link();    texturedShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    texturedShader->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(texturedVertexShader));
    texturedShader->addFragmentShader(texturedFragmentShader);
    texturedShader->link();
    
    // Initialize crop rectangle uniforms with default values
    texturedShader->use();
    texturedShader->setUniform("uCropEnabled", 0.0f);
    texturedShader->setUniform("uCropRect", 0.0f, 0.0f, 1.0f, 1.0f);

    blurShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    blurShader->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(blurVertexShader));
    blurShader->addFragmentShader(blurFragmentShader);
    blurShader->link();

    wideBlurShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    wideBlurShader->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(wideBlurVertexShader));
    wideBlurShader->addFragmentShader(wideBlurFragmentShader);
    wideBlurShader->link();

#if OSCI_PREMIUM
    glowShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    glowShader->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(glowVertexShader));
    glowShader->addFragmentShader(glowFragmentShader);
    glowShader->link();

    afterglowShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    afterglowShader->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(afterglowVertexShader));
    afterglowShader->addFragmentShader(afterglowFragmentShader);
    afterglowShader->link();
#endif

    glGenBuffers(1, &vertexBuffer);
    glGenBuffers(1, &quadIndexBuffer);
    glGenBuffers(1, &vertexIndexBuffer);

    setupTextures(resolution);
}

void VisualiserRenderer::openGLContextClosing() {
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
    glDeleteTextures(1, &renderTexture.id);
    screenOpenGLTexture.release();

#if OSCI_PREMIUM
    glDeleteTextures(1, &glowTexture.id);
    reflectionOpenGLTexture.release();
    glowShader.reset();
    afterglowShader.reset();
#endif

    simpleShader.reset();
    texturedShader.reset();
    blurShader.reset();
    wideBlurShader.reset();
    lineShader.reset();
    outputShader.reset();

    // this triggers setupArrays to be called again when the scope next renders
    scratchVertices.clear();
}

void VisualiserRenderer::handleAsyncUpdate() {
    repaint();
}

void VisualiserRenderer::renderOpenGL() {
    using namespace juce::gl;

    if (openGLContext.isActive()) {
        juce::OpenGLHelpers::clear(juce::Colours::black);

        // we have a new buffer to render
        if (sampleBufferCount != prevSampleBufferCount) {
            prevSampleBufferCount = sampleBufferCount;

            if (preRenderCallback) {
                preRenderCallback();
            }

            juce::CriticalSection::ScopedLockType lock(samplesLock);

            if (parameters.upsamplingEnabled->getBoolValue()) {
                renderScope(smoothedXSamples, smoothedYSamples, smoothedZSamples);
            } else {
                renderScope(xSamples, ySamples, zSamples);
            }

            if (postRenderCallback) {
                postRenderCallback();
            }

            renderingSemaphore.release();
        }

        // render texture to screen
        activateTargetTexture(std::nullopt);
        setShader(texturedShader.get());
        drawTexture({renderTexture});
    }
}

void VisualiserRenderer::viewportChanged(juce::Rectangle<int> area) {
    using namespace juce::gl;

    if (openGLContext.isAttached()) {
        float realWidth = area.getWidth() * renderScale;
        float realHeight = area.getHeight() * renderScale;

        float xOffset = getWidth() * renderScale - realWidth;
        float yOffset = getHeight() * renderScale - realHeight;

        if (cropRectangle.has_value()) {
            // When crop rectangle is provided, use the full viewport area
            // The crop rectangle will be applied in drawTexture() via texture coordinates
            float x = area.getX() * renderScale + xOffset;
            float y = area.getY() * renderScale + yOffset;
            
            glViewport(juce::roundToInt(x), juce::roundToInt(y), 
                       juce::roundToInt(realWidth), juce::roundToInt(realHeight));
        } else {
            // Original square viewport calculation
            float minDim = juce::jmin(realWidth, realHeight);
            float x = (realWidth - minDim) / 2 + area.getX() * renderScale + xOffset;
            float y = (realHeight - minDim) / 2 - area.getY() * renderScale + yOffset;

            glViewport(juce::roundToInt(x), juce::roundToInt(y), 
                       juce::roundToInt(minDim), juce::roundToInt(minDim));
        }
    }
}

void VisualiserRenderer::setupArrays(int nPoints) {
    using namespace juce::gl;

    if (nPoints == 0) {
        return;
    }

    nEdges = nPoints - 1;

    std::vector<float> indices(4 * nEdges);
    for (size_t i = 0; i < indices.size(); ++i) {
        indices[i] = static_cast<float>(i);
    }

    glBindBuffer(GL_ARRAY_BUFFER, quadIndexBuffer);
    glBufferData(GL_ARRAY_BUFFER, indices.size() * sizeof(float), indices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0); // Unbind

    int len = nEdges * 2 * 3;
    std::vector<uint32_t> vertexIndices(len);

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
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, vertexIndices.size() * sizeof(uint32_t), vertexIndices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0); // Unbind

    // Initialize scratch vertices
    scratchVertices.resize(12 * nPoints);
}

void VisualiserRenderer::setupTextures(int resolution) {
    using namespace juce::gl;

    // Create the framebuffer
    glGenFramebuffers(1, &frameBuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);

    // Create textures
    lineTexture = makeTexture(resolution, resolution);
    blur1Texture = makeTexture(512, 512);
    blur2Texture = makeTexture(512, 512);
    blur3Texture = makeTexture(128, 128);
    blur4Texture = makeTexture(128, 128);
    renderTexture = makeTexture(resolution, resolution);

    screenOpenGLTexture.loadImage(emptyScreenImage);
    screenTexture = {screenOpenGLTexture.getTextureID(), screenTextureImage.getWidth(), screenTextureImage.getHeight()};

#if OSCI_PREMIUM
    glowTexture = makeTexture(512, 512);
    reflectionTexture = createReflectionTexture();
#endif

    glBindFramebuffer(GL_FRAMEBUFFER, 0); // Unbind
}

Texture VisualiserRenderer::makeTexture(int width, int height, GLuint textureID) {
    using namespace juce::gl;

    // replace existing texture if it exists, otherwise create new texture
    if (textureID == 0) {
        glGenTextures(1, &textureID);
    }
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);

    // Set texture filtering and wrapping
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = {0.0f, 0.0f, 0.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureID, 0);
    glViewport(0, 0, width, height);

    // Clear it once so we don't see uninitialized pixels
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    glBindTexture(GL_TEXTURE_2D, 0); // Unbind

    return {textureID, width, height};
}

void VisualiserRenderer::setResolution(int resolution) {
    using namespace juce::gl;

    if (this->resolution != resolution) {
        this->resolution = resolution;

        // Release semaphore to prevent deadlocks during texture rebuilding
        renderingSemaphore.release();

        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);

        lineTexture = makeTexture(resolution, resolution, lineTexture.id);
        renderTexture = makeTexture(resolution, resolution, renderTexture.id);

        glBindFramebuffer(GL_FRAMEBUFFER, 0); // Unbind
    }
}

void VisualiserRenderer::setFrameRate(double frameRate) {
    using namespace juce::gl;

    if (this->frameRate != frameRate) {
        this->frameRate = frameRate;
        prepare(sampleRate, -1);
        setupArrays(RESAMPLE_RATIO * sampleRate / frameRate);
    }
}

void VisualiserRenderer::drawLineTexture(const std::vector<float> &xPoints, const std::vector<float> &yPoints, const std::vector<float> &zPoints) {
    using namespace juce::gl;

    double persistence = std::pow(0.5, parameters.getPersistence()) * 0.4;
    persistence *= 60.0 / frameRate;
    fadeAmount = juce::jmin(1.0, persistence);

    activateTargetTexture(lineTexture);
    fade();
    drawLine(xPoints, yPoints, zPoints);
    glBindTexture(GL_TEXTURE_2D, targetTexture.value().id);
}

void VisualiserRenderer::saveTextureToPNG(Texture texture, const juce::File &file) {
    using namespace juce::gl;
    GLuint textureID = texture.id;
    int width = texture.width;
    int height = texture.height;

    // Bind the texture to read its data
    glBindTexture(GL_TEXTURE_2D, textureID);
    std::vector<unsigned char> pixels = std::vector<unsigned char>(width * height * 4);
    // Read the pixels from the texture
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    juce::Image image = juce::Image(juce::Image::PixelFormat::ARGB, width, height, true);
    juce::Image::BitmapData bitmapData(image, juce::Image::BitmapData::writeOnly);

    // Copy the pixel data to the JUCE image (and swap R and B channels)
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int srcIndex = (y * width + x) * 4;     // RGBA format
            juce::uint8 r = (pixels)[srcIndex];     // Red
            juce::uint8 g = (pixels)[srcIndex + 1]; // Green
            juce::uint8 b = (pixels)[srcIndex + 2]; // Blue
            juce::uint8 a = (pixels)[srcIndex + 3]; // Alpha

            // This method uses colors in RGBA
            bitmapData.setPixelColour(x, height - y - 1, juce::Colour(r, g, b, a));
        }
    }

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

void VisualiserRenderer::activateTargetTexture(std::optional<Texture> texture) {
    using namespace juce::gl;

    if (texture.has_value()) {
        glBindFramebuffer(GL_FRAMEBUFFER, frameBuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.value().id, 0);
        glViewport(0, 0, texture.value().width, texture.value().height);
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        viewportChanged(viewportArea);
    }
    targetTexture = texture;
}

void VisualiserRenderer::setShader(juce::OpenGLShaderProgram *program) {
    currentShader = program;
    program->use();
}

void VisualiserRenderer::drawTexture(std::vector<std::optional<Texture>> textures) {
    using namespace juce::gl;

    glEnableVertexAttribArray(glGetAttribLocation(currentShader->getProgramID(), "aPos"));

    for (int i = 0; i < textures.size(); ++i) {
        if (textures[i].has_value()) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, textures[i].value().id);
            currentShader->setUniform(("uTexture" + juce::String(i)).toStdString().c_str(), i);
        }
    }

    // Check if we need to apply texture coordinate transformation for cropping
    // Only do this when displaying to screen (no target texture) and a crop rectangle is set
    if (!targetTexture.has_value() && cropRectangle.has_value() && !textures.empty() && textures[0].has_value()) {
        // Create quad with adjusted texture coordinates for cropping
        std::vector<float> croppedQuad = fullScreenQuad;
        
        // For simplified calculation, assume we're working with the first texture
        const auto& crop = cropRectangle.value();
        
        // Apply texture coordinate transformations to match the crop rectangle
        // This transforms the quad's texture coordinates to sample only the cropped region
        currentShader->setUniform("uCropEnabled", 1.0f);
        currentShader->setUniform("uCropRect", 
                                 crop.getX(), 
                                 crop.getY(), 
                                 crop.getWidth(), 
                                 crop.getHeight());
    } else {
        // No cropping, use standard coordinates
        currentShader->setUniform("uCropEnabled", 0.0f);
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

void VisualiserRenderer::setAdditiveBlending() {
    using namespace juce::gl;

    glBlendFunc(GL_ONE, GL_ONE);
}

void VisualiserRenderer::setNormalBlending() {
    using namespace juce::gl;

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void VisualiserRenderer::drawLine(const std::vector<float> &xPoints, const std::vector<float> &yPoints, const std::vector<float> &zPoints) {
    using namespace juce::gl;

    setAdditiveBlending();

    int nPoints = xPoints.size();

    // Without this, there's an access violation that seems to occur only on some systems
    if (scratchVertices.size() != nPoints * 12)
        scratchVertices.resize(nPoints * 12);

    for (int i = 0; i < nPoints; ++i) {
        int p = i * 12;
        scratchVertices[p] = scratchVertices[p + 3] = scratchVertices[p + 6] = scratchVertices[p + 9] = xPoints[i];
        scratchVertices[p + 1] = scratchVertices[p + 4] = scratchVertices[p + 7] = scratchVertices[p + 10] = yPoints[i];
        scratchVertices[p + 2] = scratchVertices[p + 5] = scratchVertices[p + 8] = scratchVertices[p + 11] = zPoints[i];
    }

    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, nPoints * 12 * sizeof(float), scratchVertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    lineShader->use();
    glEnableVertexAttribArray(glGetAttribLocation(lineShader->getProgramID(), "aStart"));
    glEnableVertexAttribArray(glGetAttribLocation(lineShader->getProgramID(), "aEnd"));
    glEnableVertexAttribArray(glGetAttribLocation(lineShader->getProgramID(), "aIdx"));

    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glVertexAttribPointer(glGetAttribLocation(lineShader->getProgramID(), "aStart"), 3, GL_FLOAT, GL_FALSE, 0, 0);
    glVertexAttribPointer(glGetAttribLocation(lineShader->getProgramID(), "aEnd"), 3, GL_FLOAT, GL_FALSE, 0, (void *)(12 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, quadIndexBuffer);
    glVertexAttribPointer(glGetAttribLocation(lineShader->getProgramID(), "aIdx"), 1, GL_FLOAT, GL_FALSE, 0, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, screenTexture.id);
    lineShader->setUniform("uScreen", 0);
    lineShader->setUniform("uSize", (GLfloat)parameters.getFocus());
    lineShader->setUniform("uGain", 450.0f / 512.0f);
    lineShader->setUniform("uInvert", 1.0f);

    float intensity = parameters.getIntensity() * (41000.0f / sampleRate);
    if (parameters.getUpsamplingEnabled()) {
        lineShader->setUniform("uIntensity", intensity);
    } else {
        lineShader->setUniform("uIntensity", (GLfloat)(intensity * RESAMPLE_RATIO * 1.5));
    }

    lineShader->setUniform("uFadeAmount", fadeAmount);
    lineShader->setUniform("uNEdges", (GLfloat)nEdges);
    setOffsetAndScale(lineShader.get());

#if OSCI_PREMIUM
    lineShader->setUniform("uFishEye", screenOverlay == ScreenOverlay::VectorDisplay ? VECTOR_DISPLAY_FISH_EYE : 0.0f);
    lineShader->setUniform("uShutterSync", parameters.getShutterSync());
#else
    lineShader->setUniform("uFishEye", 0.0f);
    lineShader->setUniform("uShutterSync", false);
#endif

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vertexIndexBuffer);
    int nEdgesThisTime = xPoints.size() - 1;
    glDrawElements(GL_TRIANGLES, nEdgesThisTime * 6, GL_UNSIGNED_INT, 0);

    glDisableVertexAttribArray(glGetAttribLocation(lineShader->getProgramID(), "aStart"));
    glDisableVertexAttribArray(glGetAttribLocation(lineShader->getProgramID(), "aEnd"));
    glDisableVertexAttribArray(glGetAttribLocation(lineShader->getProgramID(), "aIdx"));
}

void VisualiserRenderer::fade() {
    using namespace juce::gl;

    setNormalBlending();

#if OSCI_PREMIUM
    setShader(afterglowShader.get());
    afterglowShader->setUniform("fadeAmount", fadeAmount);
    afterglowShader->setUniform("afterglowAmount", (float)parameters.getAfterglow());
    afterglowShader->setUniform("uResizeForCanvas", lineTexture.width / renderTexture.width);
    drawTexture({lineTexture});
#else
    simpleShader->use();
    glEnableVertexAttribArray(glGetAttribLocation(simpleShader->getProgramID(), "vertexPosition"));
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * fullScreenQuad.size(), fullScreenQuad.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(glGetAttribLocation(simpleShader->getProgramID(), "vertexPosition"), 2, GL_FLOAT, GL_FALSE, 0, 0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    simpleShader->setUniform("colour", 0.0f, 0.0f, 0.0f, fadeAmount);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(glGetAttribLocation(simpleShader->getProgramID(), "vertexPosition"));
#endif
}

void VisualiserRenderer::drawCRT() {
    using namespace juce::gl;

    setNormalBlending();

    activateTargetTexture(blur1Texture);
    setShader(texturedShader.get());
    texturedShader->setUniform("uResizeForCanvas", lineTexture.width / (float) renderTexture.width);
    drawTexture({lineTexture});
    checkGLErrors(__FILE__, __LINE__);

    // horizontal blur 512x512
    activateTargetTexture(blur2Texture);
    setShader(blurShader.get());
    blurShader->setUniform("uOffset", 1.0f / 512.0f, 0.0f);
    drawTexture({blur1Texture});
    checkGLErrors(__FILE__, __LINE__);

    // vertical blur 512x512
    activateTargetTexture(blur1Texture);
    blurShader->setUniform("uOffset", 0.0f, 1.0f / 512.0f);
    drawTexture({blur2Texture});
    checkGLErrors(__FILE__, __LINE__);

    // preserve blur1 for later
    activateTargetTexture(blur3Texture);
    setShader(texturedShader.get());
    texturedShader->setUniform("uResizeForCanvas", 1.0f);
    drawTexture({blur1Texture});
    checkGLErrors(__FILE__, __LINE__);

    // horizontal blur 128x128
    activateTargetTexture(blur4Texture);
    setShader(wideBlurShader.get());
    wideBlurShader->setUniform("uOffset", 1.0f / 128.0f, 0.0f);
    drawTexture({blur3Texture});
    checkGLErrors(__FILE__, __LINE__);

    // vertical blur 128x128
    activateTargetTexture(blur3Texture);
    wideBlurShader->setUniform("uOffset", 0.0f, 1.0f / 128.0f);
    drawTexture({blur4Texture});
    checkGLErrors(__FILE__, __LINE__);

#if OSCI_PREMIUM
    if (parameters.screenOverlay->isRealisticDisplay()) {
        // create glow texture
        activateTargetTexture(glowTexture);
        setShader(glowShader.get());
        setOffsetAndScale(glowShader.get());
        drawTexture({blur3Texture});
        checkGLErrors(__FILE__, __LINE__);
    }
#endif

    activateTargetTexture(renderTexture);
    setShader(outputShader.get());
    outputShader->setUniform("uExposure", 0.25f);
    outputShader->setUniform("uLineSaturation", (float)parameters.getLineSaturation());
#if OSCI_PREMIUM
    outputShader->setUniform("uScreenSaturation", (float)parameters.getScreenSaturation());
    outputShader->setUniform("uHueShift", (float)parameters.getScreenHue() / 360.0f);
    outputShader->setUniform("uOverexposure", (float)parameters.getOverexposure());
#else
    outputShader->setUniform("uScreenSaturation", 1.0f);
    outputShader->setUniform("uHueShift", 0.0f);
    outputShader->setUniform("uOverexposure", 0.5f);
#endif
    outputShader->setUniform("uNoise", (float)parameters.getNoise());
    outputShader->setUniform("uRandom", juce::Random::getSystemRandom().nextFloat());
    outputShader->setUniform("uGlow", (float)parameters.getGlow());
    outputShader->setUniform("uAmbient", (float)parameters.getAmbient());
    setOffsetAndScale(outputShader.get());
#if OSCI_PREMIUM
    outputShader->setUniform("uFishEye", screenOverlay == ScreenOverlay::VectorDisplay ? VECTOR_DISPLAY_FISH_EYE : 0.0f);
    outputShader->setUniform("uRealScreen", parameters.screenOverlay->isRealisticDisplay() ? 1.0f : 0.0f);
#endif
    outputShader->setUniform("uResizeForCanvas", lineTexture.width / (float) renderTexture.width);
    juce::Colour colour = juce::Colour::fromHSV(parameters.getHue() / 360.0f, 1.0, 1.0, 1.0);
    outputShader->setUniform("uColour", colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue());
    drawTexture({
        lineTexture,
        blur1Texture,
        blur3Texture,
        screenTexture,
#if OSCI_PREMIUM
        reflectionTexture,
        glowTexture,
#endif
    });
    checkGLErrors(__FILE__, __LINE__);
}

void VisualiserRenderer::setOffsetAndScale(juce::OpenGLShaderProgram *shader) {
    osci::Point offset;
    osci::Point scale = {1.0f};
#if OSCI_PREMIUM
    if (parameters.getScreenOverlay() == ScreenOverlay::Real) {
        offset = REAL_SCREEN_OFFSET;
        scale = REAL_SCREEN_SCALE;
    } else if (parameters.getScreenOverlay() == ScreenOverlay::VectorDisplay) {
        offset = VECTOR_DISPLAY_OFFSET;
        scale = VECTOR_DISPLAY_SCALE;
    }
#endif
    shader->setUniform("uOffset", (float)offset.x, (float)offset.y);
    shader->setUniform("uScale", (float)scale.x, (float)scale.y);
}

#if OSCI_PREMIUM
Texture VisualiserRenderer::createReflectionTexture() {
    using namespace juce::gl;

    if (parameters.getScreenOverlay() == ScreenOverlay::VectorDisplay) {
        reflectionOpenGLTexture.loadImage(vectorDisplayReflectionImage);
    } else if (parameters.getScreenOverlay() == ScreenOverlay::Real) {
        reflectionOpenGLTexture.loadImage(oscilloscopeReflectionImage);
    } else {
        reflectionOpenGLTexture.loadImage(emptyReflectionImage);
    }

    Texture texture = {reflectionOpenGLTexture.getTextureID(), reflectionOpenGLTexture.getWidth(), reflectionOpenGLTexture.getHeight()};

    return texture;
}
#endif

Texture VisualiserRenderer::createScreenTexture() {
    using namespace juce::gl;

    if (screenOverlay == ScreenOverlay::Smudged || screenOverlay == ScreenOverlay::SmudgedGraticule) {
        screenOpenGLTexture.loadImage(screenTextureImage);
#if OSCI_PREMIUM
    } else if (screenOverlay == ScreenOverlay::Real) {
        screenOpenGLTexture.loadImage(oscilloscopeImage);
    } else if (screenOverlay == ScreenOverlay::VectorDisplay) {
        screenOpenGLTexture.loadImage(vectorDisplayImage);
#endif
    } else {
        screenOpenGLTexture.loadImage(emptyScreenImage);
    }
    checkGLErrors(__FILE__, __LINE__);
    Texture texture = {screenOpenGLTexture.getTextureID(), screenTextureImage.getWidth(), screenTextureImage.getHeight()};

    if (screenOverlay == ScreenOverlay::Graticule || screenOverlay == ScreenOverlay::SmudgedGraticule) {
        activateTargetTexture(texture);
        checkGLErrors(__FILE__, __LINE__);
        setNormalBlending();
        checkGLErrors(__FILE__, __LINE__);
        setShader(simpleShader.get());
        checkGLErrors(__FILE__, __LINE__);
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
            if (static_cast<int>(t) % 5 == 0)
                continue;

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
        simpleShader->setUniform("colour", 0.01f, 0.05f, 0.01f, 1.0f);
        glLineWidth(4.0f);
        glDrawArrays(GL_LINES, 0, data.size() / 2);
        glBindTexture(GL_TEXTURE_2D, targetTexture.value().id);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }

    return texture;
}

void VisualiserRenderer::checkGLErrors(juce::String file, int line) {
    using namespace juce::gl;

    GLenum error;
    while ((error = glGetError()) != GL_NO_ERROR) {
        juce::String errorMessage;
        switch (error) {
            case GL_INVALID_ENUM:
                errorMessage = "GL_INVALID_ENUM";
                break;
            case GL_INVALID_VALUE:
                errorMessage = "GL_INVALID_VALUE";
                break;
            case GL_INVALID_OPERATION:
                errorMessage = "GL_INVALID_OPERATION";
                break;
            case GL_STACK_OVERFLOW:
                errorMessage = "GL_STACK_OVERFLOW";
                break;
            case GL_STACK_UNDERFLOW:
                errorMessage = "GL_STACK_UNDERFLOW";
                break;
            case GL_OUT_OF_MEMORY:
                errorMessage = "GL_OUT_OF_MEMORY";
                break;
            case GL_INVALID_FRAMEBUFFER_OPERATION:
                errorMessage = "GL_INVALID_FRAMEBUFFER_OPERATION";
                break;
            default:
                errorMessage = "Unknown OpenGL error";
                break;
        }
        DBG("OpenGL error at " + file + ":" + juce::String(line) + " - " + errorMessage);
    }
}

void VisualiserRenderer::renderScope(const std::vector<float> &xPoints, const std::vector<float> &yPoints, const std::vector<float> &zPoints) {
    if (screenOverlay != parameters.getScreenOverlay()) {
        screenOverlay = parameters.getScreenOverlay();
#if OSCI_PREMIUM
        reflectionTexture = createReflectionTexture();
#endif
        screenTexture = createScreenTexture();
    }

    if (sampleRate != oldSampleRate || scratchVertices.empty()) {
        oldSampleRate = sampleRate;
        setupArrays(RESAMPLE_RATIO * sampleRate / frameRate);
    }

    renderScale = (float)openGLContext.getRenderingScale();

    drawLineTexture(xPoints, yPoints, zPoints);
    checkGLErrors(__FILE__, __LINE__);
    drawCRT();
    checkGLErrors(__FILE__, __LINE__);
}
