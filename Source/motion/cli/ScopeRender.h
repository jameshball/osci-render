#pragma once

#include "Recipe.h"
#include "../export/Stretches.h"

namespace motion::cli {
// The Scope's own picture of a project: the app renders it to video as Export
// does (its beam, glow and persistence), and stills are taken from the video.
// One renderer, never a likeness of it.
struct ScopeRender {
    // The app to render with: `given` (--app), else $OSCI_MOTION_APP, else the
    // newest build beside this one in the repository.
    static juce::File app(const juce::String& given) {
        const auto named = given.isNotEmpty() ? given : juce::SystemStats::getEnvironmentVariable("OSCI_MOTION_APP", {});
        if (named.isNotEmpty()) { return executableOf(juce::File::getCurrentWorkingDirectory().getChildFile(named)); }
        // This program is at Builds/MotionCli/<platform>/build/<config>/.
        auto root = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
        for (int level = 0; level < 6; ++level) { root = root.getParentDirectory(); }
        juce::File newest;
        for (const auto* config : {"Release", "Debug"}) {
            const auto found = executableOf(root.getChildFile("Builds/osci-motion/MacOSX/build").getChildFile(config).getChildFile("osci-motion.app"));
            if (found.existsAsFile() && (newest == juce::File() || found.getLastModificationTime() > newest.getLastModificationTime())) { newest = found; }
        }
        return newest;
    }
    // FFmpeg as the app downloads it.
    static juce::File ffmpeg() {
#if JUCE_WINDOWS
        const juce::String name = "ffmpeg.exe";
#else
        const juce::String name = "ffmpeg";
#endif
#if JUCE_MAC
        const juce::String folder = "Application Support/osci-render";
#else
        const juce::String folder = "osci-render";
#endif
        return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile(folder).getChildFile(name);
    }

    // `project` (a recipe is made into one first) rendered to `destination`
    // by `app`: all of it, or with `stills` only the stretches up to them
    // (see Stretches), one after another, a frame of each in seconds.
    // `said`: what the app said of it (its rate among it).
    static juce::Result video(const juce::File& project, const juce::File& destination, const juce::File& app, const std::vector<double>& stills = {}, juce::String* said = nullptr) {
        if (!app.existsAsFile()) { return juce::Result::fail("There is no osci-motion app to render with: build it, or pass --app=<osci-motion.app>."); }
        auto source = project;
        juce::TemporaryFile made(".osci-motion");
        if (Recipe::isRecipe(project)) {
            Project built;
            const auto read = Recipe::read(project, built);
            if (read.failed()) { return read; }
            const auto written = ProjectFile::write(built, made.getFile());
            if (written.failed()) { return written; }
            source = made.getFile();
        }
        destination.deleteFile();
        juce::ChildProcess process;
        juce::StringArray command {app.getFullPathName(), "--render-video", destination.getFullPathName(), source.getFullPathName()};
        if (!stills.empty()) {
            juce::StringArray times;
            for (const auto time : stills) { times.add(juce::String(time, 6)); }
            command.add("--stills=" + times.joinIntoString(","));
        }
        if (!process.start(command)) {
            return juce::Result::fail("Couldn't start " + app.getFullPathName() + ".");
        }
        const auto output = process.readAllProcessOutput();
        const auto exitCode = process.getExitCode();
        if (said != nullptr) { *said = output; }
        if (exitCode != 0 || !destination.existsAsFile()) {
            return juce::Result::fail("The app couldn't render it" + (output.trim().isEmpty() ? juce::String(".") : ":\n" + output.trim()));
        }
        return juce::Result::ok();
    }

    // The frame of `video` at `time`, as an image.
    static juce::Result still(const juce::File& video, double time, const juce::File& image) {
        const auto tool = ffmpeg();
        if (!tool.existsAsFile()) { return juce::Result::fail("FFmpeg isn't at " + tool.getFullPathName() + ": export a video from the app once to download it."); }
        image.deleteFile();
        juce::ChildProcess process;
        const juce::StringArray command {tool.getFullPathName(), "-v", "error", "-ss", juce::String(std::max(0.0, time), 3), "-i", video.getFullPathName(), "-frames:v", "1", "-y", image.getFullPathName()};
        if (!process.start(command)) { return juce::Result::fail("Couldn't start FFmpeg."); }
        const auto output = process.readAllProcessOutput();
        if (process.getExitCode() != 0 || !image.existsAsFile()) { return juce::Result::fail("FFmpeg couldn't take the frame at " + juce::String(time, 3) + " s: " + output.trim()); }
        return juce::Result::ok();
    }

private:
    // A macOS bundle's program, or the file itself.
    static juce::File executableOf(const juce::File& file) {
        if (file.isDirectory() && file.hasFileExtension(".app")) { return file.getChildFile("Contents/MacOS").getChildFile(file.getFileNameWithoutExtension()); }
        return file;
    }
};
}
