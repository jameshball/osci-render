#pragma once

#include <JuceHeader.h>
#include <thread>

namespace osci::installer {

class BlenderInstaller {
public:
    struct Target {
        juce::StringArray command;
        juce::String label;
        juce::String profile;
        bool enabled = false;
        bool conflicts = false;
    };

    static constexpr auto repositoryUrl = "https://osci-render.com/blender/index.json";
    static constexpr auto manualUrl = "https://osci-render.com/blender/";

    BlenderInstaller() : directory(juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getNonexistentChildFile("osci-blender-" + juce::Uuid().toString(), {}, false)) {
        ready = directory.createDirectory();
        if (ready.wasOk() && !script().replaceWithData(BinaryData::blender_setup_py, BinaryData::blender_setup_pySize)) {
            ready = juce::Result::fail("Could not prepare Blender setup");
        }
    }

    ~BlenderInstaller() {
        directory.deleteRecursively();
    }

    static juce::StringArray executableCommand(juce::File file) {
#if JUCE_MAC
        if (file.hasFileExtension("app")) {
            file = file.getChildFile("Contents/MacOS/Blender");
        }
#endif
        return { file.getFullPathName() };
    }

    static std::vector<juce::StringArray> candidates() {
        std::vector<juce::StringArray> result;
        juce::StringArray seen;
        const auto addFile = [&](const juce::File& file) {
            const auto command = executableCommand(file);
            if (juce::File(command[0]).existsAsFile() && !seen.contains(command[0])) {
                result.push_back(command);
                seen.add(command[0]);
            }
        };
        const auto homeDirectory = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
#if JUCE_MAC
        for (const auto& root : { juce::File("/Applications"), homeDirectory.getChildFile("Applications") }) {
            for (const auto& app : root.findChildFiles(juce::File::findDirectories, false, "*Blender*.app")) {
                addFile(app);
            }
            for (const auto& app : root.findChildFiles(juce::File::findDirectories, false, "*blender*.app")) {
                addFile(app);
            }
        }
#elif JUCE_WINDOWS
        for (const auto& hive : { "HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE" }) {
            const auto registered = juce::WindowsRegistry::getValue(juce::String(hive)
                + "\\Software\\Microsoft\\Windows\\CurrentVersion\\App Paths\\blender.exe\\");
            if (juce::File::isAbsolutePath(registered)) {
                addFile(juce::File(registered));
            }
        }
        for (const auto& variable : { "ProgramFiles", "ProgramW6432", "LOCALAPPDATA" }) {
            const auto path = juce::SystemStats::getEnvironmentVariable(variable, {});
            if (path.isEmpty()) {
                continue;
            }
            const auto root = juce::File(path).getChildFile("Blender Foundation");
            addFile(root.getChildFile("Blender/blender.exe"));
            for (const auto& folder : root.findChildFiles(juce::File::findDirectories, false, "Blender*")) {
                addFile(folder.getChildFile("blender.exe"));
            }
        }
#else
        for (const auto& root : { juce::File("/opt"), homeDirectory.getChildFile("Applications") }) {
            for (const auto& folder : root.findChildFiles(juce::File::findDirectories, false, "blender*")) {
                addFile(folder.getChildFile("blender"));
            }
        }
#endif
        const auto path = juce::SystemStats::getEnvironmentVariable("PATH", {});
#if JUCE_WINDOWS
        const auto entries = juce::StringArray::fromTokens(path, ";", "\"");
        const auto executable = "blender.exe";
#else
        const auto entries = juce::StringArray::fromTokens(path, ":", {});
        const auto executable = "blender";
        addFile(juce::File("/snap/bin/blender"));
#endif
        for (const auto& entry : entries) {
            if (juce::File::isAbsolutePath(entry)) {
                addFile(juce::File(entry).getChildFile(executable));
            }
        }
#if JUCE_LINUX
        for (const auto& root : { juce::File("/var/lib/flatpak/app/org.blender.Blender"),
                                 homeDirectory.getChildFile(".local/share/flatpak/app/org.blender.Blender") }) {
            if (root.isDirectory()) {
                result.push_back({ "flatpak", "run", "org.blender.Blender" });
                break;
            }
        }
#endif
        return result;
    }

    static bool blenderRunning() {
        juce::String output;
#if JUCE_WINDOWS
        const auto check = run({ "tasklist", "/FI", "IMAGENAME eq blender.exe", "/FO", "CSV", "/NH" }, output, 10000);
        return check.failed() || output.containsIgnoreCase("blender.exe");
#else
        juce::ChildProcess process;
        if (!process.start(juce::StringArray { "/usr/bin/pgrep", "-ix", "blender" })) {
            return true;
        }
        if (!process.waitForProcessToFinish(10000)) {
            process.kill();
            return true;
        }
        return process.getExitCode() != 1;
#endif
    }

    juce::Result probe(const juce::StringArray& command, Target& target) {
        juce::var state;
        const auto result = action(command, "inspect", false, state);
        if (result.wasOk()) {
            target = { command, "Blender " + state["version"].toString(), state["profile"].toString(),
                       static_cast<bool>(state["enabled"]), state["conflicts"].size() > 0 };
        }
        return result;
    }

    juce::Result install(const Target& target, bool replace) {
        if (blenderRunning()) {
            return juce::Result::fail("Close Blender, then retry setup. Your open scenes will not be closed automatically.");
        }
        juce::var state;
        auto result = action(target.command, "prepare", replace, state);
        if (result.wasOk()) {
            result = action(target.command, "install", false, state);
        }
        if (result.wasOk()) {
            result = action(target.command, "verify", false, state);
        }
        if (result.failed() && directory.getChildFile("journal.json").existsAsFile()) {
            const auto restored = action(target.command, "rollback", false, state);
            if (restored.failed()) {
                result = juce::Result::fail(result.getErrorMessage() + "\nCould not restore the previous add-on selection: "
                                          + restored.getErrorMessage());
            }
        }
        directory.getChildFile("journal.json").deleteFile();
        return result;
    }

    juce::File saveLog() const {
        const auto folder = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("osci-installer/blender-logs");
        const auto result = folder.createDirectory();
        if (result.failed()) {
            return {};
        }
        const auto file = folder.getNonexistentChildFile("setup", ".log", false);
        return file.replaceWithText(log) ? file : juce::File {};
    }

private:
    juce::File directory;
    juce::Result ready = juce::Result::ok();
    juce::String log;

    juce::File script() const { return directory.getChildFile("blender_setup.py"); }

    static juce::Result run(const juce::StringArray& command, juce::String& output, int timeout = 180000) {
        juce::ChildProcess process;
        if (!process.start(command)) {
            return juce::Result::fail("Could not start Blender. Choose its application or executable.");
        }
        // Drain the pipe while waiting so verbose add-ons cannot fill it and deadlock Blender.
        std::thread reader([&] {
            char buffer[4096];
            for (;;) {
                const auto bytes = process.readProcessOutput(buffer, sizeof(buffer));
                if (bytes <= 0) {
                    break;
                }
                output += juce::String::fromUTF8(buffer, bytes);
                if (output.length() > 262144) {
                    output = output.substring(output.length() - 262144);
                }
            }
        });
        const auto finished = process.waitForProcessToFinish(timeout);
        if (!finished) {
            process.kill();
        }
        reader.join();
        if (!finished) {
            return juce::Result::fail("Blender setup timed out. Close Blender and retry, or use manual installation.");
        }
        return process.getExitCode() == 0 ? juce::Result::ok() : juce::Result::fail("Blender reported an error. See the setup log.");
    }

    juce::Result action(juce::StringArray command, const juce::String& name, bool replace, juce::var& state) {
        if (ready.failed()) {
            return ready;
        }
#if JUCE_LINUX
        if (command[0] == "flatpak") {
            command.insert(2, "--filesystem=" + directory.getFullPathName());
        }
#endif
        command.addArray({ "--background", "--disable-autoexec" });
        if (name == "install") {
            // This invocation may download the requested extension. Do not alter the saved online-access preference.
            command.add("--online-mode");
        }
        command.addArray({ "--python-exit-code", "1", "--python", script().getFullPathName(), "--", name,
                           "--journal", directory.getChildFile("journal.json").getFullPathName() });
        if (replace) {
            command.add("--replace");
        }
        juce::String output;
        auto result = run(command, output);
        log += "\n" + name + "\n" + output;
        bool reported = false;
        for (const auto& line : juce::StringArray::fromLines(output)) {
            if (line.startsWith("OSCI_RESULT=")) {
                state = juce::JSON::parse(line.substring(12));
                reported = state.isObject();
            }
        }
        if (reported && !static_cast<bool>(state["ok"])) {
            return juce::Result::fail(state["error"].toString());
        }
        if (result.wasOk() && !reported) {
            result = juce::Result::fail("Blender did not return a setup result. See the setup log.");
        }
        return result;
    }
};

} // namespace osci::installer
