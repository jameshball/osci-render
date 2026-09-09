// Standalone integration harness linked to the app's shared-code archive.
#include "../Source/PluginProcessor.h"
#include <iostream>
#include <stdexcept>

static void check(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
    std::cout << "PASS " << message << std::endl;
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    try {
        OscirenderAudioProcessor processor;
        auto& files = processor.getFileController();
        const juce::File root(argc > 1 ? argv[1] : ".");
        const int first = files.addFile(root.getChildFile("Resources/models/cube.obj"));
        auto a = files.ensureScene(first);
        const juce::String text("OSCI");
        auto title = files.addSceneObject(first, "Title.txt", std::make_shared<juce::MemoryBlock>(text.toRawUTF8(), text.getNumBytesAsUTF8()));
        title->transform.set(1, -0.8f);
        title->transform.set(6, 0.4f);
        title->transform.set(7, 0.4f);
        check(a->objects.size() == 2, "two independent sources belong to one scene");
        LuaVariables context;
        context.sampleRate = 48000;
        context.frequency = 220;
        auto before = a->render(0.25, context, 0);
        check(a->objects.front()->transform.expose(), "object acquires a host slot");
        files.sceneAutomation.parameters[0][0]->setValueUnnormalised(0.7f);
        auto after = a->render(0.25, context, 0);
        check(std::abs(after.x - before.x - 0.7f) < 0.0001f, "host slot changes the actual rendered points");
        check(a->camera.expose(), "camera shares the same slot pool");
        files.sceneAutomation.parameters[1][1]->setValueUnnormalised(0.2f);
        after = a->render(0.25, context, 0);
        check(std::abs(after.y - before.y + 0.2f) < 0.0001f, "camera host automation changes output coordinates");
        const int second = files.addFile(root.getChildFile("Resources/models/diamond.obj"));
        auto b = files.ensureScene(second);
        b->camera.set(0, 2.0f);
        files.selectFile(first);
        check(std::abs(a->camera.get(0)) < 0.0001f && std::abs(b->camera.get(0) - 2.0f) < 0.0001f, "camera framing is independent across scenes");
        const int duplicate = files.duplicateFile(first);
        auto copy = files.getScene(duplicate);
        check(copy != nullptr && copy->objects.size() == 2, "scene duplication preserves the whole composition");
        check(copy->camera.getSlot() == -1 && copy->objects[0]->transform.getSlot() == -1, "scene duplication never retargets existing automation");
        check(std::abs(copy->objects[0]->transform.get(0) - 0.7f) < 0.0001f, "scene duplication preserves current automated values");
        files.selectFile(first);
        juce::MemoryBlock saved;
        processor.getStateInformation(saved);
        juce::File("/tmp/osci-scene-integration.osci").replaceWithData(saved.getData(), saved.getSize());
        a.reset(); b.reset(); copy.reset(); title.reset();
        processor.setStateInformation(saved.getData(), (int)saved.getSize());
        a = files.getScene(first);
        check(files.size() == 3 && a != nullptr && a->objects.size() == 2, "project round trip restores scene membership");
        check(a->objects[0]->transform.getSlot() == 0 && a->camera.getSlot() == 1, "project round trip restores slot identity");
        check(std::abs(a->objects[1]->transform.get(1) + 0.8f) < 0.0001f, "unexposed transforms survive project round trip");
        files.sceneAutomation.parameters[0][0]->setValueUnnormalised(-0.4f);
        check(std::abs(a->objects[0]->transform.get(0) + 0.4f) < 0.0001f, "restored bindings still respond to host parameters");
        auto live = files.addLiveSceneObject(first, true);
        std::vector<std::unique_ptr<osci::Shape>> lines;
        lines.push_back(std::make_unique<osci::Line>(-1, 0, 1, 0));
        live->replaceGeometry(std::move(lines));
        check(std::abs(live->pointAt(0.75).x - 0.5f) < 0.0001f, "live geometry uses the same point path");
        auto repeatedLive = files.addLiveSceneObject(first, true);
        check(repeatedLive != nullptr && std::abs(repeatedLive->pointAt(0.75).x - 0.5f) < 0.0001f, "a shared live source can be placed more than once");
        live.reset(); repeatedLive.reset(); a.reset();
        auto animation = [](int count) {
            const juce::String frame(R"({"focalLength":1,"objects":[{"vertices":[[{"x":-0.5,"y":0,"z":-1},{"x":0.5,"y":0,"z":-1}]],"matrix":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]}]})");
            juce::String json("{\"frames\":[");
            for (int i = 0; i < count; ++i) { json += (i == 0 ? "" : ",") + frame; }
            json += "]}";
            return std::make_shared<juce::MemoryBlock>(json.toRawUTF8(), json.getNumBytesAsUTF8());
        };
        auto shortAnimation = files.addSceneObject(first, "short.gpla", animation(60));
        auto longAnimation = files.addSceneObject(first, "long.gpla", animation(150));
        a = files.getScene(first);
        check(shortAnimation->parser->getNumFrames() == 60 && longAnimation->parser->getNumFrames() == 150, "animation fixtures retain their different durations");
        a->setFrame(165, true);
        check(shortAnimation->parser->getCurrentFrame() == 45 && longAnimation->parser->getCurrentFrame() == 15, "shared clock loops each animation at its own duration");
        a->setFrame(165, false);
        check(shortAnimation->parser->getCurrentFrame() == 59 && longAnimation->parser->getCurrentFrame() == 149, "non-looping playback holds each animation at its own end");
        shortAnimation.reset(); longAnimation.reset(); a.reset();
        processor.midiEnabled->setBoolValue(false);
        processor.inputEnabled->setBoolValue(false);
        processor.muteParameter->setBoolValue(false);
        processor.setRateAndBufferSizeDetails(48000, 512);
        processor.prepareToPlay(48000, 512);
        juce::Thread::sleep(100);
        juce::AudioBuffer<float> audio(2, 512);
        juce::MidiBuffer midi;
        float peak = 0;
        for (int block = 0; block < 64; ++block) {
            audio.clear();
            midi.clear();
            processor.processBlock(audio, midi);
            for (int channel = 0; channel < 2; ++channel) {
                for (int i = 0; i < 512; ++i) {
                    const float sample = audio.getSample(channel, i);
                    if (!std::isfinite(sample)) { throw std::runtime_error("non-finite scene audio"); }
                    peak = juce::jmax(peak, std::abs(sample));
                }
            }
        }
        processor.releaseResources();
        check(peak > 0.01f, "the full synth/effects path produces finite non-silent scene audio");
        std::cout << "Scene integration passed" << std::endl;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << std::endl;
        return 1;
    }
    return 0;
}
