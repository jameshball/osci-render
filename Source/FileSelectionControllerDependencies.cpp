#include "FileSelectionControllerDependencies.h"

#include "PluginProcessor.h"
#include "audio/synth/ShapeSound.h"
#include "audio/synth/ShapeVoice.h"
#include "audio/synth/VoiceManager.h"
#include "parser/FileParser.h"

FileSelectionController::Dependencies createFileSelectionDependencies(
    OscirenderAudioProcessor& processor, VoiceManager& voices,
    std::function<void(int, juce::String, juce::String)> errorCallback) {
    FileSelectionController::Dependencies dependencies;
    dependencies.createFileResources = [&processor, errorCallback] {
        auto parser = std::make_shared<FileParser>(processor, errorCallback);
        return FileSelectionController::FileResources { parser, new ShapeSound(processor, parser) };
    };
    dependencies.parseFile = [](FileSelectionController::File& file, juce::Font& font) {
        const auto extension = file.name.fromLastOccurrenceOf(".", true, false).toLowerCase();
        file.parser->parse(juce::String(file.id), file.name, extension,
            std::make_unique<juce::MemoryInputStream>(*file.data, false), font);
    };
    dependencies.prepareTextureInput = [](const std::shared_ptr<FileParser>& parser, int width, int height) {
        parser->prepareLiveImageInput(width, height);
    };
    dependencies.updateTextureInput = [](const std::shared_ptr<FileParser>& parser,
                                       const std::vector<std::uint8_t>& rgba, int width, int height, bool verticallyFlipped) {
        parser->updateLiveImageFrame(rgba, width, height, verticallyFlipped);
    };
    dependencies.activeSoundChanged = [&voices](juce::SynthesiserSound* sound) {
        for (int i = 0; i < voices.getNumVoices(); ++i) {
            auto* voice = dynamic_cast<ShapeVoice*>(voices.getVoice(i));
            if (voice != nullptr) {
                voice->updateSound(sound);
            }
        }
    };
    return dependencies;
}
