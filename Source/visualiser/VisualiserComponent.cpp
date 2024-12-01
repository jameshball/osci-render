#include "../LookAndFeel.h"
#include "VisualiserComponent.h"
#include "BlurFragmentShader.glsl"
#include "BlurVertexShader.glsl"
#include "LineFragmentShader.glsl"
#include "LineVertexShader.glsl"
#include "OutputFragmentShader.glsl"
#include "OutputVertexShader.glsl"
#include "SimpleFragmentShader.glsl"
#include "SimpleVertexShader.glsl"
#include "TexturedFragmentShader.glsl"
#include "TexturedVertexShader.glsl"

VisualiserComponent::VisualiserComponent(juce::File ffmpegFile, std::function<void()>& haltRecording, AudioBackgroundThreadManager& threadManager, VisualiserSettings& settings, VisualiserComponent* parent, bool visualiserOnly) : ffmpegFile(ffmpegFile), haltRecording(haltRecording), settings(settings), threadManager(threadManager), visualiserOnly(visualiserOnly), AudioBackgroundThread("VisualiserComponent" + juce::String(parent != nullptr ? " Child" : ""), threadManager), parent(parent) {
    addAndMakeVisible(ffmpegDownloader);
    
    ffmpegDownloader.onSuccessfulDownload = [this] {
        juce::MessageManager::callAsync([this] {
            record.setEnabled(true);
        });
    };
    
    haltRecording = [this] {
        setRecording(false);
    };
    
    addAndMakeVisible(record);
    record.setPulseAnimation(true);
    record.onClick = [this] {
        setRecording(record.getToggleState());
    };
    
    addAndMakeVisible(stopwatch);
    
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setWantsKeyboardFocus(true);
    
    if (parent == nullptr && !visualiserOnly) {
        addAndMakeVisible(fullScreenButton);
    }
    if (child == nullptr && parent == nullptr && !visualiserOnly) {
        addAndMakeVisible(popOutButton);
    }
    addAndMakeVisible(settingsButton);
    
    fullScreenButton.onClick = [this]() {
        enableFullScreen();
    };
    
    settingsButton.onClick = [this]() {
        openSettings();
    };
    
    popOutButton.onClick = [this]() {
        popoutWindow();
    };

    setFullScreen(false);
    
    openGLContext.setRenderer(this);
    openGLContext.attachTo(*this);

    setShouldBeRunning(true);
}

VisualiserComponent::~VisualiserComponent() {
    setRecording(false);
    if (parent == nullptr) {
        haltRecording = nullptr;
    }
    openGLContext.detach();
    setShouldBeRunning(false, [this] {
        renderingSemaphore.release();
    });
}

void VisualiserComponent::setFullScreenCallback(std::function<void(FullScreenMode)> callback) {
    fullScreenCallback = callback;
}

void VisualiserComponent::enableFullScreen() {
    if (fullScreenCallback) {
        fullScreenCallback(FullScreenMode::TOGGLE);
    }
    grabKeyboardFocus();
}

void VisualiserComponent::mouseDoubleClick(const juce::MouseEvent& event) {
    enableFullScreen();
}

void VisualiserComponent::runTask(const std::vector<OsciPoint>& points) {
    {
        juce::CriticalSection::ScopedLockType lock(samplesLock);
        
        xSamples.clear();
        ySamples.clear();
        zSamples.clear();
        for (auto& point : points) {
            OsciPoint smoothPoint = settings.parameters.smoothEffect->apply(0, point);
            xSamples.push_back(smoothPoint.x);
            ySamples.push_back(smoothPoint.y);
            zSamples.push_back(smoothPoint.z);
        }
        
        sampleBufferCount++;
        
        if (settings.parameters.upsamplingEnabled->getBoolValue()) {
            int newResampledSize = xSamples.size() * RESAMPLE_RATIO;
            
            smoothedXSamples.resize(newResampledSize);
            smoothedYSamples.resize(newResampledSize);
            smoothedZSamples.resize(newResampledSize);
            smoothedZSamples.resize(newResampledSize);
            
            xResampler.process(xSamples.data(), smoothedXSamples.data(), xSamples.size());
            yResampler.process(ySamples.data(), smoothedYSamples.data(), ySamples.size());
            zResampler.process(zSamples.data(), smoothedZSamples.data(), zSamples.size());
        }
    }
    
    // this just triggers a repaint
    triggerAsyncUpdate();
    // wait for rendering to complete
    renderingSemaphore.acquire();
}

int VisualiserComponent::prepareTask(double sampleRate, int bufferSize) {
    this->sampleRate = sampleRate;
    xResampler.prepare(sampleRate, RESAMPLE_RATIO);
    yResampler.prepare(sampleRate, RESAMPLE_RATIO);
    zResampler.prepare(sampleRate, RESAMPLE_RATIO);
    
    int desiredBufferSize = sampleRate / FRAME_RATE;
    
    return desiredBufferSize;
}

void VisualiserComponent::stopTask() {
    setRecording(false);
    renderingSemaphore.release();
}

void VisualiserComponent::setPaused(bool paused) {
    active = !paused;
    renderingSemaphore.release();
    setShouldBeRunning(active);
    repaint();
}

void VisualiserComponent::mouseDown(const juce::MouseEvent& event) {
    if (event.mods.isLeftButtonDown() && child == nullptr && !record.getToggleState()) {
        setPaused(active);
    }
}

bool VisualiserComponent::keyPressed(const juce::KeyPress& key) {
    if (key.isKeyCode(juce::KeyPress::escapeKey)) {
        if (fullScreenCallback) {
            fullScreenCallback(FullScreenMode::MAIN_COMPONENT);
        }
        return true;
    }

    return false;
}

void VisualiserComponent::setFullScreen(bool fullScreen) {}

void VisualiserComponent::setRecording(bool recording) {
    stopwatch.stop();
    stopwatch.reset();

    if (recording) {
        if (!ffmpegFile.exists()) {
            // ask the user if they want to download ffmpeg
            juce::MessageBoxOptions options = juce::MessageBoxOptions()
                .withTitle("Recording requires FFmpeg")
                .withMessage("FFmpeg is required to record video to .mp4.\n\nWould you like to download it now?")
                .withButton("Yes")
                .withButton("No")
                .withIconType(juce::AlertWindow::QuestionIcon)
                .withAssociatedComponent(this);
            
            juce::AlertWindow::showAsync(options, [this](int result) {
                if (result == 1) {
                    record.setEnabled(false);
                    ffmpegDownloader.download();
                }
            });
            record.setToggleState(false, juce::NotificationType::dontSendNotification);
            return;
        }
        juce::TemporaryFile tempFile = juce::TemporaryFile(".mp4");
        juce::String resolution = std::to_string(renderTexture.width) + "x" + std::to_string(renderTexture.height);
        juce::String cmd = "\"" + ffmpegFile.getFullPathName() +
        "\" -r " + juce::String(FRAME_RATE) + " -f rawvideo -pix_fmt rgba -s " + resolution + " -i - -threads 0 -preset fast -y -pix_fmt yuv420p -crf " + juce::String(21) + " -vf vflip \"" + tempFile.getFile().getFullPathName() + "\"";

        // open pipe to ffmpeg's stdin in binary write mode
#if JUCE_WINDOWS
        ffmpeg = _popen(cmd.toStdString().c_str(), "wb");
#else
        ffmpeg = popen(cmd.toStdString().c_str(), "w");
#endif
        if (ffmpeg == nullptr) {
            DBG("popen failed: " + juce::String(std::strerror(errno)));
        }
        framePixels.resize(renderTexture.width * renderTexture.height * 4);
        setPaused(false);
        stopwatch.start();
    } else if (ffmpeg != nullptr) {
#if JUCE_WINDOWS
        _pclose(ffmpeg);
#else
        pclose(ffmpeg);
#endif
        ffmpeg = nullptr;
    }
    setBlockOnAudioThread(recording);
    numFrames = 0;
    record.setToggleState(recording, juce::NotificationType::dontSendNotification);
    resized();
}

void VisualiserComponent::resized() {
    auto area = getLocalBounds();
    buttonRow = area.removeFromBottom(25);
    if (parent == nullptr && !visualiserOnly) {
        fullScreenButton.setBounds(buttonRow.removeFromRight(30));
    }
    if (child == nullptr && parent == nullptr && !visualiserOnly) {
        popOutButton.setBounds(buttonRow.removeFromRight(30));
    }
    settingsButton.setBounds(buttonRow.removeFromRight(30));
    record.setBounds(buttonRow.removeFromRight(25));
    if (record.getToggleState()) {
        stopwatch.setVisible(true);
        stopwatch.setBounds(buttonRow.removeFromRight(100));
    } else {
        stopwatch.setVisible(false);
    }
    if (child == nullptr) {
        auto bounds = buttonRow.removeFromRight(160);
        ffmpegDownloader.setBounds(bounds.withSizeKeepingCentre(bounds.getWidth() - 10, bounds.getHeight() - 10));
    }
    viewportArea = area;
    viewportChanged(viewportArea);
}

void VisualiserComponent::popoutWindow() {
    setRecording(false);
    auto visualiser = new VisualiserComponent(ffmpegFile, haltRecording, threadManager, settings, this);
    visualiser->settings.setLookAndFeel(&getLookAndFeel());
    visualiser->openSettings = openSettings;
    visualiser->closeSettings = closeSettings;
    child = visualiser;
    childUpdated();
    visualiser->setSize(300, 325);
    popout = std::make_unique<VisualiserWindow>("Software Oscilloscope", this);
    popout->setContentOwned(visualiser, true);
    popout->setUsingNativeTitleBar(true);
    popout->setResizable(true, false);
    popout->setVisible(true);
    popout->centreWithSize(300, 325);
    setPaused(true);
    resized();
}

void VisualiserComponent::childUpdated() {
    popOutButton.setVisible(child == nullptr);
    ffmpegDownloader.setVisible(child == nullptr);
    record.setVisible(child == nullptr);
    if (child != nullptr) {
        haltRecording = [this] {
            setRecording(false);
            child->setRecording(false);
        };
    } else {
        haltRecording = [this] {
            setRecording(false);
        };
    }
}

void VisualiserComponent::newOpenGLContextCreated() {
    using namespace juce::gl;
    
    juce::CriticalSection::ScopedLockType lock(samplesLock);
    
    juce::OpenGLHelpers::clear(juce::Colours::black);
    glColorMask(true, true, true, true);

    viewportChanged(viewportArea);
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    
    fullScreenQuad = { -1.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, -1.0f };
    
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
    outputShader->link();
    
    texturedShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    texturedShader->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(texturedVertexShader));
    texturedShader->addFragmentShader(texturedFragmentShader);
    texturedShader->link();
    
    blurShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    blurShader->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(blurVertexShader));
    blurShader->addFragmentShader(blurFragmentShader);
    blurShader->link();
    
    glGenBuffers(1, &vertexBuffer);
    glGenBuffers(1, &quadIndexBuffer);
    glGenBuffers(1, &vertexIndexBuffer);
    
    setupTextures();
}

void VisualiserComponent::openGLContextClosing() {
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
    
    simpleShader.reset();
    texturedShader.reset();
    blurShader.reset();
    lineShader.reset();
    outputShader.reset();
}

void VisualiserComponent::handleAsyncUpdate() {
    repaint();
}

void VisualiserComponent::renderOpenGL() {
    using namespace juce::gl;
    
    if (openGLContext.isActive()) {
        juce::OpenGLHelpers::clear(juce::Colours::black);
        
        // we have a new buffer to render
        if (sampleBufferCount != prevSampleBufferCount) {
            prevSampleBufferCount = sampleBufferCount;
            juce::CriticalSection::ScopedLockType lock(samplesLock);
            
            if (settings.parameters.upsamplingEnabled->getBoolValue()) {
                renderScope(smoothedXSamples, smoothedYSamples, smoothedZSamples);
            } else {
                renderScope(xSamples, ySamples, zSamples);
            }
            
            if (record.getToggleState()) {
                // draw frame to ffmpeg
                glBindTexture(GL_TEXTURE_2D, renderTexture.id);
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, framePixels.data());
                fwrite(framePixels.data(), 4 * renderTexture.width * renderTexture.height, 1, ffmpeg);
            }
            
            renderingSemaphore.release();
            stopwatch.addTime(juce::RelativeTime::seconds(1.0 / FRAME_RATE));
        }
        
        // render texture to screen
        activateTargetTexture(std::nullopt);
        setShader(texturedShader.get());
        drawTexture(renderTexture);
    }
}

void VisualiserComponent::viewportChanged(juce::Rectangle<int> area) {
    using namespace juce::gl;
    
    if (openGLContext.isAttached()) {
        float realWidth = area.getWidth() * renderScale;
        float realHeight = area.getHeight() * renderScale;
        
        float xOffset = getWidth() * renderScale - realWidth;
        float yOffset = getHeight() * renderScale - realHeight;
        
        float minDim = juce::jmin(realWidth, realHeight);
        float x = (realWidth - minDim) / 2 + area.getX() * renderScale + xOffset;
        float y = (realHeight - minDim) / 2 - area.getY() * renderScale + yOffset;
        
        glViewport(juce::roundToInt(x), juce::roundToInt(y), juce::roundToInt(minDim), juce::roundToInt(minDim));
    }
}

void VisualiserComponent::setupArrays(int nPoints) {
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

void VisualiserComponent::setupTextures() {
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
    renderTexture = makeTexture(1024, 1024);
    
    screenTexture = createScreenTexture();

    glBindFramebuffer(GL_FRAMEBUFFER, 0); // Unbind
}

Texture VisualiserComponent::makeTexture(int width, int height) {
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

void VisualiserComponent::drawLineTexture(const std::vector<float>& xPoints, const std::vector<float>& yPoints, const std::vector<float>& zPoints) {
    using namespace juce::gl;
    
    fadeAmount = juce::jmin(1.0, std::pow(0.5, settings.getPersistence()) * 0.4);
    activateTargetTexture(lineTexture);
    fade();
    drawLine(xPoints, yPoints, zPoints);
    glBindTexture(GL_TEXTURE_2D, targetTexture.value().id);
}

void VisualiserComponent::saveTextureToPNG(Texture texture, const juce::File& file) {
    using namespace juce::gl;
    GLuint textureID = texture.id;
    int width = texture.width;
    int height = texture.height;
    
    // Bind the texture to read its data
    glBindTexture(GL_TEXTURE_2D, textureID);

    // Read the pixels from the texture
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, framePixels.data());

    // Convert raw pixel data to JUCE Image
    juce::Image* image = new juce::Image (juce::Image::PixelFormat::ARGB, width, height, true);  // Create a JUCE image

    // Lock the image to get access to its pixel data
    juce::Image::BitmapData bitmapData(*image, juce::Image::BitmapData::writeOnly);

    // Copy the pixel data to the JUCE image (and swap R and B channels)
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int srcIndex = (y * width + x) * 4; // RGBA format
            juce::uint8 r = (framePixels)[srcIndex];     // Red
            juce::uint8 g = (framePixels)[srcIndex + 1]; // Green
            juce::uint8 b = (framePixels)[srcIndex + 2]; // Blue
            juce::uint8 a = (framePixels)[srcIndex + 3]; // Alpha

            // This method uses colors in RGBA
            bitmapData.setPixelColour(x, height-y-1, juce::Colour(r, g, b, a));
        }
    }

    // Unbind the texture
    glBindTexture(GL_TEXTURE_2D, 0);

    // Save the JUCE image to file (PNG in this case)
    juce::PNGImageFormat pngFormat;
    std::unique_ptr<juce::FileOutputStream> outputStream(file.createOutputStream());
    if (outputStream != nullptr) {
        outputStream->setPosition(0);
        pngFormat.writeImageToStream(*image, *outputStream);
        outputStream->flush();
    }
    delete image;
}

void VisualiserComponent::saveTextureToQOI(Texture texture, const juce::File& file) {
    using namespace juce::gl;
    GLuint textureID = texture.id;
    int width = texture.width;
    int height = texture.height;

    // Bind the texture to read its data
    glBindTexture(GL_TEXTURE_2D, textureID);
    // Read the pixels from the texture
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, framePixels.data());

    std::vector<unsigned char> binaryData = qoixx::qoi::encode<std::vector<unsigned char>>(framePixels, imageFormat);
    file.replaceWithData(binaryData.data(), binaryData.size());
}

void VisualiserComponent::activateTargetTexture(std::optional<Texture> texture) {
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

void VisualiserComponent::setShader(juce::OpenGLShaderProgram* program) {
    currentShader = program;
    program->use();
}

void VisualiserComponent::drawTexture(std::optional<Texture> texture0, std::optional<Texture> texture1, std::optional<Texture> texture2, std::optional<Texture> texture3) {
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

void VisualiserComponent::setAdditiveBlending() {
    using namespace juce::gl;
    
    glBlendFunc(GL_ONE, GL_ONE);
}

void VisualiserComponent::setNormalBlending() {
    using namespace juce::gl;
    
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void VisualiserComponent::drawLine(const std::vector<float>& xPoints, const std::vector<float>& yPoints, const std::vector<float>& zPoints) {
    using namespace juce::gl;
    
    setAdditiveBlending();

    int nPoints = xPoints.size();

    // Without this, there's an access violation that seems to occur only on some systems
    if (scratchVertices.size() != nPoints * 12) scratchVertices.resize(nPoints * 12);
    
    for (int i = 0; i < nPoints; ++i) {
        int p = i * 12;
        scratchVertices[p]     = scratchVertices[p + 3] = scratchVertices[p + 6] = scratchVertices[p + 9]  = xPoints[i];
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
    glVertexAttribPointer(glGetAttribLocation(lineShader->getProgramID(), "aEnd"), 3, GL_FLOAT, GL_FALSE, 0, (void*)(12 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, quadIndexBuffer);
    glVertexAttribPointer(glGetAttribLocation(lineShader->getProgramID(), "aIdx"), 1, GL_FLOAT, GL_FALSE, 0, 0);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, screenTexture.id);
    lineShader->setUniform("uScreen", 0);
    lineShader->setUniform("uSize", (GLfloat) settings.getFocus());
    lineShader->setUniform("uGain", 450.0f / 512.0f);
    lineShader->setUniform("uInvert", 1.0f);

    if (settings.getUpsamplingEnabled()) {
        lineShader->setUniform("uIntensity", intensity);
    } else {
        lineShader->setUniform("uIntensity", (GLfloat) (intensity * RESAMPLE_RATIO * 1.5));
    }
    
    lineShader->setUniform("uFadeAmount", fadeAmount);
    lineShader->setUniform("uNEdges", (GLfloat) nEdges);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vertexIndexBuffer);
    int nEdgesThisTime = xPoints.size() - 1;
    glDrawElements(GL_TRIANGLES, nEdgesThisTime * 6, GL_UNSIGNED_INT, 0);

    glDisableVertexAttribArray(glGetAttribLocation(lineShader->getProgramID(), "aStart"));
    glDisableVertexAttribArray(glGetAttribLocation(lineShader->getProgramID(), "aEnd"));
    glDisableVertexAttribArray(glGetAttribLocation(lineShader->getProgramID(), "aIdx"));
}

void VisualiserComponent::fade() {
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

void VisualiserComponent::drawCRT() {
    using namespace juce::gl;
    
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

    activateTargetTexture(renderTexture);
    setShader(outputShader.get());
    float brightness = std::pow(2, settings.getBrightness() - 2);
    outputShader->setUniform("uExposure", brightness);
    outputShader->setUniform("uSaturation", (float) settings.getSaturation());
    outputShader->setUniform("uNoise", (float) settings.getNoise());
    outputShader->setUniform("uTime", time);
    outputShader->setUniform("uGlow", (float) settings.getGlow());
    outputShader->setUniform("uResizeForCanvas", lineTexture.width / 1024.0f);
    juce::Colour colour = juce::Colour::fromHSV(settings.getHue() / 360.0f, 1.0, 1.0, 1.0);
    outputShader->setUniform("uColour", colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue());
    activateTargetTexture(renderTexture);
    drawTexture(lineTexture, blur1Texture, blur3Texture, screenTexture);
}

Texture VisualiserComponent::createScreenTexture() {
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

void VisualiserComponent::checkGLErrors(const juce::String& location) {
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


void VisualiserComponent::paint(juce::Graphics& g) {
    g.setColour(juce::Colours::black);
    g.fillRect(buttonRow);
    if (!active) {
        // draw a translucent overlay
        g.setColour(juce::Colours::black.withAlpha(0.5f));
        g.fillRect(viewportArea);
        
        g.setColour(juce::Colours::white);
        g.setFont(30.0f);
        juce::String text = child == nullptr ? "Paused" : "Open in another window";
        g.drawText(text, viewportArea, juce::Justification::centred);
    }
}

void VisualiserComponent::renderScope(const std::vector<float>& xPoints, const std::vector<float>& yPoints, const std::vector<float>& zPoints) {
    time += 0.01f;
    
    if (graticuleEnabled != settings.getGraticuleEnabled() || smudgesEnabled != settings.getSmudgesEnabled()) {
        graticuleEnabled = settings.getGraticuleEnabled();
        smudgesEnabled = settings.getSmudgesEnabled();
        screenTexture = createScreenTexture();
    }
    
    if (sampleRate != oldSampleRate) {
        oldSampleRate = sampleRate;
        setupArrays(RESAMPLE_RATIO * sampleRate / FRAME_RATE);
    }
    intensity = settings.getIntensity() * (41000.0f / sampleRate);

    renderScale = (float)openGLContext.getRenderingScale();

    drawLineTexture(xPoints, yPoints, zPoints);
    checkGLErrors("drawLineTexture");
    drawCRT();
    checkGLErrors("drawCRT");
}
