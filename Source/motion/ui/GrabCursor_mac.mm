#include "GrabCursor.h"

#if JUCE_MAC
#import <Cocoa/Cocoa.h>

// The system's own closed hand, drawn into a 2x image JUCE can show.
juce::MouseCursor motion::grabbingCursor() {
    static const juce::MouseCursor cursor = [] {
        NSCursor* native = [NSCursor closedHandCursor];
        NSImage* image = [native image];
        const auto size = [image size];
        const auto hotSpot = [native hotSpot];
        constexpr int scale = 2;
        const auto width = juce::jmax(1, juce::roundToInt(size.width) * scale);
        const auto height = juce::jmax(1, juce::roundToInt(size.height) * scale);
        juce::Image pixels(juce::Image::ARGB, width, height, true, juce::SoftwareImageType());
        {
            juce::Image::BitmapData data(pixels, juce::Image::BitmapData::readWrite);
            auto* space = CGColorSpaceCreateDeviceRGB();
            auto* context = CGBitmapContextCreate(data.data, static_cast<size_t>(width), static_cast<size_t>(height), 8, static_cast<size_t>(data.lineStride), space,
                                                  static_cast<uint32_t>(kCGImageAlphaPremultipliedFirst) | static_cast<uint32_t>(kCGBitmapByteOrder32Little));
            CGColorSpaceRelease(space);
            if (context != nullptr) {
                NSGraphicsContext* graphics = [NSGraphicsContext graphicsContextWithCGContext: context flipped: NO];
                [NSGraphicsContext saveGraphicsState];
                [NSGraphicsContext setCurrentContext: graphics];
                [image drawInRect: NSMakeRect(0, 0, width, height) fromRect: NSZeroRect operation: NSCompositingOperationCopy fraction: 1.0];
                [NSGraphicsContext restoreGraphicsState];
                CGContextRelease(context);
            }
        }
        return juce::MouseCursor(juce::ScaledImage(pixels, scale), juce::Point<int>(juce::roundToInt(hotSpot.x), juce::roundToInt(hotSpot.y)));
    }();
    return cursor;
}
#endif
