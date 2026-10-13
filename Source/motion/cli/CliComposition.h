#pragma once

#include "Recipe.h"
#include "../render/BeamRenderer.h"
#include "../render/HiddenLineBaker.h"
#include <atomic>
#include <memory>

namespace motion::cli {
// A project prepared to draw its beam offline, as Export draws it: hidden
// lines removed from the frames it draws before anything is sampled.
struct Composition {
    std::unique_ptr<PreparedComposition> prepared;
    double sampleRate = 48000;
    // How long preparing and removing hidden lines took.
    double prepareMilliseconds = 0, bakeMilliseconds = 0;

    // A project file, or a recipe (see Recipe) made into one.
    static juce::Result open(const juce::File& file, double sampleRate, Composition& opened) {
        ProjectFile project;
        const auto read = Recipe::isRecipe(file) ? Recipe::read(file, project.project) : ProjectFile::read(file, project);
        if (read.failed()) { return read; }
        for (const auto& line : project.notes.lines()) { std::cerr << file.getFileName() << ": left out " << line << std::endl; }
        return prepare(project.project, sampleRate, opened);
    }
    static juce::Result prepare(const Project& project, double sampleRate, Composition& prepared) {
        prepared.sampleRate = sampleRate;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        prepared.prepared = std::make_unique<PreparedComposition>(project, sampleRate, &never(), CompositionPurpose::signal, squareScope());
        prepared.prepareMilliseconds = juce::Time::getMillisecondCounterHiRes() - start;
        const auto& composition = *prepared.prepared;
        if (composition.preparationError.isNotEmpty()) { return juce::Result::fail(composition.preparationError); }
        for (const auto& clip : composition.clips) {
            if (clip.liveIdentity != nullptr) { return juce::Result::fail("It has a live source: capture it to draw it offline."); }
        }
        return juce::Result::ok();
    }
    // Hidden lines removed from every frame, to draw it all.
    void bakeAll() {
        bake([this](hidden::Bake& bake, const std::atomic<bool>& cancel) { hidden::Baker::bakeAll(bake, sampleRate, cancel); });
    }
    // Hidden lines removed from the frames at `times`, to look at those.
    void bakeAt(const std::vector<double>& times) {
        bake([this, &times](hidden::Bake& bake, const std::atomic<bool>& cancel) { hidden::Baker::bakeFrames(bake, sampleRate, times, cancel); });
    }
    double duration() const { return prepared->duration; }

private:
    // Nothing cancels work on the command line.
    static const std::atomic<bool>& never() {
        static const std::atomic<bool> flag {false};
        return flag;
    }
    template <typename Baking>
    void bake(Baking&& baking) {
        if (prepared->hiddenLines == nullptr) { return; }
        const auto start = juce::Time::getMillisecondCounterHiRes();
        baking(*prepared->hiddenLines, never());
        bakeMilliseconds = juce::Time::getMillisecondCounterHiRes() - start;
    }
};
}
