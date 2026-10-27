#include <JuceHeader.h>
#include <osci_render_core/concurrency/osci_Subprocess.h>

namespace {

class SubprocessTests final : public juce::UnitTest {
public:
    SubprocessTests() : juce::UnitTest("Subprocess", "Recording") {}

    void runTest() override {
#if JUCE_MAC || JUCE_LINUX
        beginTest("A child that exits with 0 exited cleanly");
        {
            osci::Subprocess process;
            expect(process.start(shell("exit 0"), osci::Subprocess::Output::discard));
            expect(process.wait(-1));
            expect(process.exitedCleanly());
        }

        beginTest("A child that exits with an error did not exit cleanly");
        {
            osci::Subprocess process;
            expect(process.start(shell("exit 3"), osci::Subprocess::Output::discard));
            expect(process.wait(-1));
            expect(!process.exitedCleanly());
        }

        beginTest("A child killed by a signal did not exit cleanly");
        {
            osci::Subprocess process;
            expect(process.start(shell("kill -9 $$"), osci::Subprocess::Output::capture));
            expectEquals(readAll(process), juce::String());
            expect(process.wait(-1));
            expect(!process.exitedCleanly());
        }

        beginTest("Standard output and error are captured together");
        {
            osci::Subprocess process;
            expect(process.start(shell("echo out; echo err 1>&2"), osci::Subprocess::Output::capture));
            const auto output = readAll(process);
            expect(process.wait(-1));
            expect(process.exitedCleanly());
            expect(output.contains("out"));
            expect(output.contains("err"));
        }

        beginTest("A running child is still running after a short wait, and killing it is not a clean exit");
        {
            osci::Subprocess process;
            expect(process.start(shell("sleep 5"), osci::Subprocess::Output::discard));
            expect(!process.wait(50));
            process.kill();
            expect(process.wait(5000));
            expect(!process.exitedCleanly());
        }

        beginTest("Destroying a running child does not wait for it to finish");
        {
            const auto started = juce::Time::getMillisecondCounterHiRes();
            {
                osci::Subprocess process;
                expect(process.start(shell("sleep 30"), osci::Subprocess::Output::capture));
            }
            expect(juce::Time::getMillisecondCounterHiRes() - started < 10000);
        }

        beginTest("A child that was never started reads nothing and counts as finished, not clean");
        {
            osci::Subprocess process;
            char buffer[16];
            expectEquals(process.read(buffer, sizeof(buffer)), 0);
            expect(process.wait(0));
            expect(!process.exitedCleanly());
            expect(!process.start({}, osci::Subprocess::Output::discard));
        }
#endif
    }

private:
    static juce::StringArray shell(const juce::String& script) { return {"/bin/sh", "-c", script}; }

    static juce::String readAll(osci::Subprocess& process) {
        juce::MemoryOutputStream output;
        char buffer[256];
        for (;;) {
            const auto count = process.read(buffer, sizeof(buffer));
            if (count <= 0) {
                break;
            }
            output.write(buffer, static_cast<std::size_t>(count));
        }
        return output.toString();
    }
};

static SubprocessTests subprocessTests;

} // namespace
