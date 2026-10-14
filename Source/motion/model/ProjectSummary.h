#pragma once

#include <JuceHeader.h>
#include <algorithm>

namespace motion {
// What the start screen shows of a saved project: its picture (the Scope's
// frame when it was last saved, a 4:3 JPEG) and its description. Written as
// the first thing in the project's XML, so both are read from the start of
// the file alone, however large the rest. The description is the project's
// own (Project::description); this is a copy of it where it can be found fast.
struct ProjectSummary {
    juce::Image picture;
    juce::String description;

    static constexpr int width = 512, height = 384;
    // A saved project's summary lies within this many bytes of its start.
    static constexpr std::size_t reach = 256 * 1024;

    // The middle of `frame` at 4:3, at the picture's size, as a JPEG.
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

    // Puts the summary first in a project's XML, in place of any there.
    static void write(juce::XmlElement& project, const juce::MemoryBlock& jpeg, const juce::String& description) {
        if (auto* old = project.getChildByName(tag)) {
            project.removeChildElement(old, true);
        }
        if (jpeg.isEmpty() && description.isEmpty()) {
            return;
        }
        auto* summary = new juce::XmlElement(tag);
        if (description.isNotEmpty()) { summary->setAttribute("description", description); }
        if (!jpeg.isEmpty()) { summary->setAttribute("jpeg", juce::Base64::toBase64(jpeg.getData(), jpeg.getSize())); }
        project.insertChildElement(summary, 0);
    }
    // The picture a project's XML was saved with, as a JPEG.
    static juce::MemoryBlock pictureIn(const juce::XmlElement& project) {
        const auto* summary = project.getChildByName(tag);
        return summary != nullptr ? jpegOf(*summary) : juce::MemoryBlock();
    }

    // The summary of a saved project, read from its start alone; empty when
    // it has none.
    static ProjectSummary peek(const void* data, std::size_t size) {
        const auto* begin = static_cast<const char*>(data);
        const auto* end = begin + std::min(size, reach);
        const std::string_view open {"<summary "}, close {"/>"};
        const auto* start = std::search(begin, end, open.begin(), open.end());
        const auto* finish = start == end ? end : std::search(start, end, close.begin(), close.end());
        if (finish == end) {
            return {};
        }
        const auto element = juce::parseXML(juce::String::fromUTF8(start, static_cast<int>(finish - start + static_cast<std::ptrdiff_t>(close.size()))));
        if (element == nullptr || !element->hasTagName(tag)) {
            return {};
        }
        ProjectSummary summary;
        summary.description = element->getStringAttribute("description");
        const auto jpeg = jpegOf(*element);
        if (!jpeg.isEmpty()) { summary.picture = juce::ImageFileFormat::loadFrom(jpeg.getData(), jpeg.getSize()); }
        return summary;
    }
    static ProjectSummary peek(const juce::File& file) {
        juce::FileInputStream stream(file);
        if (!stream.openedOk()) {
            return {};
        }
        juce::MemoryBlock start;
        stream.readIntoMemoryBlock(start, static_cast<ssize_t>(reach));
        return peek(start.getData(), start.getSize());
    }

private:
    static constexpr const char* tag = "summary";
    static juce::MemoryBlock jpegOf(const juce::XmlElement& summary) {
        juce::MemoryBlock jpeg;
        bool decoded = false;
        {
            juce::MemoryOutputStream stream(jpeg, false);
            decoded = juce::Base64::convertFromBase64(stream, summary.getStringAttribute("jpeg"));
        }
        return decoded ? jpeg : juce::MemoryBlock();
    }
};
}
