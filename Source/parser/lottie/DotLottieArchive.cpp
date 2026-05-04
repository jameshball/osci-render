#include "DotLottieArchive.h"

namespace {
juce::String normaliseArchivePath(juce::String path) {
    path = path.trim().replaceCharacter('\\', '/');
    while (path.startsWithChar('/')) {
        path = path.substring(1);
    }
    return path;
}

int findEntryIndex(juce::ZipFile& zip, const juce::String& path) {
    const auto wanted = normaliseArchivePath(path).toLowerCase();
    if (wanted.isEmpty()) return -1;

    for (int entryIndex = 0; entryIndex < zip.getNumEntries(); ++entryIndex) {
        auto* entry = zip.getEntry(entryIndex);
        if (entry == nullptr) continue;

        if (normaliseArchivePath(entry->filename).toLowerCase() == wanted) {
            return entryIndex;
        }
    }

    return -1;
}

juce::String readEntry(juce::ZipFile& zip, int entryIndex) {
    if (entryIndex < 0) return {};

    std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(entryIndex));
    if (stream == nullptr) return {};

    return stream->readEntireStreamAsString();
}

juce::String pathForManifestAnimation(const juce::var& animation) {
    auto* object = animation.getDynamicObject();
    if (object == nullptr) return {};

    auto path = object->getProperty("path").toString();
    if (path.isNotEmpty()) return normaliseArchivePath(path);

    auto id = object->getProperty("id").toString().trim();
    if (id.isNotEmpty()) return "animations/" + id + ".json";

    return {};
}

juce::String selectManifestAnimationPath(const juce::String& manifestJson) {
    auto manifest = juce::JSON::parse(manifestJson);
    auto* manifestObject = manifest.getDynamicObject();
    if (manifestObject == nullptr) return {};

    auto animationsVar = manifestObject->getProperty("animations");
    auto* animations = animationsVar.getArray();
    if (animations == nullptr || animations->isEmpty()) return {};

    auto activeId = manifestObject->getProperty("activeAnimationId").toString().trim();
    if (activeId.isEmpty()) {
        activeId = manifestObject->getProperty("defaultAnimationId").toString().trim();
    }
    if (activeId.isEmpty()) {
        activeId = manifestObject->getProperty("initialAnimationId").toString().trim();
    }

    if (activeId.containsChar('/')) {
        return normaliseArchivePath(activeId);
    }

    if (activeId.isNotEmpty()) {
        for (auto& animation : *animations) {
            auto* animationObject = animation.getDynamicObject();
            if (animationObject == nullptr) continue;

            if (animationObject->getProperty("id").toString().trim() == activeId) {
                return pathForManifestAnimation(animation);
            }
        }
    }

    return pathForManifestAnimation(animations->getReference(0));
}

int findDeterministicFallbackAnimation(juce::ZipFile& zip) {
    juce::StringArray animationEntries;
    for (int entryIndex = 0; entryIndex < zip.getNumEntries(); ++entryIndex) {
        auto* entry = zip.getEntry(entryIndex);
        if (entry == nullptr) continue;

        auto name = normaliseArchivePath(entry->filename);
        auto lowerName = name.toLowerCase();
        if (lowerName.startsWith("animations/") && lowerName.endsWith(".json")) {
            animationEntries.add(name);
        }
    }

    animationEntries.sort(true);
    return animationEntries.isEmpty() ? -1 : findEntryIndex(zip, animationEntries[0]);
}
}



namespace osci::lottie {
juce::String extractAnimationJsonFromDotLottie(const juce::MemoryBlock& archiveData) {
    juce::MemoryInputStream zipStream(archiveData, false);
    juce::ZipFile zip(zipStream);

    const auto manifestJson = readEntry(zip, findEntryIndex(zip, "manifest.json"));
    if (manifestJson.isNotEmpty()) {
        const auto manifestPath = selectManifestAnimationPath(manifestJson);
        const auto manifestAnimation = readEntry(zip, findEntryIndex(zip, manifestPath));
        if (manifestAnimation.isNotEmpty()) {
            return manifestAnimation;
        }
    }

    return readEntry(zip, findDeterministicFallbackAnimation(zip));
}
}
