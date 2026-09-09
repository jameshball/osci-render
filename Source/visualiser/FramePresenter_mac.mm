#include "FramePresenter.h"

#if JUCE_MAC
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <IOSurface/IOSurface.h>
#import <OpenGL/OpenGL.h>
#include <array>
#include <atomic>
#if JUCE_MODULE_AVAILABLE_jucewright
#include <jucewright/jucewright.h>
#endif

@interface OsciGpuPresentationView : NSView
@end
@implementation OsciGpuPresentationView
- (BOOL)isFlipped { return YES; }
- (NSView*)hitTest:(NSPoint)point { return nil; }
@end

namespace {

class MacFramePresenter final : public FramePresenter, private juce::AsyncUpdater {
    struct Slot {
        std::atomic<bool> available{true};
        IOSurfaceRef surface = nullptr;
        unsigned texture = 0, framebuffer = 0;
        int width = 0, height = 0;
    };
    juce::Component& owner;
    juce::OpenGLContext& context;
    juce::Component contextHost;
    juce::Colour backgroundColour;
    OsciGpuPresentationView* view = nil;
    NSView* peerView = nil;
    std::array<Slot, 3> slots;
    std::atomic<int> pending{-1};
    int displayed = -1;
    unsigned readFramebuffer = 0;
    bool available = false;
public:
    MacFramePresenter(juce::Component& component, juce::OpenGLContext& context) : owner(component), context(context) {
        context.detach();
        contextHost.setInterceptsMouseClicks(false, false);
        owner.addAndMakeVisible(contextHost);
        contextHost.setBounds(0, 0, 1, 1);
        context.setComponentPaintingEnabled(false);
        context.attachTo(contextHost);
        view = [[OsciGpuPresentationView alloc] initWithFrame:NSZeroRect];
        [view setWantsLayer:YES];
        view.layer.contentsGravity = kCAGravityResizeAspect;
        view.layer.backgroundColor = NSColor.blackColor.CGColor;
        view.layer.opaque = YES;
    }
    ~MacFramePresenter() override {
        cancelPendingUpdate();
        view.layer.contents = nil;
        [view removeFromSuperview];
        [view release];
        for (auto& slot : slots) {
            if (slot.surface != nullptr) { CFRelease(slot.surface); }
        }
    }
    void resized(juce::Rectangle<int> area, juce::Colour background) override {
        // Hide only the native drawable; hiding the JUCE host stops rendering.
        auto* nativeContext = static_cast<NSOpenGLContext*>(context.getRawContext());
        [nativeContext.view setHidden:YES];
        auto* peer = owner.getPeer();
        auto* native = peer != nullptr ? static_cast<NSView*>(peer->getNativeHandle()) : nil;
        auto* parent = native.superview;
        available = parent != nil;
        if (!available) {
            [view removeFromSuperview];
            peerView = nil;
            return;
        }
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        if (native != peerView || view.superview != parent) {
            [view removeFromSuperview];
            [parent addSubview:view positioned:NSWindowBelow relativeTo:native];
            peerView = native;
        }
        const auto bounds = peer->getComponent().getLocalArea(&owner, area);
        const auto nativeBounds = native.bounds;
        const auto scaleX = nativeBounds.size.width / juce::jmax(1, peer->getComponent().getWidth());
        const auto scaleY = nativeBounds.size.height / juce::jmax(1, peer->getComponent().getHeight());
        const auto rect = NSMakeRect(nativeBounds.origin.x + bounds.getX() * scaleX, nativeBounds.origin.y + bounds.getY() * scaleY,
                                     bounds.getWidth() * scaleX, bounds.getHeight() * scaleY);
        const auto frame = [native convertRect:rect toView:parent];
        if (!NSEqualRects(view.frame, frame)) { [view setFrame:frame]; }
        if (background != backgroundColour) {
            backgroundColour = background;
            view.layer.backgroundColor = [NSColor colorWithSRGBRed:background.getFloatRed() green:background.getFloatGreen() blue:background.getFloatBlue() alpha:1.0].CGColor;
        }
        [view setHidden:!owner.isShowing() || area.isEmpty()];
        [CATransaction commit];
    }
    void paint(juce::Graphics& g, juce::Rectangle<int> viewport) override {
#if JUCE_MODULE_AVAILABLE_jucewright && JUCEWRIGHT_ENABLE_AUTOMATION
        if (jucewright::isTakingComponentScreenshot()) {
            paintPresentedFrame(g, viewport);
            return;
        }
#endif
        if (available) {
            // Replace ancestor painting with a hole; controls paint normally above it.
            g.setColour(juce::Colours::transparentBlack);
            g.getInternalContext().fillRect(viewport, true);
        }
    }
#if JUCE_MODULE_AVAILABLE_jucewright && JUCEWRIGHT_ENABLE_AUTOMATION
    void paintPresentedFrame(juce::Graphics& g, juce::Rectangle<int> viewport) {
        // Message thread owns displayed. The displayed slot is unavailable to
        // the GL producer until the next presentation, so its pixels stay stable.
        handleAsyncUpdate();
        if (displayed < 0) { return; }
        const auto surface = slots[displayed].surface;
        if (surface == nullptr || IOSurfaceLock(surface, kIOSurfaceLockReadOnly, nullptr) != kIOReturnSuccess) { return; }
        const auto unlock = juce::ScopeGuard([&] { IOSurfaceUnlock(surface, kIOSurfaceLockReadOnly, nullptr); });
        const int width = (int)IOSurfaceGetWidth(surface);
        const int height = (int)IOSurfaceGetHeight(surface);
        const auto* source = static_cast<const juce::uint8*>(IOSurfaceGetBaseAddress(surface));
        if (source == nullptr || width <= 0 || height <= 0) { return; }
        juce::Image image(juce::Image::ARGB, width, height, false);
        {
            juce::Image::BitmapData pixels(image, juce::Image::BitmapData::writeOnly);
            const auto stride = IOSurfaceGetBytesPerRow(surface);
            for (int y = 0; y < height; ++y) {
                const auto* row = source + y * stride;
                for (int x = 0; x < width; ++x) {
                    pixels.setPixelColour(x, y, juce::Colour(row[x * 4 + 2], row[x * 4 + 1], row[x * 4], row[x * 4 + 3]));
                }
            }
        }
        g.setColour(backgroundColour);
        g.fillRect(viewport);
        g.drawImageWithin(image, viewport.getX(), viewport.getY(), viewport.getWidth(), viewport.getHeight(), juce::RectanglePlacement::centred);
    }
#endif
    void handleAsyncUpdate() override {
        const int next = pending.exchange(-1);
        if (next < 0) { return; }
        [CATransaction begin];
        [CATransaction setDisableActions:YES];
        view.layer.contents = (id) slots[next].surface;
        [CATransaction commit];
        // Commit removal before making the preceding surface eligible for reuse.
        [CATransaction flush];
        const int old = displayed;
        displayed = next;
        if (old >= 0) { slots[old].available.store(true); }
    }
    bool allocate(Slot& slot, int width, int height) {
        using namespace juce::gl;
        if (slot.width == width && slot.height == height && slot.texture != 0) { return true; }
        if (slot.framebuffer != 0) { glDeleteFramebuffers(1, &slot.framebuffer); slot.framebuffer = 0; }
        if (slot.texture != 0) { glDeleteTextures(1, &slot.texture); slot.texture = 0; }
        if (slot.surface != nullptr) { CFRelease(slot.surface); slot.surface = nullptr; }
        slot.width = 0; slot.height = 0;
        NSDictionary* properties = @{
            (id)kIOSurfaceWidth: @(width), (id)kIOSurfaceHeight: @(height),
            (id)kIOSurfaceBytesPerElement: @4, (id)kIOSurfacePixelFormat: @((uint32_t)'BGRA')
        };
        slot.surface = IOSurfaceCreate((CFDictionaryRef) properties);
        if (slot.surface == nullptr) { return false; }
        glGenTextures(1, &slot.texture);
        glBindTexture(GL_TEXTURE_RECTANGLE, slot.texture);
        const auto error = CGLTexImageIOSurface2D(CGLGetCurrentContext(), GL_TEXTURE_RECTANGLE, GL_RGBA8,
                                                 width, height, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, slot.surface, 0);
        if (error != kCGLNoError) { return false; }
        glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glGenFramebuffers(1, &slot.framebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, slot.framebuffer);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_RECTANGLE, slot.texture, 0);
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { return false; }
        slot.width = width; slot.height = height;
        return true;
    }
    void present(unsigned texture, int width, int height) override {
        using namespace juce::gl;
        if (texture == 0 || width <= 0 || height <= 0) { return; }
        int index = -1;
        for (int i = 0; i < int(slots.size()); ++i) {
            bool expected = true;
            if (slots[i].available.compare_exchange_strong(expected, false)) {
                if (slots[i].surface == nullptr || !IOSurfaceIsInUse(slots[i].surface)) { index = i; break; }
                slots[i].available.store(true);
            }
        }
        if (index < 0) { return; }
        auto& slot = slots[index];
        GLint oldRead = 0, oldDraw = 0, oldTexture = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldRead);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldDraw);
        glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE, &oldTexture);
        const auto restore = juce::ScopeGuard([&] {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, oldRead);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, oldDraw);
            glBindTexture(GL_TEXTURE_RECTANGLE, oldTexture);
        });
        if (!allocate(slot, width, height)) {
            slot.available.store(true);
            return;
        }
        if (readFramebuffer == 0) { glGenFramebuffers(1, &readFramebuffer); }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, slot.framebuffer);
        const bool scissor = glIsEnabled(GL_SCISSOR_TEST);
        glDisable(GL_SCISSOR_TEST);
        glBlitFramebuffer(0, 0, width, height, 0, height, width, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        GLboolean mask[4]; GLfloat clear[4];
        glGetBooleanv(GL_COLOR_WRITEMASK, mask); glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
        glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
        glColorMask(mask[0], mask[1], mask[2], mask[3]);
        glClearColor(clear[0], clear[1], clear[2], clear[3]);
        if (scissor) { glEnable(GL_SCISSOR_TEST); }
        // The compositor must never see a surface before its GPU writes finish.
        // This waits on the GL worker, with no pixel transfer to CPU memory.
        glFinish();
        const int old = pending.exchange(index);
        if (old >= 0) { slots[old].available.store(true); }
        triggerAsyncUpdate();
    }
    void releaseResources() override {
        using namespace juce::gl;
        for (auto& slot : slots) {
            if (slot.framebuffer != 0) { glDeleteFramebuffers(1, &slot.framebuffer); slot.framebuffer = 0; }
            if (slot.texture != 0) { glDeleteTextures(1, &slot.texture); slot.texture = 0; }
        }
        if (readFramebuffer != 0) { glDeleteFramebuffers(1, &readFramebuffer); readFramebuffer = 0; }
    }
};

} // namespace

std::unique_ptr<FramePresenter> FramePresenter::create(juce::Component& owner, juce::OpenGLContext& context) {
    return std::make_unique<MacFramePresenter>(owner, context);
}
#endif
