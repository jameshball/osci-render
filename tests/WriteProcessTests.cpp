#include <JuceHeader.h>
#include <osci_render_core/concurrency/osci_WriteProcess.h>

namespace {

class WriteProcessTests final : public juce::UnitTest {
public:
    WriteProcessTests() : juce::UnitTest("WriteProcess", "Recording") {}

    void runTest() override {
        std::vector<unsigned char> frame(2 * 1024 * 1024);
        for (size_t i = 0; i < frame.size(); ++i) {
            frame[i] = static_cast<unsigned char>(i);
        }

        beginTest("Complete writes preserve binary bytes and flush on close");
        juce::TemporaryFile output;
        osci::WriteProcess process;
        const bool started = process.start(readerCommand(output.getFile()));
        expect(started);
        if (started) {
            expectEquals(process.write(frame.data(), frame.size(), 5000), frame.size());
            expect(process.close());
            juce::MemoryBlock result;
            expect(output.getFile().loadFileAsData(result));
            expectEquals(result.getSize(), frame.size());
            expect(result == juce::MemoryBlock(frame.data(), frame.size()));
            expect(!process.isRunning());
        }

        beginTest("A child that consumes the frame but fails finalization reports failure");
        expect(process.start(readerCommand(output.getFile()) + " && " + exitCommand(1)));
        expectEquals(process.write(frame.data(), frame.size(), 5000), frame.size());
        expect(!process.close());
        expect(!process.isRunning());
        expect(process.close());

        beginTest("A non-reading child times out without retaining the frame or writer");
        for (int attempt = 0; attempt < 2; ++attempt) {
            expect(process.start(nonReadingCommand()));
            const auto before = juce::Time::getMillisecondCounterHiRes();
            expectEquals(process.write(frame.data(), frame.size(), 50), size_t(0));
            expect(juce::Time::getMillisecondCounterHiRes() - before < 1500.0);
            expect(!process.isRunning());
        }

        beginTest("Early child exit reports a broken pipe without changing SIGPIPE policy");
#if !JUCE_WINDOWS
        struct sigaction previous {};
        sigaction(SIGPIPE, nullptr, &previous);
#endif
        expect(process.start(exitCommand()));
        expectEquals(process.write(frame.data(), frame.size(), 2000), size_t(0));
        expect(!process.isRunning());
#if !JUCE_WINDOWS
        struct sigaction current {};
        sigaction(SIGPIPE, nullptr, &current);
        expect(current.sa_handler == previous.sa_handler);
#endif

        beginTest("Shutdown bounds a child that does not exit on EOF");
        expect(process.start(nonReadingCommand()));
        const auto before = juce::Time::getMillisecondCounterHiRes();
        expect(!process.close(50));
        expect(juce::Time::getMillisecondCounterHiRes() - before < 1500.0);
        expect(!process.isRunning());
        expect(process.close(0));

#if JUCE_WINDOWS
        beginTest("A GUI host without console handles can launch a writer");
        const auto savedOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        const auto savedError = GetStdHandle(STD_ERROR_HANDLE);
        SetStdHandle(STD_OUTPUT_HANDLE, nullptr);
        SetStdHandle(STD_ERROR_HANDLE, INVALID_HANDLE_VALUE);
        const bool guiStarted = process.start(readerCommand(output.getFile()));
        SetStdHandle(STD_OUTPUT_HANDLE, savedOutput);
        SetStdHandle(STD_ERROR_HANDLE, savedError);
        expect(guiStarted);
        expectEquals(process.write(frame.data(), frame.size(), 5000), frame.size());
        expect(process.close());
        expectEquals(output.getFile().getSize(), static_cast<juce::int64>(frame.size()));
#endif

        beginTest("The writer can restart after cancellation");
        expect(process.start(readerCommand(output.getFile())));
        expectEquals(process.write(frame.data(), frame.size(), 5000), frame.size());
        expect(process.close());
        expectEquals(output.getFile().getSize(), static_cast<juce::int64>(frame.size()));
    }

private:
    static juce::String readerCommand(const juce::File& file) {
#if JUCE_WINDOWS
        return "powershell -NoProfile -NonInteractive -Command \"$s = [Console]::OpenStandardInput(); "
               "$f = [IO.File]::Create('" + file.getFullPathName().replace("'", "''")
            + "'); $s.CopyTo($f); $f.Dispose()\"";
#else
        return "cat > '" + file.getFullPathName().replace("'", "'\\''") + "'";
#endif
    }

    static juce::String nonReadingCommand() {
#if JUCE_WINDOWS
        return "ping -n 31 127.0.0.1 > nul";
#else
        return "sleep 30 & wait";
#endif
    }

    static juce::String exitCommand(int code = 0) {
#if JUCE_WINDOWS
        return "exit /b " + juce::String(code);
#else
        return "exit " + juce::String(code);
#endif
    }
};

static WriteProcessTests writeProcessTests;

} // namespace
