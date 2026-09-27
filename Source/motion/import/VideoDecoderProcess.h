#pragma once

#include <JuceHeader.h>
#if JUCE_MAC || JUCE_LINUX
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#if JUCE_MAC
#include <crt_externs.h>
#else
extern char** environ;
#endif
#endif

namespace motion {
// JUCE's POSIX ChildProcess loses signal termination status after reaping.
// Own the decoder's wait status so a crash can never publish partial frames.
// All methods, including destruction, belong to the import worker.
class VideoDecoderProcess {
public:
    VideoDecoderProcess() = default;
    ~VideoDecoderProcess() { stop(); }
    bool start(const juce::StringArray& arguments) {
#if JUCE_MAC || JUCE_LINUX
        if (arguments.isEmpty() || pid > 0) { return false; }
        std::vector<char*> argv;
        for (const auto& argument : arguments) { argv.push_back(const_cast<char*>(argument.toRawUTF8())); }
        argv.push_back(nullptr);
        posix_spawn_file_actions_t actions;
        if (posix_spawn_file_actions_init(&actions) != 0) { return false; }
        const auto input = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
        const auto output = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        const auto error = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
#if JUCE_MAC
        auto environment = *_NSGetEnviron();
#else
        auto environment = environ;
#endif
        const auto spawned = input == 0 && output == 0 && error == 0
            ? posix_spawn(&pid, argv.front(), &actions, nullptr, argv.data(), environment) : EINVAL;
        posix_spawn_file_actions_destroy(&actions);
        if (spawned != 0) { pid = -1; return false; }
        return true;
#else
        return process.start(arguments, 0);
#endif
    }
    bool waitForProcessToFinish(int milliseconds) {
#if JUCE_MAC || JUCE_LINUX
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + milliseconds;
        do {
            if (finished) { return true; }
            int status = 0;
            const auto result = waitpid(pid, &status, WNOHANG);
            if (result == pid) {
                succeeded = WIFEXITED(status) && WEXITSTATUS(status) == 0;
                finished = true;
                return true;
            }
            if (result < 0 && errno != EINTR) { finished = true; succeeded = false; return true; }
            juce::Thread::sleep(2);
        } while (juce::Time::getMillisecondCounterHiRes() < deadline);
        return false;
#else
        const auto complete = process.waitForProcessToFinish(milliseconds);
        if (complete) { succeeded = process.getExitCode() == 0; }
        return complete;
#endif
    }
    bool wasSuccessful() const { return succeeded; }
    void stop() {
#if JUCE_MAC || JUCE_LINUX
        if (pid > 0 && !finished) {
            kill(pid, SIGKILL);
            int status = 0;
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            finished = true;
        }
#else
        if (process.isRunning()) { process.kill(); process.waitForProcessToFinish(5000); }
#endif
    }
private:
    bool succeeded = false;
#if JUCE_MAC || JUCE_LINUX
    pid_t pid = -1;
    bool finished = false;
#else
    juce::ChildProcess process;
#endif
    JUCE_DECLARE_NON_COPYABLE(VideoDecoderProcess)
};
}
