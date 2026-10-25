#include <JuceHeader.h>
#include "FrameStats.h"
#include "SignalDiff.h"
#include "ScopeRender.h"
#include "../export/SignalExporter.h"
#include "../render/gpu/GpuDevice.h"
#include "../../video/VideoEncodingConstants.h"

// osci-motion on the command line: projects drawn offline exactly as Export
// draws them, to a signal, numbers, or (rendered by the app) video and
// pictures of frames, so a change to the renderer can be seen on real
// projects and compared before and after.

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

// --rate, or 0: the project's own (48 kHz times its ratio).
double sampleRateOf(const juce::ArgumentList& args) {
    const auto text = args.getValueForOption("--rate");
    if (text.isEmpty()) { return 0; }
    const auto rate = text.getDoubleValue();
    if (!motion::Project::validSampleRate(rate)) { juce::ConsoleApplication::fail("--rate must be a sample rate in Hz from " + juce::String(motion::Project::lowestSampleRate, 0) + " to " + juce::String(motion::Project::highestSampleRate, 0) + "."); }
    return rate;
}

// The frames asked for: --at=times (seconds, comma separated), or --frames=N
// evenly over the composition (8 unless asked). A time past `last` (the last
// frame's start) is the last frame.
std::vector<double> timesOf(const juce::ArgumentList& args, double duration, double last) {
    const auto at = args.getValueForOption("--at");
    if (at.isNotEmpty()) {
        std::vector<double> times;
        for (const auto& time : juce::StringArray::fromTokens(at, ",", {})) { times.push_back(motion::Stretches::within(time.getDoubleValue(), last)); }
        return times;
    }
    const auto count = args.getValueForOption("--frames");
    return frameTimes(duration, count.isEmpty() ? 8 : std::max(1, count.getIntValue()));
}

std::vector<double> timesOf(const juce::ArgumentList& args, const Composition& composition) {
    return timesOf(args, composition.duration(), motion::Stretches::lastFrame(composition.duration(), composition.frameRate));
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

// Each frame as the Scope shows it, taken from a video the app rendered at
// `at` (its time in the video for each of `times`), as its own image, or with `sheet` all of them
// on one, in rows of four, each with its time.
void writePreviews(const juce::File& video, const std::vector<double>& times, const std::function<double(std::size_t)>& at, const juce::File& folder, const juce::String& name, bool sheet) {
    if (!folder.createDirectory()) { juce::ConsoleApplication::fail("Couldn't make " + folder.getFullPathName() + "."); }
    juce::Array<juce::File> stills;
    for (std::size_t index = 0; index < times.size(); ++index) {
        // Its index first: two times can round to the same name.
        const auto file = folder.getChildFile(name + "-" + juce::String(index + 1) + "-" + juce::String(times[index], 3) + "s.png");
        const auto taken = ScopeRender::still(video, at(index), file);
        if (taken.failed()) { juce::ConsoleApplication::fail(taken.getErrorMessage()); }
        stills.add(file);
        if (!sheet) { say("Preview " + file.getFullPathName()); }
    }
    if (!sheet) { return; }
    constexpr int size = 360, across = 4;
    const auto count = static_cast<int>(times.size());
    juce::Image image(juce::Image::RGB, size * std::min(count, across), size * ((count + across - 1) / across), true);
    juce::Graphics g(image);
    g.fillAll(juce::Colours::black);
    for (int index = 0; index < count; ++index) {
        const juce::Rectangle<int> cell(size * (index % across), size * (index / across), size, size);
        const auto still = juce::ImageFileFormat::loadFrom(stills[index]);
        // An image is drawn at the opacity of the colour last set (the cell's faint border).
        g.setOpacity(1.0f);
        g.drawImage(still, cell.toFloat(), juce::RectanglePlacement::centred);
        g.setColour(juce::Colours::white.withAlpha(.6f));
        g.drawText(juce::String(times[static_cast<std::size_t>(index)], 2) + " s", cell.reduced(8).removeFromTop(16), juce::Justification::topLeft);
        g.setColour(juce::Colours::white.withAlpha(.12f));
        g.drawRect(cell);
        stills[index].deleteFile();
    }
    const auto file = folder.getChildFile(name + "-sheet.png");
    writeImage(image, file);
    say("Sheet " + file.getFullPathName());
}

// A video of the project as Export makes it, rendered by the app.
// The kind of file a project's video goes in, as its codec has it (ProRes
// in a .mov), with the dot.
juce::String videoExtension(const juce::File& project) {
    int codec = 0;
    if (Recipe::isRecipe(project)) {
        codec = static_cast<int>(juce::JSON::parse(project.loadFileAsString()).getProperty("export", {}).getProperty("codec", 0));
    } else {
        juce::MemoryBlock bytes;
        std::unique_ptr<juce::XmlElement> xml;
        if (project.loadFileAsData(bytes)) { xml = juce::AudioProcessor::getXmlFromBinary(bytes.getData(), static_cast<int>(bytes.getSize())); }
        const auto* document = xml != nullptr ? xml->getChildByName("composition") : nullptr;
        const auto* exporting = document != nullptr ? document->getChildByName("export") : nullptr;
        codec = exporting != nullptr ? exporting->getIntAttribute("codec", 0) : 0;
    }
    const auto known = codec >= 0 && codec < static_cast<int>(VideoEncodingConstants::videoCodecs.size());
    return "." + juce::String(VideoEncodingConstants::getVideoCodecInfo(static_cast<VideoCodec>(known ? codec : 0)).defaultFileExtension);
}

juce::File renderVideo(const juce::ArgumentList& args, const juce::File& project, const juce::File& destination, const std::vector<double>& stills = {}) {
    say("Rendering " + project.getFileName() + (stills.empty() ? juce::String() : " (" + juce::String(static_cast<int>(stills.size())) + " stills)") + " with the app...");
    const auto start = juce::Time::getMillisecondCounterHiRes();
    juce::String said;
    const auto rendered = ScopeRender::video(project, destination, ScopeRender::app(args.getValueForOption("--app")), stills, &said);
    if (rendered.failed()) { juce::ConsoleApplication::fail(rendered.getErrorMessage()); }
    // The rate it rendered at, as the app said.
    for (const auto& line : juce::StringArray::fromLines(said)) {
        if (line.startsWith("osci-motion rendering")) { say(line.fromFirstOccurrenceOf(" at ", false, false).upToFirstOccurrenceOf(" to ", false, false).trim() + " in the app"); }
    }
    say("Video " + destination.getFullPathName() + " in " + juce::String((juce::Time::getMillisecondCounterHiRes() - start) / 1000.0, 1) + " s");
    return destination;
}

void video(const juce::ArgumentList& args) {
    const auto files = positional(args);
    if (files.size() != 1) { juce::ConsoleApplication::fail("video takes one project."); }
    const auto project = fileArgument(files[0]);
    if (!project.existsAsFile()) { juce::ConsoleApplication::fail("There is no project at " + project.getFullPathName() + "."); }
    const auto out = args.getValueForOption("--out");
    renderVideo(args, project, out.isNotEmpty() ? fileArgument(out) : project.getSiblingFile(sceneName(project) + videoExtension(project)));
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
    if (!destination.hasFileExtension(".osci-motion")) { juce::ConsoleApplication::fail("A project is an .osci-motion file: name --out with .osci-motion."); }
    const auto written = motion::ProjectFile::write(project, destination);
    if (written.failed()) { juce::ConsoleApplication::fail(written.getErrorMessage()); }
    // "picture": a time, in seconds, whose frame the app draws as the
    // project's picture (see ProjectSummary).
    const auto picture = juce::JSON::parse(recipe.loadFileAsString()).getProperty("picture", {});
    if (!picture.isVoid()) {
        const std::vector<double> times {motion::Stretches::within(static_cast<double>(picture), motion::Stretches::lastFrame(project.duration, project.frameRate))};
        const juce::TemporaryFile video(videoExtension(destination)), still(".png");
        renderVideo(args, destination, video.getFile(), times);
        const auto at = motion::Stretches::upTo(times, project.frameRate, project.duration).exportTime(0, times[0]) + .5 / (project.frameRate > 0 ? project.frameRate : 60.0);
        const auto taken = ScopeRender::still(video.getFile(), at, still.getFile());
        if (taken.failed()) { juce::ConsoleApplication::fail(taken.getErrorMessage()); }
        const auto jpeg = motion::ProjectSummary::encode(juce::ImageFileFormat::loadFrom(still.getFile()));
        if (jpeg.isEmpty()) { juce::ConsoleApplication::fail("Couldn't make the picture from " + still.getFile().getFullPathName() + "."); }
        const auto pictured = motion::ProjectFile::write(project, destination, jpeg);
        if (pictured.failed()) { juce::ConsoleApplication::fail(pictured.getErrorMessage()); }
        say("Picture at " + timeText(times[0]));
    }
    say("Project " + destination.getFullPathName());
}

// The beam's channels, as an Output choice plays them: --output=xy, xyz,
// xyz-inverted or xyrgb (the default).
motion::OutputMode outputOf(const juce::ArgumentList& args) {
    const auto text = args.getValueForOption("--output");
    if (text.isEmpty() || text == "xyrgb") { return motion::OutputMode::xyrgb; }
    if (text == "xy") { return motion::OutputMode::xy; }
    if (text == "xyz") { return motion::OutputMode::xyz; }
    if (text == "xyz-inverted") { return motion::OutputMode::xyzInverted; }
    juce::ConsoleApplication::fail("--output must be xy, xyz, xyz-inverted or xyrgb.");
    return motion::OutputMode::xyrgb;
}

void render(const juce::ArgumentList& args) {
    const auto files = positional(args);
    if (files.size() != 1) { juce::ConsoleApplication::fail("render takes one project."); }
    const auto project = fileArgument(files[0]);
    auto composition = open(project, sampleRateOf(args));
    const auto previews = args.getValueForOption("--previews");
    const auto out = args.getValueForOption("--out");
    if (out.isNotEmpty() && !fileArgument(out).hasFileExtension(".wav")) { juce::ConsoleApplication::fail("The signal is a WAV file: name --out with .wav."); }
    if (previews.isNotEmpty()) {
        // The Scope's picture, from a video the app renders: of only a short
        // stretch up to each frame (its persistence built as playing builds
        // it), or with --whole all of it, kept beside the pictures to watch or
        // take more from (--video). A picture is of a whole frame, so they
        // spread over the frames' starts: half a frame on from the last is
        // still within the video.
        const auto last = motion::Stretches::lastFrame(composition.duration(), composition.frameRate);
        const auto times = timesOf(args, last, last);
        const auto given = args.getValueForOption("--video");
        const auto whole = given.isNotEmpty() || args.containsOption("--whole");
        const auto folder = fileArgument(previews);
        if (!folder.createDirectory()) { juce::ConsoleApplication::fail("Couldn't make " + folder.getFullPathName() + "."); }
        const auto stretches = motion::Stretches::upTo(times, composition.frameRate, composition.duration());
        const auto halfFrame = .5 / (composition.frameRate > 0 ? composition.frameRate : 60.0);
        const juce::TemporaryFile stills(videoExtension(project));
        const auto source = given.isNotEmpty() ? fileArgument(given) : renderVideo(args, project, whole ? folder.getChildFile(sceneName(project) + videoExtension(project)) : stills.getFile(), whole ? std::vector<double> {} : times);
        writePreviews(source, times, [&](std::size_t index) { return whole ? times[index] + halfFrame : stretches.exportTime(index, times[index]) + halfFrame; }, folder, sceneName(project), args.containsOption("--sheet"));
        if (out.isEmpty()) { return; }
    }
    composition.bakeAll();
    say("Prepared in " + juce::String(composition.prepareMilliseconds, 1) + " ms, hidden lines in " + juce::String(composition.bakeMilliseconds, 1) + " ms");
    const auto destination = out.isNotEmpty() ? fileArgument(out) : project.getSiblingFile(sceneName(project) + ".wav");
    const std::atomic<bool> cancel {false};
    const auto start = juce::Time::getMillisecondCounterHiRes();
    const auto written = motion::SignalExporter::write(*composition.prepared, destination, composition.sampleRate, cancel, nullptr, nullptr, nullptr, outputOf(args));
    if (written.failed()) { juce::ConsoleApplication::fail(written.getErrorMessage()); }
    say("Signal " + destination.getFullPathName() + " (" + timeText(composition.duration()) + " in " + juce::String((juce::Time::getMillisecondCounterHiRes() - start) / 1000, 2) + " s)");
}

void stats(const juce::ArgumentList& args) {
    const auto files = positional(args);
    if (files.size() != 1) { juce::ConsoleApplication::fail("stats takes one project."); }
    auto composition = open(fileArgument(files[0]), sampleRateOf(args));
    const auto times = timesOf(args, composition);
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

// What a frame is made of once its hidden lines are removed: each clip's
// drawing and what each mirror shows, with how long and in how many pieces.
void frame(const juce::ArgumentList& args) {
    const auto files = positional(args);
    if (files.size() != 1) { juce::ConsoleApplication::fail("frame takes one project."); }
    auto composition = open(fileArgument(files[0]), sampleRateOf(args));
    const auto times = timesOf(args, composition);
    composition.bakeAt(times);
    const auto* bake = composition.prepared->hiddenLines.get();
    for (const auto time : times) {
        say("At " + timeText(time) + ":");
        const auto* baked = bake != nullptr ? bake->at(time) : nullptr;
        if (baked == nullptr) { say("  nothing baked"); continue; }
        const auto list = [](const juce::String& kind, const std::vector<motion::hidden::Frame::Entry>& entries) {
            for (const auto& entry : entries) {
                if (entry.drawing == nullptr) { say("  " + kind + " " + juce::String(static_cast<int>(entry.clip)) + ": hidden"); continue; }
                // Pieces: runs of shapes each starting where the last ended.
                int pieces = 0;
                osci::Point end;
                for (std::size_t index = 0; index < entry.drawing->shapeCount(); ++index) {
                    auto* shape = entry.drawing->shape(index);
                    const auto start = shape->nextVector(0);
                    if (index == 0 || std::abs(start.x - end.x) > 1.0e-5f || std::abs(start.y - end.y) > 1.0e-5f || std::abs(start.z - end.z) > 1.0e-5f) { ++pieces; }
                    end = shape->nextVector(1);
                }
                say("  " + kind + " " + juce::String(static_cast<int>(entry.clip)) + ": " + juce::String(static_cast<int>(entry.drawing->shapeCount())) + " shapes in " + juce::String(pieces)
                    + " pieces, length " + juce::String(entry.drawing->length(), 3) + ", weighted " + juce::String(entry.drawing->beamLength(), 3) + ", shown " + juce::String(entry.shown, 3));
            }
        };
        list("drawing", baked->drawings);
        list("reflection", baked->reflections);
        list("hatching", baked->hatching);
        // How the beam shares the cycle: each drawing's samples, beside the
        // samples its strokes need whole and the fewest it can make do with.
        motion::BeamRenderer beam;
        beam.planCycle(*composition.prepared, time, composition.sampleRate);
        const auto cycle = composition.sampleRate / composition.prepared->beamRate;
        std::int64_t drawn = 0, moving = 0;
        say("  beam (" + juce::String(cycle, 0) + " samples a cycle" + (beam.plannedSimplified() ? ", simplified to fit" : "") + "):");
        for (const auto& segment : beam.plannedSegments()) {
            if (segment.kind != motion::BeamRenderer::Kind::draw) { moving += segment.count; continue; }
            drawn += segment.count;
            const auto* drawing = segment.drawing;
            const auto clip = segment.clip != nullptr ? static_cast<int>(segment.clip - composition.prepared->clips.data()) : -1;
            const auto kind = drawing != nullptr && std::any_of(baked->reflections.begin(), baked->reflections.end(), [&](const auto& entry) { return entry.drawing.get() == drawing; }) ? "reflection" : "drawing";
            say("    " + juce::String(kind) + " " + juce::String(clip) + ": " + juce::String(segment.count) + " samples (whole " + juce::String(drawing != nullptr ? drawing->minimumTraversalSamples() : 0)
                + ", coarse " + juce::String(drawing != nullptr ? drawing->coarseTraversalSamples() : 0) + ")");
        }
        say("    drawing " + juce::String(drawn) + ", moving " + juce::String(moving));
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
    const auto last = std::min(motion::Stretches::lastFrame(first.duration(), first.frameRate), motion::Stretches::lastFrame(second.duration(), second.frameRate));
    const auto times = timesOf(args, std::min(first.duration(), second.duration()), last);
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
// Whether --gpu was given, for a command whose default differs.
bool gpuChosen = false;

// The GPU hidden lines bake on, or why none is used.
void gpu(const juce::ArgumentList&) {
    const auto* device = motion::gpu::Device::shared();
    if (device == nullptr) { juce::ConsoleApplication::fail("No GPU: " + motion::gpu::Device::unavailableReason() + ". Hidden lines bake on the CPU."); }
    say("GPU: " + device->describe() + (device->software() ? " (software)" : ""));
}

void check(const juce::ArgumentList& args) {
    // The stats kept were drawn on the CPU, which lights lines a little
    // differently (more sparsely along them) than the GPU: unless asked,
    // projects are checked as they were drawn.
    if (!gpuChosen) { motion::gpu::setMode(motion::gpu::Mode::off); }
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
            times = timesOf(args, composition);
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
            object->setProperty("rate", composition.sampleRate);
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
    // --gpu=auto|off|force, for any command.
    for (int index = 1; index < argc; ++index) {
        const juce::String argument(argv[index]);
        if (!argument.startsWith("--gpu=")) { continue; }
        const auto mode = motion::gpu::modeFrom(argument.fromFirstOccurrenceOf("=", false, false));
        if (!mode.has_value()) {
            std::cerr << "--gpu takes auto, off or force." << std::endl;
            return 1;
        }
        motion::gpu::setMode(*mode);
        gpuChosen = true;
    }
    juce::ConsoleApplication app;
    app.addHelpCommand("--help|-h", "osci-motion-cli: draws osci-motion projects offline, as Export draws them.\n"
        "Times are in seconds; --rate=Hz sets the sample rate (the project's own).\n"
        "--gpu=auto|off|force bakes hidden lines on the GPU when there is one (auto), never, or always.", true);
    app.addCommand({"make", "make <scene.recipe.json> [--out=scene.osci-motion]",
        "Makes a project from a recipe: objects from generators or files, their properties and keys, a camera.",
        "Every command takes a recipe wherever it takes a project. See Source/motion/cli/Recipe.h for the format.", make});
    app.addCommand({"render", "render <project> [--out=signal.wav] [--output=xy|xyz|xyz-inverted|xyrgb] [--previews=folder [--sheet] [--frames=N | --at=t1,t2] [--whole | --video=file.mp4] [--app=osci-motion.app]] [--rate=Hz]",
        "Writes the beam's signal (XYRGB unless --output; beside the project unless --out), and pictures of frames as the Scope shows them.",
        "With --previews and no --out, writes only the pictures, rendered by the app: a short stretch up to each frame, in seconds,\n"
        "or with --whole a video of all of it, kept in that folder (--video takes them from one rendered before). --sheet puts them on one image.", render});
    app.addCommand({"video", "video <project> [--out=file.mp4|.mov, as its codec has it] [--app=osci-motion.app]",
        "Renders the project to video with the app, as Export does.", {}, video});
    app.addCommand({"stats", "stats <project> [--frames=N | --at=t1,t2] [--json] [--rate=Hz]",
        "Each frame's beam in numbers: drawn and travelled length, brightness, a fingerprint of its samples.", {}, stats});
    app.addCommand({"diff", "diff <a.wav> <b.wav> [--tolerance=1e-4] | diff <a> <b> [--frames=N | --at=t1,t2]",
        "Compares two signals sample by sample, or two projects frame by frame.", {}, diff});
    app.addCommand({"frame", "frame <project> [--frames=N | --at=t1,t2] [--rate=Hz]",
        "What a baked frame is made of: each clip's drawing and what each mirror shows, with length and pieces.", {}, frame});
    app.addCommand({"gpu", "gpu [--gpu=auto|off|force]", "Says which GPU hidden lines bake on, or why none is used.", {}, gpu});
    app.addCommand({"check", "check <folder> [--update] [--frames=N] [--rate=Hz] [--gpu=off|auto|force]",
        "Draws every project and recipe under a folder and compares each with the stats kept beside it (on the CPU, as they were made, unless --gpu says).", {}, check});
    return app.findAndRunCommand(argc, argv);
}
