#pragma once

#include <JuceHeader.h>
#include "../../modules/laser_dac_c/include/laser_dac_c.h"

enum class OscirenderLaserLinkState {
    notInstalled,
    detected,
    launching,
    connecting,
    streaming,
    stopped,
    fault,
};

struct OscirenderLaserLinkSnapshot {
    OscirenderLaserLinkState state = OscirenderLaserLinkState::notInstalled;
    juce::String message;
    juce::String installedApplicationPath;
    bool installed = false;
    bool streamingRequested = false;
};

class OscirenderLaserAdapter final : private juce::Thread, private juce::Timer {
public:
    OscirenderLaserAdapter();
    ~OscirenderLaserAdapter() override;

    void prepare(double sampleRate, int maximumBlockSize);
    void releaseResources() noexcept;
    void processPostEffects(const juce::AudioBuffer<float>& xyzRgbBuffer, bool hostMayOutput) noexcept;

    void refreshInstallationStatus();
    bool launchApplication();
    void startStreaming();
    void stopStreaming() noexcept;
    [[nodiscard]] OscirenderLaserLinkSnapshot getSnapshot() const;

private:
    struct SourceBlock {
        static constexpr int maximumSamples = 2048;
        int sampleCount = 0;
        std::array<std::array<float, maximumSamples>, 5> channels;
    };

    class SourceQueue {
    public:
        [[nodiscard]] bool push(const juce::AudioBuffer<float>& input, int offset, int sampleCount) noexcept;
        [[nodiscard]] bool pop(SourceBlock& destination, int minimumSamples) noexcept;
        void clear() noexcept;
    private:
        static constexpr std::uint64_t capacity = 65536;
        std::array<std::array<float, capacity>, 5> channels;
        std::atomic<std::uint64_t> writeIndex {0};
        std::atomic<std::uint64_t> readIndex {0};
    };

    void run() override;
    void timerCallback() override;
    bool connectToReceiver();
    void closeSession() noexcept;
    void submit(const SourceBlock& block);
    void publishState(OscirenderLaserLinkState state, juce::String message);
    [[nodiscard]] static juce::File findInstalledApplication();

    SourceQueue sourceQueue;
    std::atomic<bool> streamingRequested {false};
    std::atomic<bool> sourceFault {false};
    std::atomic<bool> hostOutputAllowed {false};
    std::atomic<int> pointRate {48000};
    ldc_device* device = nullptr;
    ldc_session* session = nullptr;
    SourceBlock consumerBlock;
    std::vector<ldc_point> convertedPoints;
    mutable juce::SpinLock snapshotLock;
    OscirenderLaserLinkSnapshot snapshot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscirenderLaserAdapter)
};
