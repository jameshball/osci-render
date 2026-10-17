#pragma once

#include <JuceHeader.h>
#include <optional>
#include <vector>

// A live picture another app shares (Syphon on macOS, Spout on Windows),
// traced into beam strokes as it arrives. Like a Blender source it is never
// prepared ahead: its clips draw whatever arrives now, and export needs a
// capture of it.
namespace motion::texture {

inline const juce::String extension = ".texture";
inline const juce::String captureExtension = ".texture-capture";

inline bool isSource(const juce::String& extensionOrName) { return extensionOrName.endsWithIgnoreCase(extension); }

// What the sharing technology is called here.
inline juce::String technology() {
#if JUCE_MAC
    return "Syphon";
#elif JUCE_WINDOWS
    return "Spout";
#else
    return "Live video";
#endif
}

// The app and the picture it shares, as the source remembers them.
struct Sender {
    juce::String name, application;
    bool operator==(const Sender&) const = default;
    // As people see it: "Composition (Resolume Arena)".
    juce::String describe() const {
        if (application.isEmpty() || application == name) { return name; }
        return name.isEmpty() ? application : name + " (" + application + ")";
    }
};

inline juce::MemoryBlock encode(const Sender& sender) {
    juce::XmlElement root("texture-source");
    root.setAttribute("name", sender.name);
    root.setAttribute("application", sender.application);
    const auto text = root.toString(juce::XmlElement::TextFormat().singleLine().withoutHeader());
    return {text.toRawUTF8(), text.getNumBytesAsUTF8()};
}

inline std::optional<Sender> decode(const juce::MemoryBlock& data) {
    const auto root = juce::parseXML(data.toString());
    if (root == nullptr || !root->hasTagName("texture-source")) { return std::nullopt; }
    Sender sender {root->getStringAttribute("name"), root->getStringAttribute("application")};
    if (sender.name.isEmpty() && sender.application.isEmpty()) { return std::nullopt; }
    return sender;
}

// A frame of any size fitted into a square of `dimension`, as a video's
// frames are: centred, its shape kept, the rest transparent black. Each
// pixel averages the ones it covers. Rows run top first unless `bottomUp`.
inline void fit(const std::uint8_t* rgba, int width, int height, bool bottomUp, int dimension, std::vector<std::uint8_t>& out) {
    out.assign(static_cast<std::size_t>(dimension) * static_cast<std::size_t>(dimension) * 4, 0);
    if (rgba == nullptr || width <= 0 || height <= 0 || dimension <= 0) { return; }
    const auto scale = static_cast<double>(dimension) / std::max(width, height);
    const auto fittedWidth = std::max(1, static_cast<int>(width * scale));
    const auto fittedHeight = std::max(1, static_cast<int>(height * scale));
    const auto left = (dimension - fittedWidth) / 2, top = (dimension - fittedHeight) / 2;
    for (int y = 0; y < fittedHeight; ++y) {
        const auto fromY = y * height / fittedHeight, toY = std::max(fromY + 1, (y + 1) * height / fittedHeight);
        for (int x = 0; x < fittedWidth; ++x) {
            const auto fromX = x * width / fittedWidth, toX = std::max(fromX + 1, (x + 1) * width / fittedWidth);
            // A few samples across the block are enough to average it.
            const auto stepY = std::max(1, (toY - fromY) / 4), stepX = std::max(1, (toX - fromX) / 4);
            std::uint32_t sum[3] {}, count = 0;
            for (int sy = fromY; sy < toY; sy += stepY) {
                const auto row = bottomUp ? height - 1 - sy : sy;
                const auto* pixel = rgba + (static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(fromX)) * 4;
                for (int sx = fromX; sx < toX; sx += stepX, pixel += 4 * stepX) {
                    for (int channel = 0; channel < 3; ++channel) { sum[channel] += pixel[channel]; }
                    ++count;
                }
            }
            auto* target = out.data() + (static_cast<std::size_t>(top + y) * static_cast<std::size_t>(dimension) + static_cast<std::size_t>(left + x)) * 4;
            for (int channel = 0; channel < 3; ++channel) { target[channel] = static_cast<std::uint8_t>(sum[channel] / std::max<std::uint32_t>(1, count)); }
            // Shared pictures are opaque, whatever alpha their apps leave.
            target[3] = 255;
        }
    }
}
}
