#pragma once

#include <JuceHeader.h>
#include <algorithm>

namespace motion {
// A project's picture for the start screen: the Scope's frame when it was
// last saved, as a 4:3 JPEG. It is the first thing in the project's XML, so
// it can be read from the start of the file alone, however large the rest.
struct ProjectPicture {
    static constexpr int width = 512, height = 384;
    // A saved project's picture lies within this many bytes of its start.
    static constexpr std::size_t reach = 256 * 1024;

    // The middle of `frame` at 4:3, at the picture's size.
    static juce::MemoryBlock encode(const juce::Image& frame) {
        juce::MemoryBlock jpeg;
        if (!frame.isValid()) {
            return jpeg;
        }
        auto area = frame.getBounds();
        if (area.getWidth() * 3 > area.getHeight() * 4) {
            area = area.withSizeKeepingCentre(area.getHeight() * 4 / 3, area.getHeight());
        } else {
            area = area.withSizeKeepingCentre(area.getWidth(), area.getWidth() * 3 / 4);
        }
        const auto picture = frame.getClippedImage(area).rescaled(width, height, juce::Graphics::highResamplingQuality);
        juce::JPEGImageFormat format;
        format.setQuality(.88f);
        bool written = false;
        {
            juce::MemoryOutputStream stream(jpeg, false);
            written = format.writeImageToStream(picture, stream);
        }
        return written ? jpeg : juce::MemoryBlock();
    }

    // Puts `jpeg` first in a project's XML, in place of any picture there.
    static void write(juce::XmlElement& project, const juce::MemoryBlock& jpeg) {
        if (auto* old = project.getChildByName(tag)) {
            project.removeChildElement(old, true);
        }
        if (jpeg.isEmpty()) {
            return;
        }
        auto* picture = new juce::XmlElement(tag);
        picture->setAttribute("jpeg", juce::Base64::toBase64(jpeg.getData(), jpeg.getSize()));
        project.insertChildElement(picture, 0);
    }
    static juce::MemoryBlock read(const juce::XmlElement& project) {
        const auto* picture = project.getChildByName(tag);
        return picture != nullptr ? jpegOf(*picture) : juce::MemoryBlock();
    }

    // The picture in a saved project, read from its start alone; none when
    // it has none (saved before pictures were, or never shown).
    static juce::Image peek(const void* data, std::size_t size) {
        const auto* begin = static_cast<const char*>(data);
        const auto* end = begin + std::min(size, reach);
        const std::string_view open {"<picture "}, close {"/>"};
        const auto* start = std::search(begin, end, open.begin(), open.end());
        const auto* finish = start == end ? end : std::search(start, end, close.begin(), close.end());
        if (finish == end) {
            return {};
        }
        const auto element = juce::parseXML(juce::String(juce::CharPointer_ASCII(start), static_cast<std::size_t>(finish - start) + close.size()));
        if (element == nullptr || !element->hasTagName(tag)) {
            return {};
        }
        const auto jpeg = jpegOf(*element);
        return jpeg.isEmpty() ? juce::Image() : juce::ImageFileFormat::loadFrom(jpeg.getData(), jpeg.getSize());
    }
    static juce::Image peek(const juce::File& file) {
        juce::FileInputStream stream(file);
        if (!stream.openedOk()) {
            return {};
        }
        juce::MemoryBlock start;
        stream.readIntoMemoryBlock(start, static_cast<ssize_t>(reach));
        return peek(start.getData(), start.getSize());
    }

private:
    static constexpr const char* tag = "picture";
    static juce::MemoryBlock jpegOf(const juce::XmlElement& picture) {
        juce::MemoryBlock jpeg;
        bool decoded = false;
        {
            juce::MemoryOutputStream stream(jpeg, false);
            decoded = juce::Base64::convertFromBase64(stream, picture.getStringAttribute("jpeg"));
        }
        return decoded ? jpeg : juce::MemoryBlock();
    }
};
}
