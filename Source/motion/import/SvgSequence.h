#pragma once

#include <JuceHeader.h>
#include <osci_file_import/osci_file_import.h>
#include <optional>
#include <vector>

// Several SVG files as the frames of one animation, as a Lottie is: kept
// together in the source's data, in order, with their frame rate.
namespace motion::svgsequence {

inline const juce::String extension = ".svgs";
inline constexpr double defaultFrameRate = 24.0;

struct Frames {
    std::vector<juce::String> svgs;
    double frameRate = defaultFrameRate;
};

inline juce::MemoryBlock encode(const Frames& frames) {
    juce::XmlElement root("svg-sequence");
    root.setAttribute("frameRate", frames.frameRate);
    for (const auto& svg : frames.svgs) {
        root.createNewChildElement("frame")->addTextElement(juce::Base64::toBase64(svg.toRawUTF8(), svg.getNumBytesAsUTF8()));
    }
    const auto text = root.toString(juce::XmlElement::TextFormat().singleLine().withoutHeader());
    return {text.toRawUTF8(), text.getNumBytesAsUTF8()};
}

inline std::optional<Frames> decode(const juce::MemoryBlock& data) {
    const auto root = juce::parseXML(data.toString());
    if (root == nullptr || !root->hasTagName("svg-sequence")) { return std::nullopt; }
    Frames frames;
    frames.frameRate = root->getDoubleAttribute("frameRate", defaultFrameRate);
    for (const auto* frame : root->getChildWithTagNameIterator("frame")) {
        juce::MemoryOutputStream svg;
        if (!juce::Base64::convertFromBase64(svg, frame->getAllSubText().trim())) { return std::nullopt; }
        frames.svgs.push_back(svg.toString());
    }
    if (frames.svgs.empty()) { return std::nullopt; }
    return frames;
}

// The page an SVG is drawn on (its viewBox), or what it draws when it has
// none: every frame is placed on the first frame's, so they keep their
// places relative to each other.
inline juce::Rectangle<float> page(const juce::String& svg) {
    const auto drawable = juce::Drawable::createFromImageData(svg.toRawUTF8(), svg.getNumBytesAsUTF8());
    if (drawable == nullptr) { return {}; }
    const auto* composite = dynamic_cast<const juce::DrawableComposite*>(drawable.get());
    const auto area = composite != nullptr ? composite->getContentArea() : juce::Rectangle<float>();
    return area.isEmpty() ? drawable->getOutlineAsPath().getBounds() : area;
}

// A frame's shapes, fitted as one SVG is fitted to the screen but by the
// shared page, not its own drawing. False when it isn't an SVG.
inline bool frameShapes(const juce::String& svg, juce::Rectangle<float> sharedPage, std::vector<std::unique_ptr<osci::Shape>>& shapes) {
    const auto drawable = juce::Drawable::createFromImageData(svg.toRawUTF8(), svg.getNumBytesAsUTF8());
    if (drawable == nullptr || sharedPage.isEmpty()) { return false; }
    auto path = drawable->getOutlineAsPath();
    const auto scale = 1.8f / std::max(sharedPage.getWidth(), sharedPage.getHeight());
    path.applyTransform(juce::AffineTransform::translation(-sharedPage.getCentreX(), -sharedPage.getCentreY()).scaled(scale));
    SvgParser::pathToShapes(path, shapes, false);
    return true;
}

// Files in the order a person numbers them: "frame 2" before "frame 10".
inline void sortByName(juce::Array<juce::File>& files) {
    std::sort(files.begin(), files.end(), [](const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural(b.getFileName()) < 0; });
}

// The name the files share, without the numbering that tells them apart
// ("walk_001.svg", "walk_002.svg": "walk"); empty when they share none.
inline juce::String sharedName(const juce::Array<juce::File>& files) {
    if (files.isEmpty()) { return {}; }
    auto common = files.getFirst().getFileNameWithoutExtension();
    for (const auto& file : files) {
        const auto name = file.getFileNameWithoutExtension();
        int length = 0;
        while (length < common.length() && length < name.length() && common[length] == name[length]) { ++length; }
        common = common.substring(0, length);
    }
    return common.trimCharactersAtEnd("0123456789").trimCharactersAtEnd(" _-.").trim();
}

// Whether the names differ only by a number: frames, most likely.
inline bool numbered(const juce::Array<juce::File>& files) {
    if (files.size() < 2) { return false; }
    const auto shared = sharedName(files);
    return std::all_of(files.begin(), files.end(), [&](const juce::File& file) {
        const auto rest = file.getFileNameWithoutExtension().substring(shared.length()).trimCharactersAtStart(" _-.");
        return rest.isNotEmpty() && rest.containsOnly("0123456789");
    });
}
}
