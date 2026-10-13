#include <JuceHeader.h>
#include "FrameStats.h"
#include "SignalDiff.h"
#include "../export/SignalExporter.h"

// osci-motion on the command line: projects drawn offline exactly as Export
// draws them, to a signal, preview images or numbers, so a change to the
// renderer can be seen on real projects and compared before and after.

namespace {
using namespace motion::cli;

juce::File fileArgument(const juce::String& text) { return juce::File::getCurrentWorkingDirectory().getChildFile(text); }

// A project's or recipe's name, for what is written from it.
juce::String sceneName(const juce::File& file) {
    const auto name = file.getFileName();
    return Recipe::isRecipe(file) ? name.dropLastCharacters(juce::String(".recipe.json").length()) : file.getFileNameWithoutExtension();
}

// The arguments after the command that are not options.
juce::StringArray positional(const juce::ArgumentList& args) {
    juce::StringArray found;
    for (int index = 1; index < args.size(); ++index) {
        if (!args[index].isOption()) { found.add(args[index].text); }
    }
    return found;
}

double sampleRateOf(const juce::ArgumentList& args) {
    const auto text = args.getValueForOption("--rate");
    const auto rate = text.isEmpty() ? 48000.0 : text.getDoubleValue();
    if (!(rate >= 1000)) { juce::ConsoleApplication::fail("--rate must be a sample rate in Hz, such as 48000."); }
    return rate;
}

// The frames asked for: --at=times (seconds, comma separated), or --frames=N
// evenly over the composition (8 unless asked).
std::vector<double> timesOf(const juce::ArgumentList& args, double duration) {
    const auto at = args.getValueForOption("--at");
    if (at.isNotEmpty()) {
        std::vector<double> times;
        for (const auto& time : juce::StringArray::fromTokens(at, ",", {})) { times.push_back(time.getDoubleValue()); }
        return times;
    }
    const auto count = args.getValueForOption("--frames");
    return frameTimes(duration, count.isEmpty() ? 8 : std::max(1, count.getIntValue()));
}

Composition open(const juce::File& file, double sampleRate) {
    if (!file.existsAsFile()) { juce::ConsoleApplication::fail("There is no project at " + file.getFullPathName() + "."); }
    Composition composition;
    const auto opened = Composition::open(file, sampleRate, composition);
    if (opened.failed()) { juce::ConsoleApplication::fail(file.getFileName() + ": " + opened.getErrorMessage()); }
    return composition;
}

void say(const juce::String& line) { std::cout << line << std::endl; }

juce::String timeText(double seconds) { return juce::String(seconds, 3) + " s"; }

std::vector<BeamFrame::Stats> statsAt(const Composition& composition, const std::vector<double>& times) {
    std::vector<BeamFrame::Stats> stats;
    for (const auto time : times) { stats.push_back(BeamFrame::at(composition, time).stats()); }
    return stats;
}

void writeImage(const juce::Image& image, const juce::File& file) {
    file.deleteFile();
    juce::FileOutputStream stream(file);
    if (!stream.openedOk() || !juce::PNGImageFormat().writeImageToStream(image, stream)) { juce::ConsoleApplication::fail("Couldn't write " + file.getFullPathName() + "."); }
}

// Each frame's preview as its own image, or with `sheet` all of them on one,
// in rows of four, each with its time.
void writePreviews(const Composition& composition, const std::vector<double>& times, const juce::File& folder, const juce::String& name, bool sheet) {
    if (!folder.createDirectory()) { juce::ConsoleApplication::fail("Couldn't make " + folder.getFullPathName() + "."); }
    if (!sheet) {
        for (const auto time : times) {
            const auto file = folder.getChildFile(name + "-" + juce::String(time, 3) + "s.png");
            writeImage(BeamFrame::at(composition, time).preview(640), file);
            say("Preview " + file.getFullPathName());
        }
        return;
    }
    constexpr int size = 360, across = 4;
    const auto count = static_cast<int>(times.size());
    juce::Image image(juce::Image::RGB, size * std::min(count, across), size * ((count + across - 1) / across), true);
    juce::Graphics g(image);
    g.fillAll(juce::Colours::black);
    for (int index = 0; index < count; ++index) {
        const juce::Rectangle<int> cell(size * (index % across), size * (index / across), size, size);
        const auto time = times[static_cast<std::size_t>(index)];
        g.drawImageAt(BeamFrame::at(composition, time).preview(size), cell.getX(), cell.getY());
        g.setColour(juce::Colours::white.withAlpha(.6f));
        g.drawText(juce::String(time, 2) + " s", cell.reduced(8).removeFromTop(16), juce::Justification::topLeft);
        g.setColour(juce::Colours::white.withAlpha(.12f));
        g.drawRect(cell);
    }
    const auto file = folder.getChildFile(name + "-sheet.png");
    writeImage(image, file);
    say("Sheet " + file.getFullPathName());
}

// A recipe made into a project file, to open in the app.
void make(const juce::ArgumentList& args) {
    const auto files = positional(args);
    if (files.size() != 1) { juce::ConsoleApplication::fail("make takes one recipe."); }
    const auto recipe = fileArgument(files[0]);
    if (!Recipe::isRecipe(recipe) || !recipe.existsAsFile()) { juce::ConsoleApplication::fail("There is no recipe (<scene>.recipe.json) at " + recipe.getFullPathName() + "."); }
    motion::Project project;
    const auto read = Recipe::read(recipe, project);
    if (read.failed()) { juce::ConsoleApplication::fail(recipe.getFileName() + ": " + read.getErrorMessage()); }
    const auto out = args.getValueForOption("--out");
    const auto destination = out.isNotEmpty() ? fileArgument(out) : recipe.getSiblingFile(sceneName(recipe) + ".osci-motion");
    const auto written = motion::ProjectFile::write(project, destination);
    if (written.failed()) { juce::ConsoleApplication::fail(written.getErrorMessage()); }
    say("Project " + destination.getFullPathName());
}

void render(const juce::ArgumentList& args) {
    const auto files = positional(args);
    if (files.size() != 1) { juce::ConsoleApplication::fail("render takes one project."); }
    const auto project = fileArgument(files[0]);
    auto composition = open(project, sampleRateOf(args));
    const auto previews = args.getValueForOption("--previews");
    const auto out = args.getValueForOption("--out");
    // Previews alone need only their frames.
    const auto signal = out.isNotEmpty() || previews.isEmpty();
    const auto times = timesOf(args, composition.duration());
    if (signal) {
        composition.bakeAll();
    } else {
        composition.bakeAt(times);
    }
    say("Prepared in " + juce::String(composition.prepareMilliseconds, 1) + " ms, hidden lines in " + juce::String(composition.bakeMilliseconds, 1) + " ms");
    if (previews.isNotEmpty()) { writePreviews(composition, times, fileArgument(previews), sceneName(project), args.containsOption("--sheet")); }
    if (!signal) { return; }
    const auto destination = out.isNotEmpty() ? fileArgument(out) : project.getSiblingFile(sceneName(project) + ".wav");
    const std::atomic<bool> cancel {false};
    const auto start = juce::Time::getMillisecondCounterHiRes();
    const auto written = motion::SignalExporter::write(*composition.prepared, destination, composition.sampleRate, cancel);
    if (written.failed()) { juce::ConsoleApplication::fail(written.getErrorMessage()); }
    say("Signal " + destination.getFullPathName() + " (" + timeText(composition.duration()) + " in " + juce::String((juce::Time::getMillisecondCounterHiRes() - start) / 1000, 2) + " s)");
}

void stats(const juce::ArgumentList& args) {
    const auto files = positional(args);
    if (files.size() != 1) { juce::ConsoleApplication::fail("stats takes one project."); }
    auto composition = open(fileArgument(files[0]), sampleRateOf(args));
    const auto times = timesOf(args, composition.duration());
    composition.bakeAt(times);
    const auto frames = statsAt(composition, times);
    if (args.containsOption("--json")) {
        juce::Array<juce::var> list;
        for (const auto& frame : frames) { list.add(toVar(frame, true)); }
        say(juce::JSON::toString(juce::var(list), true));
        return;
    }
    say("Prepared in " + juce::String(composition.prepareMilliseconds, 1) + " ms, hidden lines in " + juce::String(composition.bakeMilliseconds, 1) + " ms");
    say("time        ms   samples   drawn  travel  bright   lit  fingerprint");
    for (const auto& frame : frames) {
        say(juce::String(frame.time, 3).paddedLeft(' ', 7) + juce::String(frame.milliseconds, 2).paddedLeft(' ', 8) + juce::String(frame.samples).paddedLeft(' ', 10)
            + juce::String(frame.drawn, 2).paddedLeft(' ', 8) + juce::String(frame.travelled, 2).paddedLeft(' ', 8) + juce::String(frame.brightness, 3).paddedLeft(' ', 8)
            + juce::String(frame.litShare, 2).paddedLeft(' ', 6) + "  " + frame.fingerprint);
    }
}

void diff(const juce::ArgumentList& args) {
    const auto files = positional(args);
    if (files.size() != 2) { juce::ConsoleApplication::fail("diff takes two signals (.wav) or two projects."); }
    const auto one = fileArgument(files[0]), other = fileArgument(files[1]);
    const auto toleranceText = args.getValueForOption("--tolerance");
    const auto tolerance = toleranceText.isEmpty() ? 1.0e-4 : toleranceText.getDoubleValue();
    if (one.hasFileExtension("wav") && other.hasFileExtension("wav")) {
        SignalDiff result;
        const auto compared = SignalDiff::compare(one, other, tolerance, result);
        if (compared.failed()) { juce::ConsoleApplication::fail(compared.getErrorMessage()); }
        const auto names = juce::StringArray {"x", "y", "r", "g", "b"};
        const auto channel = result.largestChannel < names.size() ? names[result.largestChannel] : juce::String(result.largestChannel);
        say("Largest difference " + juce::String(result.largest, 6) + " (" + channel + " at " + timeText(result.largestAt) + "), rms " + juce::String(result.rms, 6));
        if (result.firstAt < 0) {
            say("Same within " + juce::String(tolerance));
            return;
        }
        juce::ConsoleApplication::fail("They differ by more than " + juce::String(tolerance) + " from " + timeText(result.firstAt) + ".");
    }
    // Two projects: each frame drawn from both, side by side.
    const auto rate = sampleRateOf(args);
    auto first = open(one, rate), second = open(other, rate);
    const auto times = timesOf(args, std::min(first.duration(), second.duration()));
    first.bakeAt(times);
    second.bakeAt(times);
    int differing = 0;
    for (const auto time : times) {
        const auto a = BeamFrame::at(first, time).stats(), b = BeamFrame::at(second, time).stats();
        const auto change = changeBetween(a, b);
        say(juce::String(a.time, 3).paddedLeft(' ', 7) + "  " + (change.isEmpty() ? juce::String("same") : change));
        differing += change.isEmpty() ? 0 : 1;
    }
    if (differing > 0) { juce::ConsoleApplication::fail(juce::String(differing) + (differing == 1 ? " frame differs." : " frames differ.")); }
}

// Every project and recipe under a folder against the stats kept beside it
// (<name>.stats.json): what changed, frame by frame. --update keeps the new
// stats instead.
void check(const juce::ArgumentList& args) {
    const auto folders = positional(args);
    if (folders.size() != 1) { juce::ConsoleApplication::fail("check takes one folder of projects."); }
    const auto folder = fileArgument(folders[0]);
    if (!folder.isDirectory()) { juce::ConsoleApplication::fail("There is no folder at " + folder.getFullPathName() + "."); }
    const auto rate = sampleRateOf(args);
    const auto update = args.containsOption("--update");
    int changed = 0, added = 0, failed = 0, same = 0;
    for (const auto& entry : juce::RangedDirectoryIterator(folder, true, "*.osci-motion;*.recipe.json")) {
        const auto project = entry.getFile();
        const auto kept = project.withFileExtension("stats.json");
        const auto name = project.getRelativePathFrom(folder);
        Composition composition;
        const auto opened = Composition::open(project, rate, composition);
        if (opened.failed()) {
            say("FAILED   " + name + ": " + opened.getErrorMessage());
            ++failed;
            continue;
        }
        const auto before = juce::JSON::parse(kept);
        const auto* beforeFrames = before["frames"].getArray();
        // The frames kept before, unless others are asked for.
        std::vector<double> times;
        if (beforeFrames != nullptr && !args.containsOption("--frames") && !args.containsOption("--at")) {
            for (const auto& frame : *beforeFrames) { times.push_back(frame["time"]); }
        } else {
            times = timesOf(args, composition.duration());
        }
        composition.bakeAt(times);
        const auto frames = statsAt(composition, times);
        juce::StringArray changes;
        if (beforeFrames != nullptr) {
            for (std::size_t index = 0; index < frames.size() && index < static_cast<std::size_t>(beforeFrames->size()); ++index) {
                const auto change = changeBetween(statsFrom((*beforeFrames)[static_cast<int>(index)]), frames[index]);
                if (change.isNotEmpty()) { changes.add("  at " + timeText(frames[index].time) + ": " + change); }
            }
        }
        if (beforeFrames == nullptr || update) {
            juce::Array<juce::var> list;
            for (const auto& frame : frames) { list.add(toVar(frame, false)); }
            auto object = std::make_unique<juce::DynamicObject>();
            object->setProperty("rate", rate);
            object->setProperty("frames", list);
            if (!kept.replaceWithText(juce::JSON::toString(juce::var(object.release()), false), false, false, "\n")) { juce::ConsoleApplication::fail("Couldn't write " + kept.getFullPathName() + "."); }
        }
        if (beforeFrames == nullptr) {
            say("NEW      " + name);
            ++added;
        } else if (changes.isEmpty()) {
            ++same;
        } else {
            say((update ? "UPDATED  " : "CHANGED  ") + name);
            for (const auto& line : changes) { say(line); }
            ++changed;
        }
    }
    say(juce::String(same) + " same, " + juce::String(changed) + (update ? " updated, " : " changed, ") + juce::String(added) + " new, " + juce::String(failed) + " failed");
    if (failed > 0 || (changed > 0 && !update)) { juce::ConsoleApplication::fail({}); }
}
}

int main(int argc, char* argv[]) {
    const juce::ScopedJuceInitialiser_GUI initialiser;
    juce::ConsoleApplication app;
    app.addHelpCommand("--help|-h", "osci-motion-cli: draws osci-motion projects offline, as Export draws them.\n"
        "Times are in seconds; --rate=Hz sets the sample rate (48000).", true);
    app.addCommand({"make", "make <scene.recipe.json> [--out=scene.osci-motion]",
        "Makes a project from a recipe: objects from generators or files, their properties and keys, a camera.",
        "Every command takes a recipe wherever it takes a project. See Source/motion/cli/Recipe.h for the format.", make});
    app.addCommand({"render", "render <project> [--out=signal.wav] [--previews=folder [--sheet] [--frames=N | --at=t1,t2]] [--rate=Hz]",
        "Writes the XYRGB signal (beside the project unless --out), and preview images of frames.",
        "With --previews and no --out, writes only the previews; --sheet puts them on one image.", render});
    app.addCommand({"stats", "stats <project> [--frames=N | --at=t1,t2] [--json] [--rate=Hz]",
        "Each frame's beam in numbers: drawn and travelled length, brightness, a fingerprint of its samples.", {}, stats});
    app.addCommand({"diff", "diff <a.wav> <b.wav> [--tolerance=1e-4] | diff <a> <b> [--frames=N | --at=t1,t2]",
        "Compares two signals sample by sample, or two projects frame by frame.", {}, diff});
    app.addCommand({"check", "check <folder> [--update] [--frames=N] [--rate=Hz]",
        "Draws every project and recipe under a folder and compares each with the stats kept beside it.", {}, check});
    return app.findAndRunCommand(argc, argv);
}
