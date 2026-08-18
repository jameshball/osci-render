#if OSCI_PREMIUM

#include "OscirenderLaserAdapter.h"

OscirenderLaserAdapter::OscirenderLaserAdapter() : juce::Thread("osci-render external laser link") {
    convertedPoints.reserve(SourceBlock::maximumSamples);
    refreshInstallationStatus();
    startThread(juce::Thread::Priority::normal);
    startTimer(1000);
}

OscirenderLaserAdapter::~OscirenderLaserAdapter() {
    stopTimer();
    stopStreaming();
    signalThreadShouldExit();
    stopThread(5000);
    closeSession();
}

void OscirenderLaserAdapter::prepare(double sampleRate, int maximumBlockSize) {
    juce::ignoreUnused(maximumBlockSize);
    pointRate.store(juce::jlimit(1000, 100000, static_cast<int>(std::lround(sampleRate))), std::memory_order_release);
    stopStreaming();
}

void OscirenderLaserAdapter::releaseResources() noexcept {
    stopStreaming();
}

void OscirenderLaserAdapter::processPostEffects(const juce::AudioBuffer<float>& input, bool hostMayOutput) noexcept {
    const bool previouslyAllowed = hostOutputAllowed.exchange(hostMayOutput, std::memory_order_acq_rel);
    if (!hostMayOutput) {
        if (previouslyAllowed) {
            stopStreaming();
        }
        return;
    }
    if (!streamingRequested.load(std::memory_order_acquire) || input.getNumChannels() < 6 || input.getNumSamples() <= 0) {
        return;
    }
    for (int offset = 0; offset < input.getNumSamples();) {
        const int sampleCount = juce::jmin(SourceBlock::maximumSamples, input.getNumSamples() - offset);
        if (!sourceQueue.push(input, offset, sampleCount)) {
            sourceFault.store(true, std::memory_order_release);
            streamingRequested.store(false, std::memory_order_release);
            return;
        }
        offset += sampleCount;
    }
}

void OscirenderLaserAdapter::refreshInstallationStatus() {
    const auto application = findInstalledApplication();
    juce::SpinLock::ScopedLockType lock(snapshotLock);
    snapshot.installed = application.exists();
    snapshot.installedApplicationPath = application.getFullPathName();
    if (!snapshot.installed && snapshot.state != OscirenderLaserLinkState::streaming
        && snapshot.state != OscirenderLaserLinkState::connecting) {
        snapshot.state = OscirenderLaserLinkState::notInstalled;
        snapshot.message = "osci-laser is not installed.";
    } else if (snapshot.installed && snapshot.state == OscirenderLaserLinkState::notInstalled) {
        snapshot.state = OscirenderLaserLinkState::detected;
        snapshot.message = "osci-laser is installed and ready to launch.";
    }
}

bool OscirenderLaserAdapter::launchApplication() {
    refreshInstallationStatus();
    const auto current = getSnapshot();
    if (!current.installed) {
        return false;
    }
    publishState(OscirenderLaserLinkState::launching, "Launching osci-laser...");
    const bool launched = juce::File(current.installedApplicationPath).startAsProcess();
    if (!launched) {
        publishState(OscirenderLaserLinkState::fault, "The installed osci-laser application could not be launched.");
    } else {
        publishState(OscirenderLaserLinkState::detected, "osci-laser is running. Start streaming when ready.");
    }
    return launched;
}

void OscirenderLaserAdapter::startStreaming() {
    sourceQueue.clear();
    sourceFault.store(false, std::memory_order_release);
    streamingRequested.store(true, std::memory_order_release);
    publishState(OscirenderLaserLinkState::connecting, "Looking for the local osci-laser receiver...");
    notify();
}

void OscirenderLaserAdapter::stopStreaming() noexcept {
    streamingRequested.store(false, std::memory_order_release);
}

OscirenderLaserLinkSnapshot OscirenderLaserAdapter::getSnapshot() const {
    juce::SpinLock::ScopedLockType lock(snapshotLock);
    auto result = snapshot;
    result.streamingRequested = streamingRequested.load(std::memory_order_acquire);
    return result;
}

bool OscirenderLaserAdapter::SourceQueue::push(const juce::AudioBuffer<float>& input, int offset, int sampleCount) noexcept {
    const auto write = writeIndex.load(std::memory_order_relaxed);
    const auto read = readIndex.load(std::memory_order_acquire);
    if (sampleCount <= 0 || static_cast<std::uint64_t>(sampleCount) > capacity - (write - read)) {
        return false;
    }
    constexpr std::array<int, 5> sourceChannels {0, 1, 3, 4, 5};
    for (int sample = 0; sample < sampleCount; ++sample) {
        const auto destination = (write + static_cast<std::uint64_t>(sample)) % capacity;
        for (std::size_t channel = 0; channel < sourceChannels.size(); ++channel) {
            channels[channel][destination] = input.getSample(sourceChannels[channel], offset + sample);
        }
    }
    writeIndex.store(write + static_cast<std::uint64_t>(sampleCount), std::memory_order_release);
    return true;
}

bool OscirenderLaserAdapter::SourceQueue::pop(SourceBlock& destination, int minimumSamples) noexcept {
    const auto read = readIndex.load(std::memory_order_relaxed);
    const auto write = writeIndex.load(std::memory_order_acquire);
    const auto available = write - read;
    if (available < static_cast<std::uint64_t>(juce::jmax(1, minimumSamples))) {
        return false;
    }
    destination.sampleCount = static_cast<int>(juce::jmin<std::uint64_t>(SourceBlock::maximumSamples, available));
    for (int sample = 0; sample < destination.sampleCount; ++sample) {
        const auto source = (read + static_cast<std::uint64_t>(sample)) % capacity;
        for (std::size_t channel = 0; channel < destination.channels.size(); ++channel) {
            destination.channels[channel][sample] = channels[channel][source];
        }
    }
    readIndex.store(read + static_cast<std::uint64_t>(destination.sampleCount), std::memory_order_release);
    return true;
}

void OscirenderLaserAdapter::SourceQueue::clear() noexcept {
    readIndex.store(writeIndex.load(std::memory_order_acquire), std::memory_order_release);
}

void OscirenderLaserAdapter::run() {
    while (!threadShouldExit()) {
        if (sourceFault.exchange(false, std::memory_order_acq_rel)) {
            closeSession();
            sourceQueue.clear();
            publishState(OscirenderLaserLinkState::fault, "Streaming stopped because the source queue overflowed.");
        }
        if (!streamingRequested.load(std::memory_order_acquire)) {
            if (session != nullptr) {
                closeSession();
                sourceQueue.clear();
                publishState(OscirenderLaserLinkState::stopped, "Streaming stopped. osci-laser remains locally disarmed.");
            }
            wait(50);
            continue;
        }
        if (session == nullptr && !connectToReceiver()) {
            streamingRequested.store(false, std::memory_order_release);
            wait(100);
            continue;
        }
        const int minimumBatch = juce::jlimit(1, 128, pointRate.load(std::memory_order_acquire) / 250);
        if (!sourceQueue.pop(consumerBlock, minimumBatch)) {
            wait(2);
            continue;
        }
        submit(consumerBlock);
    }
}

void OscirenderLaserAdapter::timerCallback() {
    refreshInstallationStatus();
}

bool OscirenderLaserAdapter::connectToReceiver() {
    publishState(OscirenderLaserLinkState::connecting, "Connecting to the local osci-laser IDN receiver...");
    auto result = ldc_idn_open("127.0.0.1:7255", &device);
    if (result != LDC_OK) {
        closeSession();
        publishState(OscirenderLaserLinkState::fault, "The local osci-laser receiver could not be opened.");
        return false;
    }
    ldc_session_config config {};
    config.struct_size = sizeof(config);
    config.abi_version = LDC_ABI_VERSION;
    config.points_per_second = static_cast<std::uint32_t>(pointRate.load(std::memory_order_acquire));
    result = ldc_session_start(device, &config, &session);
    if (result == LDC_OK) {
        result = ldc_session_arm(session);
    }
    if (result != LDC_OK) {
        closeSession();
        publishState(OscirenderLaserLinkState::fault, "The local osci-laser receiver rejected the stream.");
        return false;
    }
    publishState(OscirenderLaserLinkState::streaming, "Streaming XYRGB to osci-laser. Arming remains local to osci-laser.");
    return true;
}

void OscirenderLaserAdapter::closeSession() noexcept {
    if (session != nullptr) {
        ldc_session_disarm(session);
        ldc_session_stop(session);
        ldc_session_destroy(session);
        session = nullptr;
    }
    if (device != nullptr) {
        ldc_device_destroy(device);
        device = nullptr;
    }
}

void OscirenderLaserAdapter::submit(const SourceBlock& block) {
    convertedPoints.resize(static_cast<std::size_t>(block.sampleCount));
    for (int sample = 0; sample < block.sampleCount; ++sample) {
        auto& point = convertedPoints[static_cast<std::size_t>(sample)];
        point.x = juce::jlimit(-1.0f, 1.0f, block.channels[0][sample]);
        point.y = juce::jlimit(-1.0f, 1.0f, block.channels[1][sample]);
        const auto convert = [](float value) {
            return static_cast<std::uint16_t>(std::lround(juce::jlimit(0.0f, 1.0f, value) * 65535.0f));
        };
        auto red = block.channels[2][sample];
        auto green = block.channels[3][sample];
        auto blue = block.channels[4][sample];
        if (red < -0.999f || green < -0.999f || blue < -0.999f) {
            red = 0.0f;
            green = 1.0f;
            blue = 0.0f;
        }
        point.r = convert(red);
        point.g = convert(green);
        point.b = convert(blue);
        point.intensity = std::numeric_limits<std::uint16_t>::max();
    }
    if (ldc_session_submit_frame(session, convertedPoints.data(), convertedPoints.size()) != LDC_OK) {
        streamingRequested.store(false, std::memory_order_release);
        closeSession();
        publishState(OscirenderLaserLinkState::fault, "The IDN stream was interrupted.");
    }
}

void OscirenderLaserAdapter::publishState(OscirenderLaserLinkState state, juce::String message) {
    juce::SpinLock::ScopedLockType lock(snapshotLock);
    snapshot.state = state;
    snapshot.message = std::move(message);
}

juce::File OscirenderLaserAdapter::findInstalledApplication() {
    const auto overriddenPath = juce::SystemStats::getEnvironmentVariable("OSCI_LASER_APP_PATH", {});
    if (overriddenPath.isNotEmpty()) {
        const auto overriddenApplication = juce::File(overriddenPath);
        if (overriddenApplication.exists()) {
            return overriddenApplication;
        }
    }
#if JUCE_MAC
    const std::array<juce::File, 2> candidates {
        juce::File("/Applications/osci-laser.app"),
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getSiblingFile("Applications").getChildFile("osci-laser.app"),
    };
#elif JUCE_WINDOWS
    const auto programFiles = juce::SystemStats::getEnvironmentVariable("ProgramFiles", "C:\\Program Files");
    const auto registeredPath = juce::WindowsRegistry::getValue("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\osci-laser.exe\\");
    if (registeredPath.isNotEmpty()) {
        const auto registeredApplication = juce::File(registeredPath);
        if (registeredApplication.existsAsFile()) {
            return registeredApplication;
        }
    }
    const std::array<juce::File, 1> candidates {juce::File(programFiles).getChildFile("osci-laser").getChildFile("osci-laser.exe")};
#else
    const std::array<juce::File, 4> candidates {
        juce::File("/usr/bin/osci-laser"),
        juce::File("/usr/local/bin/osci-laser"),
        juce::File("/opt/osci-laser/osci-laser"),
        juce::File::getSpecialLocation(juce::File::userHomeDirectory).getChildFile(".local/bin/osci-laser"),
    };
#endif
    for (const auto& candidate : candidates) {
        if (candidate.exists()) {
            return candidate;
        }
    }
    return {};
}

#endif
