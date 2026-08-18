#pragma once

#include <JuceHeader.h>
#include <cstdint>

namespace osci::files {
enum class FileCapability : std::uint8_t {
    Audio = 1 << 0,
    Image = 1 << 1,
    Video = 1 << 2,
    Animated = 1 << 3,
    Lottie = 1 << 4,
    CodeEditable = 1 << 5,
};

struct FileFormat {
    const char* extension;
    std::uint8_t capabilities = 0;

    static constexpr FileFormat define(const char* extension) { return { extension }; }

    constexpr FileFormat audio() const { return with(FileCapability::Audio); }
    constexpr FileFormat image() const { return with(FileCapability::Image); }
    constexpr FileFormat video() const { return with(FileCapability::Video); }
    constexpr FileFormat animated() const { return with(FileCapability::Animated); }
    constexpr FileFormat lottie() const { return with(FileCapability::Lottie); }
    constexpr FileFormat codeEditable() const { return with(FileCapability::CodeEditable); }

    constexpr bool has(FileCapability capability) const {
        return (capabilities & static_cast<std::uint8_t>(capability)) != 0;
    }

private:
    constexpr FileFormat with(FileCapability capability) const {
        return { extension, static_cast<std::uint8_t>(capabilities | static_cast<std::uint8_t>(capability)) };
    }
};

inline const std::vector<FileFormat>& sourceFormats() {
    static const std::vector<FileFormat> formats = {
        FileFormat::define("obj"),
        FileFormat::define("svg"),
        FileFormat::define("lua").codeEditable(),
        FileFormat::define("txt").codeEditable(),
        FileFormat::define("gpla").animated(),
        FileFormat::define("gif").image().animated(),
        FileFormat::define("png").image(),
        FileFormat::define("jpg").image(),
        FileFormat::define("jpeg").image(),
        FileFormat::define("wav").audio(),
        FileFormat::define("aiff").audio(),
        FileFormat::define("ogg").audio(),
        FileFormat::define("flac").audio(),
        FileFormat::define("mp3").audio(),
#if JUCE_MAC
        FileFormat::define("aac").audio(),
        FileFormat::define("m4a").audio(),
#endif
#if OSCI_PREMIUM
        FileFormat::define("lsystem"),
        FileFormat::define("mp4").image().video().animated(),
        FileFormat::define("mov").image().video().animated(),
        FileFormat::define("json").lottie().animated(),
        FileFormat::define("lottie").lottie().animated(),
        FileFormat::define("lot").lottie().animated(),
#endif
    };
    return formats;
}

inline juce::String normaliseExtension(juce::String extensionOrName) {
    extensionOrName = extensionOrName.trim().toLowerCase();
    const auto extension = extensionOrName.fromLastOccurrenceOf(".", false, false);
    return extension.isNotEmpty() ? extension : extensionOrName.trimCharactersAtStart(".");
}

inline const FileFormat* findSourceFormat(const juce::String& extensionOrName) {
    const auto extension = normaliseExtension(extensionOrName);
    for (const auto& format : sourceFormats()) {
        if (extension == format.extension) {
            return &format;
        }
    }
    return nullptr;
}

inline bool isSupportedSource(const juce::String& extensionOrName) {
    return findSourceFormat(extensionOrName) != nullptr;
}

inline bool isSupportedSource(const juce::File& file) {
    return isSupportedSource(file.getFileExtension());
}

inline bool isAudio(const juce::String& extensionOrName) {
    const auto* format = findSourceFormat(extensionOrName);
    return format != nullptr && format->has(FileCapability::Audio);
}

inline bool isImage(const juce::String& extensionOrName) {
    const auto* format = findSourceFormat(extensionOrName);
    return format != nullptr && format->has(FileCapability::Image);
}

inline bool isVideo(const juce::String& extensionOrName) {
    const auto* format = findSourceFormat(extensionOrName);
    return format != nullptr && format->has(FileCapability::Video);
}

inline bool isAnimated(const juce::String& extensionOrName) {
    const auto* format = findSourceFormat(extensionOrName);
    return format != nullptr && format->has(FileCapability::Animated);
}

inline bool isLottie(const juce::String& extensionOrName) {
    const auto* format = findSourceFormat(extensionOrName);
    return format != nullptr && format->has(FileCapability::Lottie);
}

inline bool isCodeEditable(const juce::String& extensionOrName) {
    const auto* format = findSourceFormat(extensionOrName);
    return format != nullptr && format->has(FileCapability::CodeEditable);
}

inline juce::String sourceWildcard() {
    juce::StringArray patterns;
    for (const auto& format : sourceFormats()) {
        patterns.add("*." + juce::String(format.extension));
    }
    return patterns.joinIntoString(";");
}

inline juce::String audioWildcard() {
    juce::StringArray patterns;
    for (const auto& format : sourceFormats()) {
        if (format.has(FileCapability::Audio)) {
            patterns.add("*." + juce::String(format.extension));
        }
    }
    return patterns.joinIntoString(";");
}

inline bool isOsciProject(const juce::File& file) {
    return file.hasFileExtension("osci");
}

inline bool isSosciProject(const juce::File& file) {
    return file.hasFileExtension("sosci");
}
}
