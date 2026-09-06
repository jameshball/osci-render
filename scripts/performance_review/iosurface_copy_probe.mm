// Standalone macOS transfer check; CPU readback here is validation only.
// clang++ -Wno-deprecated-declarations iosurface_copy_probe.mm -framework Foundation -framework IOSurface -framework OpenGL -o /tmp/iosurface_copy_probe
#import <Foundation/Foundation.h>
#import <IOSurface/IOSurface.h>
#import <OpenGL/OpenGL.h>
#import <OpenGL/gl3.h>
#include <vector>
#include <cstdio>
#include <cstdlib>
int main(int argc, char** argv) {
    @autoreleasepool {
        CGLPixelFormatAttribute attributes[] = {kCGLPFAAccelerated, (CGLPixelFormatAttribute)0};
        CGLPixelFormatObj format = nullptr; GLint count = 0; CGLContextObj context = nullptr;
        if (CGLChoosePixelFormat(attributes, &format, &count) != kCGLNoError || CGLCreateContext(format, nullptr, &context) != kCGLNoError) { return 1; }
        CGLDestroyPixelFormat(format); CGLSetCurrentContext(context);
        const int width = argc > 1 ? std::atoi(argv[1]) : 128, height = argc > 2 ? std::atoi(argv[2]) : 256;
        std::vector<float> input(width * height * 4);
        for (int i = 0; i < width * height * 4; ++i) { input[i] = float((i * 37 + i / 7) % 256) / 255.0f; }
        GLuint source = 0, target = 0, read = 0, draw = 0;
        glGenTextures(1, &source); glBindTexture(GL_TEXTURE_2D, source);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, input.data());
        NSDictionary* props = @{(id)kIOSurfaceWidth:@(width), (id)kIOSurfaceHeight:@(height), (id)kIOSurfaceBytesPerElement:@4, (id)kIOSurfacePixelFormat:@((uint32_t)'BGRA')};
        IOSurfaceRef surface = IOSurfaceCreate((CFDictionaryRef)props);
        if (surface == nullptr) { return 2; }
        glGenTextures(1, &target); glBindTexture(GL_TEXTURE_RECTANGLE, target);
        auto status = CGLTexImageIOSurface2D(context, GL_TEXTURE_RECTANGLE, GL_RGBA8, width, height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, surface, 0);
        if (status != kCGLNoError) { return 2; }
        glGenFramebuffers(1, &read); glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, source, 0);
        glGenFramebuffers(1, &draw); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE, target, 0);
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { return 3; }
        glBlitFramebuffer(0, 0, width, height, 0, height, width, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE); glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT); glFinish();
        std::vector<unsigned char> baseline(width * height * 4);
        glBindTexture(GL_TEXTURE_2D, source); glGetTexImage(GL_TEXTURE_2D, 0, GL_BGRA, GL_UNSIGNED_BYTE, baseline.data());
        IOSurfaceLock(surface, kIOSurfaceLockReadOnly, nullptr);
        const auto* raw = static_cast<const unsigned char*>(IOSurfaceGetBaseAddress(surface));
        const auto stride = IOSurfaceGetBytesPerRow(surface);
        int memoryMismatches = 0;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                for (int c = 0; c < 4; ++c) {
                    const auto expected = c == 3 ? 255 : baseline[((height - 1 - y) * width + x) * 4 + c];
                    if (raw[y * stride + x * 4 + c] != expected) { ++memoryMismatches; }
                }
            }
        }
        IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, nullptr);
        const auto error = glGetError();
        printf("%dx%d: %d physical surface pixels checked, %d channel mismatches, GL error %u\n", width, height, width * height, memoryMismatches, error);
        glDeleteFramebuffers(1, &read); glDeleteFramebuffers(1, &draw); glDeleteTextures(1, &source); glDeleteTextures(1, &target);
        CFRelease(surface); CGLSetCurrentContext(nullptr); CGLDestroyContext(context);
        return memoryMismatches != 0 || error != 0;
    }
}
