#include "../LookAndFeel.h"
#include "VisualiserOpenGLComponent.h"


VisualiserOpenGLComponent::VisualiserOpenGLComponent(VisualiserSettings& settings) : settings(settings) {
    openGLContext.setRenderer(this);
    openGLContext.attachTo(*this);
    openGLContext.setContinuousRepainting(true);
}

void VisualiserOpenGLComponent::newOpenGLContextCreated() {
    using namespace juce::gl;
    
    juce::OpenGLHelpers::clear(juce::Colours::black);
    glColorMask(true, true, true, true);

    glViewport(0, 0, getWidth(), getHeight());
    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    
    fullScreenQuad = { -1.0f, 1.0f, 1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f, -1.0f, -1.0f, -1.0f };
    
    simpleShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    simpleShader->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(R""""(
        attribute vec2 vertexPosition;

        void main() {
            gl_Position = vec4(vertexPosition, 0.0, 1.0);
        }
    )""""));
    simpleShader->addFragmentShader(R""""(
        uniform vec4 colour;

        void main() {
            gl_FragColor = colour;
        }
    )"""");
    simpleShader->link();
    
    lineShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    lineShader->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(R""""(
        #define EPS 1E-6

        uniform float uInvert;
        uniform float uSize;
        uniform float uNEdges;
        uniform float uFadeAmount;
        uniform float uIntensity;
        uniform float uGain;
        attribute vec3 aStart, aEnd;
        attribute float aIdx;
        varying vec4 uvl;
        varying vec2 vTexCoord;
        varying float vLen;
        varying float vSize;

        void main () {
            float tang;
            vec2 current;
            // All points in quad contain the same data:
            // segment start point and segment end point.
            // We determine point position using its index.
            float idx = mod(aIdx,4.0);
            
            vec2 aStartPos = aStart.xy;
            vec2 aEndPos = aEnd.xy;
            float aStartBrightness = aStart.z;
            float aEndBrightness = aEnd.z;
            
            // `dir` vector is storing the normalized difference
            // between end and start
            vec2 dir = (aEndPos-aStartPos)*uGain;
            uvl.z = length(dir);
            
            if (uvl.z > EPS) {
                dir = dir / uvl.z;
            } else {
                // If the segment is too short, just draw a square
                dir = vec2(1.0, 0.0);
            }
            
            vSize = uSize;
            float intensity = 0.015 * uIntensity / uSize;
            vec2 norm = vec2(-dir.y, dir.x);
            
            if (idx >= 2.0) {
                current = aEndPos*uGain;
                tang = 1.0;
                uvl.x = -vSize;
                uvl.w = aEndBrightness;
            } else {
                current = aStartPos*uGain;
                tang = -1.0;
                uvl.x = uvl.z + vSize;
                uvl.w = aStartBrightness;
            }
            // `side` corresponds to shift to the "right" or "left"
            float side = (mod(idx, 2.0)-0.5)*2.0;
            uvl.y = side * vSize;
            
            uvl.w *= intensity * mix(1.0-uFadeAmount, 1.0, floor(aIdx / 4.0 + 0.5)/uNEdges);
                                     
            vec4 pos = vec4((current+(tang*dir+norm*side)*vSize)*uInvert,0.0,1.0);
            gl_Position = pos;
            vTexCoord = 0.5*pos.xy+0.5;
            //float seed = floor(aIdx/4.0);
            //seed = mod(sin(seed*seed), 7.0);
            //if (mod(seed/2.0, 1.0)<0.5) gl_Position = vec4(10.0);
        }
    )""""));
    lineShader->addFragmentShader(R""""(
        #define EPS 1E-6
        #define TAU 6.283185307179586
        #define TAUR 2.5066282746310002
        #define SQRT2 1.4142135623730951
    
        uniform float uSize;
        uniform float uIntensity;
        uniform sampler2D uScreen;
        varying float vSize;
        varying vec4 uvl;
        varying vec2 vTexCoord;
        
        // A standard gaussian function, used for weighting samples
        float gaussian(float x, float sigma) {
            return exp(-(x * x) / (2.0 * sigma * sigma)) / (TAUR * sigma);
        }
                       
        // This approximates the error function, needed for the gaussian integral
        float erf(float x) {
            float s = sign(x), a = abs(x);
            x = 1.0 + (0.278393 + (0.230389 + 0.078108 * (a * a)) * a) * a;
            x *= x;
            return s - s / (x * x);
        }
                       
                       
        void main() {
            float len = uvl.z;
            vec2 xy = uvl.xy;
            float brightness;
            
            float sigma = vSize/5.0;
            if (len < EPS) {
                // If the beam segment is too short, just calculate intensity at the position.
                brightness = gaussian(length(xy), sigma);
            } else {
                // Otherwise, use analytical integral for accumulated intensity.
                brightness = erf(xy.x/SQRT2/sigma) - erf((xy.x-len)/SQRT2/sigma);
                brightness *= exp(-xy.y*xy.y/(2.0*sigma*sigma))/2.0/len;
            }
                                      
            brightness *= uvl.w;
            gl_FragColor = 2.0 * texture2D(uScreen, vTexCoord) * brightness;
            gl_FragColor.a = 1.0;
        }
    )"""");
    lineShader->link();
    
    outputShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    outputShader->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(R""""(
        attribute vec2 aPos;
        varying vec2 vTexCoord;
        varying vec2 vTexCoordCanvas;
        uniform float uResizeForCanvas;
    
        void main() {
            gl_Position = vec4(aPos, 0.0, 1.0);
            vTexCoord = (0.5 * aPos + 0.5);
            vTexCoordCanvas = vTexCoord * uResizeForCanvas;
        }
    )""""));
    outputShader->addFragmentShader(R""""(
        uniform sampler2D uTexture0; //line
        uniform sampler2D uTexture1; //tight glow
        uniform sampler2D uTexture2; //big glow
        uniform sampler2D uTexture3; //screen
        uniform float uExposure;
        uniform float uSaturation;
        uniform vec3 uColour;
        varying vec2 vTexCoord;
        varying vec2 vTexCoordCanvas;
        
        vec3 desaturate(vec3 color, float factor) {
            vec3 lum = vec3(0.299, 0.587, 0.114);
            vec3 gray = vec3(dot(lum, color));
            return vec3(mix(color, gray, factor));
        }
        
        /* Gradient noise from Jorge Jimenez's presentation: */
        /* http://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare */
        float gradientNoise(in vec2 uv) {
            return fract(52.9829189 * fract(dot(uv, vec2(0.06711056, 0.00583715))));
        }
        
        void main() {
            vec4 line = texture2D(uTexture0, vTexCoordCanvas);
            // r components have grid; g components do not.
            vec4 screen = texture2D(uTexture3, vTexCoord);
            vec4 tightGlow = texture2D(uTexture1, vTexCoord);
            vec4 scatter = texture2D(uTexture2, vTexCoord)+0.35;
            float light = line.r + 1.5*screen.g*screen.g*tightGlow.r;
            light += 0.4*scatter.g * (2.0+1.0*screen.g + 0.5*screen.r);
            float tlight = 1.0-pow(2.0, -uExposure*light);
            float tlight2 = tlight*tlight*tlight;
            gl_FragColor.rgb = mix(uColour, vec3(1.0), 0.3+tlight2*tlight2*0.5)*tlight;
            gl_FragColor.rgb = desaturate(gl_FragColor.rgb, 1.0 - uSaturation);
            gl_FragColor.rgb += (1.0 / 255.0) * gradientNoise(gl_FragCoord.xy) - (0.5 / 255.0);
            gl_FragColor.a = 1.0;
        }
    )"""");
    outputShader->link();
    
    texturedShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    texturedShader->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(R""""(
        attribute vec2 aPos;
        varying vec2 vTexCoord;
        uniform float uResizeForCanvas;
    
        void main() {
            gl_Position = vec4(aPos, 0.0, 1.0);
            vTexCoord = (0.5 * aPos + 0.5) * uResizeForCanvas;
        }
    )""""));
    texturedShader->addFragmentShader(R""""(
        uniform sampler2D uTexture0;
        varying vec2 vTexCoord;
    
        void main() {
            gl_FragColor = texture2D(uTexture0, vTexCoord);
            gl_FragColor.a = 1.0;
        }
    )"""");
    texturedShader->link();
    
    blurShader = std::make_unique<juce::OpenGLShaderProgram>(openGLContext);
    blurShader->addVertexShader(juce::OpenGLHelpers::translateVertexShaderToV3(R""""(
        attribute vec2 aPos;
        varying vec2 vTexCoord;
    
        void main() {
            gl_Position = vec4(aPos, 0.0, 1.0);
            vTexCoord = (0.5*aPos+0.5);
        }
    )""""));
    blurShader->addFragmentShader(R""""(
        uniform sampler2D uTexture0;
        uniform vec2 uOffset;
        varying vec2 vTexCoord;
    
        void main() {
            vec4 sum = vec4(0.0);
            sum += texture2D(uTexture0, vTexCoord - uOffset*8.0) * 0.000078;
            sum += texture2D(uTexture0, vTexCoord - uOffset*7.0) * 0.000489;
            sum += texture2D(uTexture0, vTexCoord - uOffset*6.0) * 0.002403;
            sum += texture2D(uTexture0, vTexCoord - uOffset*5.0) * 0.009245;
            sum += texture2D(uTexture0, vTexCoord - uOffset*4.0) * 0.027835;
            sum += texture2D(uTexture0, vTexCoord - uOffset*3.0) * 0.065592;
            sum += texture2D(uTexture0, vTexCoord - uOffset*2.0) * 0.12098;
            sum += texture2D(uTexture0, vTexCoord - uOffset*1.0) * 0.17467;
            sum += texture2D(uTexture0, vTexCoord + uOffset*0.0) * 0.19742;
            sum += texture2D(uTexture0, vTexCoord + uOffset*1.0) * 0.17467;
            sum += texture2D(uTexture0, vTexCoord + uOffset*2.0) * 0.12098;
            sum += texture2D(uTexture0, vTexCoord + uOffset*3.0) * 0.065592;
            sum += texture2D(uTexture0, vTexCoord + uOffset*4.0) * 0.027835;
            sum += texture2D(uTexture0, vTexCoord + uOffset*5.0) * 0.009245;
            sum += texture2D(uTexture0, vTexCoord + uOffset*6.0) * 0.002403;
            sum += texture2D(uTexture0, vTexCoord + uOffset*7.0) * 0.000489;
            sum += texture2D(uTexture0, vTexCoord + uOffset*8.0) * 0.000078;
            gl_FragColor = sum;
        }
    )"""");
    blurShader->link();
    
    glGenBuffers(1, &vertexBuffer);
    setupTextures();
    
    setupArrays(1024);
}

void VisualiserOpenGLComponent::renderOpenGL() {
    if (openGLContext.isActive()) {
        xSamples.clear();
        ySamples.clear();
        zSamples.clear();
        for (int i = 0; i < 1024; i++) {
            xSamples.push_back(std::sin(i * 0.1));
            ySamples.push_back(std::cos(i * 0.1));
            zSamples.push_back(1);
        }
        
        drawLineTexture(xSamples, ySamples, zSamples);
        drawCRT();
    }
}

void VisualiserOpenGLComponent::openGLContextClosing() {}

void VisualiserOpenGLComponent::resized() {
    using namespace juce::gl;
    
    if (openGLContext.isAttached()) {
        glViewport(0, 0, getWidth(), getHeight());
    }
}

void VisualiserOpenGLComponent::setupArrays(int nPoints) {
    using namespace juce::gl;
    
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

    // Load the screen texture from file
    juce::Image screenTextureImage = juce::ImageFileFormat::loadFrom(BinaryData::noise_jpg, BinaryData::noise_jpgSize);
    if (screenTextureImage.isValid()) {
        screenOpenGLTexture.loadImage(screenTextureImage);
        screenTexture = { screenOpenGLTexture.getTextureID(), screenTextureImage.getWidth(), screenTextureImage.getHeight() };
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0); // Unbind
}

Texture VisualiserOpenGLComponent::makeTexture(int width, int height) {
    using namespace juce::gl;
    
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);

    // Set texture filtering and wrapping
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenerateMipmap(GL_TEXTURE_2D);
    
    glBindTexture(GL_TEXTURE_2D, 0); // Unbind
    
    return { textureID, width, height };
}

void VisualiserOpenGLComponent::drawLineTexture(std::vector<float>& xPoints, std::vector<float>& yPoints, std::vector<float>& zPoints) {
    using namespace juce::gl;
    
    float persistence = settings.parameters.persistenceEffect->getActualValue() - 1.33;
    fadeAmount = juce::jmin(1.0, std::pow(0.5, persistence) * 0.4);
    activateTargetTexture(lineTexture);
    checkGLErrors("activateTargetTexture - lineTexture");
    fade();
    drawLine(xPoints, yPoints, zPoints);
    glBindTexture(GL_TEXTURE_2D, targetTexture.value().id);
    glGenerateMipmap(GL_TEXTURE_2D);
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
        checkGLErrors("activateTargetTexture - glBindFramebuffer");
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture.value().id, 0);
        checkGLErrors("activateTargetTexture - glFramebufferTexture2D");
        glViewport(0, 0, texture.value().width, texture.value().height);
        checkGLErrors("activateTargetTexture - glViewport");
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, getWidth(), getHeight());
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
        glGenerateMipmap(GL_TEXTURE_2D);
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

void VisualiserOpenGLComponent::drawLine(std::vector<float>& xPoints, std::vector<float>& yPoints, std::vector<float>& zPoints) {
    using namespace juce::gl;
    
    setAdditiveBlending();
    checkGLErrors("drawLine - setAdditiveBlending");

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
    checkGLErrors("drawLine - glBindBuffer");

    lineShader->use();
    glEnableVertexAttribArray(glGetAttribLocation(lineShader->getProgramID(), "aStart"));
    glEnableVertexAttribArray(glGetAttribLocation(lineShader->getProgramID(), "aEnd"));
    glEnableVertexAttribArray(glGetAttribLocation(lineShader->getProgramID(), "aIdx"));
    checkGLErrors("drawLine - glEnableVertexAttribArray");

    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    glVertexAttribPointer(glGetAttribLocation(lineShader->getProgramID(), "aStart"), 3, GL_FLOAT, GL_FALSE, 0, 0);
    glVertexAttribPointer(glGetAttribLocation(lineShader->getProgramID(), "aEnd"), 3, GL_FLOAT, GL_FALSE, 0, (void*)(12 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, quadIndexBuffer);
    glVertexAttribPointer(glGetAttribLocation(lineShader->getProgramID(), "aIdx"), 1, GL_FLOAT, GL_FALSE, 0, 0);
    checkGLErrors("drawLine - glVertexAttribPointer");

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, screenTexture.id);
    lineShader->setUniform("uScreen", 0);
    checkGLErrors("drawLine - glBindTexture");

    float focus = settings.parameters.focusEffect->getActualValue() / 100;
    lineShader->setUniform("uSize", focus);
    // TODO: change gain
    double mainGain = 0;
    lineShader->setUniform("uGain", (GLfloat) (pow(2.0, mainGain) * 450.0 / 512.0));
    bool invertXY = false;
    lineShader->setUniform("uInvert", invertXY ? -1.0f : 1.0f);
    checkGLErrors("drawLine - setUniform - uSize");

    // TODO: integrate sampleRate
    int sampleRate = 192000;
    float modifiedIntensity = settings.parameters.intensityEffect->getActualValue() / 100;
    float intensity = modifiedIntensity * (41000.0f / sampleRate);
    if (settings.parameters.upsamplingEnabled->getBoolValue()) {
        lineShader->setUniform("uIntensity", intensity);
    } else {
        // TODO: filter steps
        int steps = 6;
        lineShader->setUniform("uIntensity", (GLfloat) (intensity * (steps + 1.5)));
    }
    checkGLErrors("drawLine - setUniform - uIntensity");
    
    lineShader->setUniform("uFadeAmount", fadeAmount);
    lineShader->setUniform("uNEdges", (GLfloat) nEdges);
    checkGLErrors("drawLine - setUniform - uFadeAmount");

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vertexIndexBuffer);
    int nEdgesThisTime = xPoints.size() - 1;
    glDrawElements(GL_TRIANGLES, nEdgesThisTime, GL_UNSIGNED_SHORT, 0);
    checkGLErrors("drawLine - glDrawElements");

    glDisableVertexAttribArray(glGetAttribLocation(lineShader->getProgramID(), "aStart"));
    glDisableVertexAttribArray(glGetAttribLocation(lineShader->getProgramID(), "aEnd"));
    glDisableVertexAttribArray(glGetAttribLocation(lineShader->getProgramID(), "aIdx"));
    checkGLErrors("drawLine - glDisableVertexAttribArray");
}

void VisualiserOpenGLComponent::fade() {
    using namespace juce::gl;
    
    setNormalBlending();
    checkGLErrors("fade - setNormalBlending");
    
    simpleShader->use();
    checkGLErrors("fade - simpleShader->use");
    glEnableVertexAttribArray(glGetAttribLocation(simpleShader->getProgramID(), "vertexPosition"));
    checkGLErrors("fade - glEnableVertexAttribArray");
    glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer);
    checkGLErrors("fade - glBindBuffer");
    glBufferData(GL_ARRAY_BUFFER, sizeof(float) * fullScreenQuad.size(), fullScreenQuad.data(), GL_STATIC_DRAW);
    checkGLErrors("fade - glBufferData");
    glVertexAttribPointer(glGetAttribLocation(simpleShader->getProgramID(), "vertexPosition"), 2, GL_FLOAT, GL_FALSE, 0, 0);
    checkGLErrors("fade - glVertexAttribPointer");
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    checkGLErrors("fade - glBindBuffer");
    
    simpleShader->setUniform("colour", 0.0f, 0.0f, 0.0f, fadeAmount);
    checkGLErrors("fade - setUniform");
    glDrawArrays(GL_TRIANGLES, 0, 6);
    checkGLErrors("fade - glDrawArrays");
    glDisableVertexAttribArray(glGetAttribLocation(simpleShader->getProgramID(), "vertexPosition"));
    checkGLErrors("fade - glDisableVertexAttribArray");
}

void VisualiserOpenGLComponent::drawCRT() {
    setNormalBlending();
    
    activateTargetTexture(blur1Texture);
    checkGLErrors("drawCRT - activateTargetTexture - blur1Texture");
    setShader(texturedShader.get());
    checkGLErrors("drawCRT - setShader - texturedShader");
    texturedShader->setUniform("uResizeForCanvas", lineTexture.width / 1024.0f);
    checkGLErrors("drawCRT - texturedShader - uResizeForCanvas");
    drawTexture(lineTexture);
    checkGLErrors("drawCRT - drawTexture - lineTexture");
    
    //horizontal blur 256x256
    activateTargetTexture(blur2Texture);
    setShader(blurShader.get());
    blurShader->setUniform("uOffset", 1.0f / 256.0f, 0.0f);
    drawTexture(blur1Texture);
    checkGLErrors("drawCRT - blurShader");
    
    //vertical blur 256x256
    activateTargetTexture(blur1Texture);
    blurShader->setUniform("uOffset", 0.0f, 1.0f / 256.0f);
    drawTexture(blur2Texture);
    checkGLErrors("drawCRT - blurShader");
    
    //preserve blur1 for later
    activateTargetTexture(blur3Texture);
    checkGLErrors("drawCRT - activateTargetTexture - blur3Texture");
    setShader(texturedShader.get());
    checkGLErrors("drawCRT - setShader - texturedShader");
    texturedShader->setUniform("uResizeForCanvas", 1.0f);
    checkGLErrors("drawCRT - texturedShader - uResizeForCanvas");
    drawTexture(blur1Texture);
    checkGLErrors("drawCRT - drawTexture - blur1Texture");
    
    //horizontal blur 64x64
    activateTargetTexture(blur4Texture);
    setShader(blurShader.get());
    blurShader->setUniform("uOffset", 1.0f / 32.0f, 1.0f / 60.0f);
    drawTexture(blur3Texture);
    checkGLErrors("drawCRT - blurShader - blur3Texture");
    
    
    //vertical blur 64x64
    activateTargetTexture(blur3Texture);
    blurShader->setUniform("uOffset", -1.0f / 60.0f, 1.0f / 32.0f);
    drawTexture(blur4Texture);
    checkGLErrors("drawCRT - blurShader - blur4Texture");
    
    activateTargetTexture(std::nullopt);
    setShader(outputShader.get());
    float brightness = std::pow(2, settings.parameters.brightnessEffect->getActualValue() - 4);
    outputShader->setUniform("uExposure", brightness);
    outputShader->setUniform("uSaturation", (float) settings.parameters.saturationEffect->getActualValue());
    outputShader->setUniform("uResizeForCanvas", lineTexture.width / 1024.0f);
    juce::Colour colour = juce::Colour::fromHSV(settings.parameters.hueEffect->getActualValue() / 360.0f, 1.0, 1.0, 1.0);
    outputShader->setUniform("uColour", colour.getFloatRed(), colour.getFloatGreen(), colour.getFloatBlue());
    drawTexture(lineTexture, blur1Texture, blur3Texture, screenTexture);
    checkGLErrors("drawCRT - outputShader");
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
