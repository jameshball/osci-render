#include "Source/motion/MotionProcessor.h"
#include "Source/motion/render/SampleClock.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <vector>

#if defined(OSCI_ALLOCATION_PROBE) && OSCI_ALLOCATION_PROBE
#include "allocation_probe.h"
#include <dlfcn.h>
#endif

namespace {
#if defined(OSCI_ALLOCATION_PROBE) && OSCI_ALLOCATION_PROBE
struct AllocationProbe {
    using Reset = void (*)();
    using Enable = void (*)(int);
    using Read = void (*)(OsciAllocationCounts*);
    Reset reset = reinterpret_cast<Reset>(dlsym(RTLD_DEFAULT, "osci_allocation_probe_reset"));
    Enable enable = reinterpret_cast<Enable>(dlsym(RTLD_DEFAULT, "osci_allocation_probe_set_enabled"));
    Read read = reinterpret_cast<Read>(dlsym(RTLD_DEFAULT, "osci_allocation_probe_read"));

    AllocationProbe() {
        if (reset == nullptr || enable == nullptr || read == nullptr) {
            throw std::runtime_error("Allocation probe build requires DYLD_INSERT_LIBRARIES pointing to its probe dylib");
        }
        using Malloc = void* (*)(std::size_t);
        using Free = void (*)(void*);
        const auto mallocFunction = reinterpret_cast<Malloc>(dlsym(RTLD_DEFAULT, "malloc"));
        const auto freeFunction = reinterpret_cast<Free>(dlsym(RTLD_DEFAULT, "free"));
        if (mallocFunction == nullptr || freeFunction == nullptr) {
            throw std::runtime_error("Allocation probe could not resolve malloc/free positive control");
        }
        reset();
        enable(1);
        auto* allocation = mallocFunction(64);
        if (allocation == nullptr) {
            enable(0);
            throw std::runtime_error("Allocation probe positive-control allocation failed");
        }
        static void* volatile positiveControl = nullptr;
        positiveControl = allocation;
        freeFunction(allocation);
        positiveControl = nullptr;
        enable(0);
        OsciAllocationCounts positiveCounts {};
        read(&positiveCounts);
        if (positiveCounts.mallocCalls == 0 || positiveCounts.freeCalls == 0) {
            throw std::runtime_error("Allocation probe positive control observed no malloc/free calls");
        }
        reset();
    }

    ~AllocationProbe() { enable(0); }
};
#endif

struct Options {
    juce::String project, output, mode = "xyrgb";
    double rate = 48000;
    int blockSize = 256, warmup = 2000, blocks = 5000;
    bool verifyOutputControls = false;
};

Options parse(const juce::StringArray& args) {
    Options options;
    for (int i = 0; i < args.size(); ++i) {
        const auto key = args[i];
        if (key == "--verify-output-controls") {
            options.verifyOutputControls = true;
            continue;
        }
        if (++i >= args.size()) {
            throw std::runtime_error("Every option requires a value");
        }
        const auto value = args[i];
        if (key == "--project") { options.project = value; }
        else if (key == "--output-mode") { options.mode = value; }
        else if (key == "--output") { options.output = value; }
        else if (key == "--sample-rate") { options.rate = value.getDoubleValue(); }
        else if (key == "--block-size") { options.blockSize = value.getIntValue(); }
        else if (key == "--warmup") { options.warmup = value.getIntValue(); }
        else if (key == "--blocks") { options.blocks = value.getIntValue(); }
        else { throw std::runtime_error("Unknown option: " + key.toStdString()); }
    }
    if (options.project.isEmpty() || !std::isfinite(options.rate) || options.rate <= 0
        || options.blockSize <= 0 || options.warmup < 0 || options.blocks < 1) {
        throw std::runtime_error("Motion benchmark requires --project and valid sample-rate, block-size, warmup and blocks");
    }
    if (options.mode != "xy" && options.mode != "xyrgb" && options.mode != "soundtrack") { throw std::runtime_error("Unknown output mode"); }
    return options;
}

class PlayHead final : public juce::AudioPlayHead {
public:
    explicit PlayHead(double rateToUse) : rate(rateToUse) {}

    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo position;
        position.setTimeInSamples(samples);
        position.setTimeInSeconds(static_cast<double>(samples) / rate);
        position.setPpqPosition(static_cast<double>(samples) * 2.0 / rate);
        position.setBpm(120.0);
        position.setIsPlaying(true);
        position.setTimeSignature(TimeSignature { 4, 4 });
        return position;
    }

    std::int64_t samples = 0;

private:
    double rate;
};

std::uint64_t hashSampleBits(std::uint64_t hash, float sample) {
    const auto bits = std::bit_cast<std::uint32_t>(sample);
    for (int shift = 0; shift < 32; shift += 8) {
        hash = (hash ^ ((bits >> shift) & 0xff)) * UINT64_C(1099511628211);
    }
    return hash;
}

void writeJson(const juce::String& path, juce::DynamicObject* object) {
    const auto json = juce::JSON::toString(juce::var(object));
    if (path.isNotEmpty()) {
        const auto file = juce::File::getCurrentWorkingDirectory().getChildFile(path);
        if (!file.getParentDirectory().createDirectory() || !file.replaceWithText(json)) {
            throw std::runtime_error("Could not write benchmark JSON");
        }
    }
    std::cout << json << std::endl;
}

void waitForPreparation(MotionProcessor& processor) {
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 30000.0;
    juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    while (processor.isPreparingComposition()) {
        juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
        processor.collectPreparedState();
        if (juce::Time::getMillisecondCounterHiRes() >= deadline) {
            throw std::runtime_error("Timed out preparing Motion composition");
        }
    }
    const auto error = processor.getPreparationError();
    if (error.isNotEmpty()) {
        throw std::runtime_error("Motion composition preparation failed: " + error.toStdString());
    }
}

struct OutputMeasurement {
    std::array<double, 5> energy {};
    double xyPeak = 0, rgbPeak = 0;
    bool finite = true;
};

void setStaticOutputControls(MotionProcessor& processor, float volume, float threshold, bool muted) {
    const auto configure = [](const std::shared_ptr<osci::Effect>& effect, float value) {
        auto* parameter = effect->parameters[0];
        parameter->setUnnormalisedValueNotifyingHost(value);
        if (parameter->lfo != nullptr) {
            parameter->lfo->setUnnormalisedValueNotifyingHost(static_cast<float>(osci::LfoType::Static));
        }
        if (parameter->sidechain != nullptr) {
            parameter->sidechain->setBoolValue(false);
        }
    };
    configure(processor.volumeEffect, volume);
    configure(processor.thresholdEffect, threshold);
    processor.muteParameter->setBoolValue(muted);
}

OutputMeasurement measureOutputAt(MotionProcessor& processor, PlayHead& playHead, juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi, double rate, double time) {
    processor.seek(time);
    playHead.samples = static_cast<std::int64_t>(std::llround(time * rate));
    constexpr int settleBlocks = 32;
    for (int block = 0; block < settleBlocks; ++block) {
        audio.clear();
        midi.clear();
        processor.processBlock(audio, midi);
        playHead.samples += audio.getNumSamples();
    }
    audio.clear();
    midi.clear();
    processor.processBlock(audio, midi);
    playHead.samples += audio.getNumSamples();
    OutputMeasurement result;
    for (int channel = 0; channel < 5; ++channel) {
        for (int sample = 0; sample < audio.getNumSamples(); ++sample) {
            const auto value = static_cast<double>(audio.getSample(channel, sample));
            if (!std::isfinite(value)) {
                result.finite = false;
                continue;
            }
            result.energy[static_cast<std::size_t>(channel)] += value * value;
            const auto magnitude = std::abs(value);
            if (channel < 2) {
                result.xyPeak = std::max(result.xyPeak, magnitude);
            } else {
                result.rgbPeak = std::max(result.rgbPeak, magnitude);
            }
        }
    }
    return result;
}

double xyEnergy(const OutputMeasurement& measurement) {
    return measurement.energy[0] + measurement.energy[1];
}

double rgbOutputEnergy(const OutputMeasurement& measurement) {
    return measurement.energy[2] + measurement.energy[3] + measurement.energy[4];
}

bool approximately(double actual, double expected, double relativeTolerance) {
    return std::abs(actual - expected) <= std::max(1.0e-12, std::abs(expected) * relativeTolerance);
}

struct OutputControlsResult {
    OutputMeasurement baseline, halfGain, highGain, thresholded, muted, restored;
};

OutputControlsResult verifyOutputControls(MotionProcessor& processor, PlayHead& playHead, juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi, double rate) {
    // Ten seconds is an active opening section in the Return Path benchmark.
    constexpr double representativeTime = 10.0;
    OutputControlsResult result;
    setStaticOutputControls(processor, 1.0f, 1.0f, false);
    result.baseline = measureOutputAt(processor, playHead, audio, midi, rate, representativeTime);
    setStaticOutputControls(processor, 0.5f, 1.0f, false);
    result.halfGain = measureOutputAt(processor, playHead, audio, midi, rate, representativeTime);
    setStaticOutputControls(processor, 3.0f, 1.0f, false);
    result.highGain = measureOutputAt(processor, playHead, audio, midi, rate, representativeTime);
    setStaticOutputControls(processor, 3.0f, 0.1f, false);
    result.thresholded = measureOutputAt(processor, playHead, audio, midi, rate, representativeTime);
    setStaticOutputControls(processor, 1.0f, 1.0f, true);
    result.muted = measureOutputAt(processor, playHead, audio, midi, rate, representativeTime);
    setStaticOutputControls(processor, 1.0f, 1.0f, false);
    result.restored = measureOutputAt(processor, playHead, audio, midi, rate, representativeTime);

    const auto valid = [](const OutputMeasurement& measurement) { return measurement.finite; };
    if (!valid(result.baseline) || !valid(result.halfGain) || !valid(result.highGain) || !valid(result.thresholded)
        || !valid(result.muted) || !valid(result.restored)) {
        throw std::runtime_error("Output-controls callback produced a non-finite sample");
    }
    if (xyEnergy(result.baseline) <= 1.0e-10 || rgbOutputEnergy(result.baseline) <= 1.0e-10) {
        throw std::runtime_error("Representative Motion section has insufficient XYRGB output for output-controls verification");
    }
    if (!approximately(xyEnergy(result.halfGain), xyEnergy(result.baseline) * 0.25, 0.04)
        || !approximately(rgbOutputEnergy(result.halfGain), rgbOutputEnergy(result.baseline), 0.01)) {
        throw std::runtime_error("Volume did not scale XY energy while preserving XYRGB colour channels");
    }
    if (result.highGain.xyPeak <= 0.15 || result.thresholded.xyPeak < 0.099 || result.thresholded.xyPeak > 0.10001
        || !approximately(rgbOutputEnergy(result.thresholded), rgbOutputEnergy(result.highGain), 0.01)) {
        throw std::runtime_error("Threshold did not clamp XY while preserving XYRGB colour channels");
    }
    if (xyEnergy(result.muted) != 0 || rgbOutputEnergy(result.muted) != 0
        || !approximately(xyEnergy(result.restored), xyEnergy(result.baseline), 0.02)
        || !approximately(rgbOutputEnergy(result.restored), rgbOutputEnergy(result.baseline), 0.01)) {
        throw std::runtime_error("Mute did not clear output or restored output diverged from baseline");
    }
    return result;
}
}

int runBenchmark() {
    try {
        const auto options = parse(juce::JUCEApplication::getCommandLineParameterArray());
        const auto profileHome = juce::SystemStats::getEnvironmentVariable("CFFIXED_USER_HOME", "");
        const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
        if (profileHome.isEmpty() || home != juce::File(profileHome)
            || !juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).isAChildOf(home)) {
            throw std::runtime_error("Set HOME and CFFIXED_USER_HOME to an isolated benchmark directory");
        }
        juce::AudioProcessor::setTypeOfNextNewPlugin(juce::AudioProcessor::wrapperType_Standalone);
        MotionProcessor processor;
        PlayHead playHead(options.rate);
        processor.setPlayHead(&playHead);

        juce::MemoryBlock state;
        const auto project = juce::File::getCurrentWorkingDirectory().getChildFile(options.project);
        if (!project.loadFileAsData(state)) {
            throw std::runtime_error("Could not read Motion project");
        }
        const auto generation = processor.document.generation();
        processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        if (processor.document.generation() == generation || processor.document.project().tracks.empty()) {
            throw std::runtime_error("Motion project failed to load or contains no tracks");
        }
        // This isolated offline test bypasses the UI acknowledgement gate only;
        // it does not modify the user's legal state or run a device.
        processor.legalNoticePending.store(false);
        auto layout = processor.getBusesLayout();
        const auto channels = options.mode == "xyrgb" ? 5 : 2;
        layout.outputBuses.set(0, juce::AudioChannelSet::discreteChannels(channels));
        if (!processor.setBusesLayout(layout)) { throw std::runtime_error("Cannot configure five-channel XYRGB output"); }
        processor.setOutputMode(options.mode == "xyrgb" ? MotionProcessor::OutputMode::xyrgb : options.mode == "xy" ? MotionProcessor::OutputMode::xy : MotionProcessor::OutputMode::soundtrack);
        processor.muteParameter->setBoolValue(false);
        processor.setRateAndBufferSizeDetails(options.rate, options.blockSize);
        processor.prepareToPlay(options.rate, options.blockSize);
        waitForPreparation(processor);

        juce::AudioBuffer<float> audio(channels, options.blockSize);
        juce::MidiBuffer midi;
        const auto process = [&] {
            audio.clear();
            midi.clear();
            processor.processBlock(audio, midi);
            playHead.samples += options.blockSize;
        };
        processor.playing.store(true);
        for (int index = 0; index < options.warmup; ++index) {
            process();
        }
        std::optional<OutputControlsResult> outputControls;
        if (options.verifyOutputControls) {
            if (options.mode != "xyrgb") {
                throw std::runtime_error("--verify-output-controls requires --output-mode xyrgb");
            }
            outputControls = verifyOutputControls(processor, playHead, audio, midi, options.rate);
        }
        processor.seek(0.0);
        playHead.samples = 0;
        processor.collectPreparedState();

#if defined(OSCI_ALLOCATION_PROBE) && OSCI_ALLOCATION_PROBE
        AllocationProbe allocationProbe;
#endif
        std::vector<double> timings(static_cast<std::size_t>(options.blocks));
        std::vector<std::uint64_t> hashes(channels, UINT64_C(14695981039346656037));
        std::vector<double> channelEnergy(static_cast<std::size_t>(channels), 0.0);
        double energy = 0, rgbEnergy = 0, peak = 0, maxPositionError = 0;
        std::int64_t nonfinite = 0, lateBlocks = 0;
        const auto deadline = options.blockSize / options.rate;
        const auto durationSamples = motion::sampleIndex(processor.document.project().duration, options.rate).value_or(0);
        if (durationSamples < 1) { throw std::runtime_error("Invalid project sample duration"); }
        for (int index = 0; index < options.blocks; ++index) {
            audio.clear();
            midi.clear();
#if defined(OSCI_ALLOCATION_PROBE) && OSCI_ALLOCATION_PROBE
            allocationProbe.enable(1);
#endif
            const auto start = juce::Time::getHighResolutionTicks();
            processor.processBlock(audio, midi);
            const auto end = juce::Time::getHighResolutionTicks();
#if defined(OSCI_ALLOCATION_PROBE) && OSCI_ALLOCATION_PROBE
            allocationProbe.enable(0);
#endif
            const auto seconds = juce::Time::highResolutionTicksToSeconds(end - start);
            timings[static_cast<std::size_t>(index)] = seconds;
            lateBlocks += seconds > deadline ? 1 : 0;
            playHead.samples += options.blockSize;
            const auto expectedPosition = static_cast<double>(playHead.samples % durationSamples) / options.rate;
            maxPositionError = std::max(maxPositionError, std::abs(processor.position.load() - expectedPosition));
            for (int channel = 0; channel < audio.getNumChannels(); ++channel) {
                for (int sample = 0; sample < audio.getNumSamples(); ++sample) {
                    const auto value = audio.getSample(channel, sample);
                    hashes[static_cast<std::size_t>(channel)] = hashSampleBits(hashes[static_cast<std::size_t>(channel)], value);
                    if (!std::isfinite(value)) {
                        ++nonfinite;
                    } else {
                        energy += static_cast<double>(value) * value;
                        channelEnergy[static_cast<std::size_t>(channel)] += static_cast<double>(value) * value;
                        if (channel >= 2) { rgbEnergy += static_cast<double>(value) * value; }
                        peak = std::max(peak, std::abs(static_cast<double>(value)));
                    }
                }
            }
        }
        processor.playing.store(false);
        processor.releaseResources();
        processor.setPlayHead(nullptr);

        const auto mean = std::accumulate(timings.begin(), timings.end(), 0.0) / options.blocks;
        std::sort(timings.begin(), timings.end());
        const auto percentile = [&](double fraction) { return timings[static_cast<std::size_t>(std::ceil(fraction * options.blocks)) - 1]; };
        auto result = std::make_unique<juce::DynamicObject>();
        result->setProperty("product", "motion");
        result->setProperty("project", options.project);
        result->setProperty("project_duration", processor.document.project().duration);
        result->setProperty("tracks", static_cast<int>(processor.document.project().tracks.size()));
        result->setProperty("profile_home", home.getFullPathName());
        result->setProperty("sample_rate", options.rate);
        result->setProperty("block_size", options.blockSize);
        result->setProperty("warmup_blocks", options.warmup);
        result->setProperty("measured_blocks", options.blocks);
        result->setProperty("measured_seconds", static_cast<double>(options.blocks) * options.blockSize / options.rate);
        result->setProperty("output_mode", options.mode);
        result->setProperty("output_controls_verified", outputControls.has_value());
        if (outputControls.has_value()) {
            auto controls = std::make_unique<juce::DynamicObject>();
            controls->setProperty("representative_time_seconds", 10.0);
            controls->setProperty("baseline_xy_energy", xyEnergy(outputControls->baseline));
            controls->setProperty("half_gain_xy_energy", xyEnergy(outputControls->halfGain));
            controls->setProperty("baseline_rgb_energy", rgbOutputEnergy(outputControls->baseline));
            controls->setProperty("threshold_xy_peak", outputControls->thresholded.xyPeak);
            controls->setProperty("threshold_rgb_energy", rgbOutputEnergy(outputControls->thresholded));
            controls->setProperty("muted_xy_energy", xyEnergy(outputControls->muted));
            controls->setProperty("muted_rgb_energy", rgbOutputEnergy(outputControls->muted));
            result->setProperty("output_controls", juce::var(controls.release()));
        }
        result->setProperty("mean_us", mean * 1.0e6);
        result->setProperty("p50_us", percentile(0.50) * 1.0e6);
        result->setProperty("p95_us", percentile(0.95) * 1.0e6);
        result->setProperty("p99_us", percentile(0.99) * 1.0e6);
        result->setProperty("max_us", timings.back() * 1.0e6);
        result->setProperty("mean_deadline_fraction", mean / deadline);
        result->setProperty("p99_deadline_fraction", percentile(0.99) / deadline);
        result->setProperty("max_deadline_fraction", timings.back() / deadline);
        result->setProperty("late_block_count", lateBlocks);
        result->setProperty("nonfinite_samples", juce::var(nonfinite));
        result->setProperty("energy", energy);
        result->setProperty("rgb_energy", rgbEnergy);
        juce::Array<juce::var> energies;
        for (const auto channel : channelEnergy) { energies.add(channel); }
        result->setProperty("channel_energy", energies);
        result->setProperty("max_position_error_seconds", maxPositionError);
        result->setProperty("final_position_seconds", processor.position.load());
        result->setProperty("peak", peak);
        juce::Array<juce::var> outputHashes;
        for (const auto hash : hashes) {
            outputHashes.add(juce::String::toHexString(static_cast<juce::int64>(hash)).paddedLeft('0', 16));
        }
        result->setProperty("channel_sample_bit_hashes", outputHashes);
        result->setProperty("sample_bit_hash_algorithm", "FNV-1a-64 over float32 bytes, least-significant byte first");
#if defined(OSCI_ALLOCATION_PROBE) && OSCI_ALLOCATION_PROBE
        OsciAllocationCounts counts {};
        allocationProbe.read(&counts);
        result->setProperty("allocation_instrumented", true);
        auto operations = std::make_unique<juce::DynamicObject>();
        operations->setProperty("malloc", static_cast<juce::int64>(counts.mallocCalls));
        operations->setProperty("calloc", static_cast<juce::int64>(counts.callocCalls));
        operations->setProperty("realloc", static_cast<juce::int64>(counts.reallocCalls));
        operations->setProperty("free", static_cast<juce::int64>(counts.freeCalls));
        result->setProperty("allocator_operations", juce::var(operations.release()));
#else
        result->setProperty("allocation_instrumented", false);
#endif
        result->setProperty("timing_scope", "public processBlock only; no editor or audio device; not a realtime device test");
        writeJson(options.output, result.release());
        return nonfinite == 0 && channelEnergy[0] + channelEnergy[1] > 0 && (options.mode != "xyrgb" || rgbEnergy > 0) && maxPositionError < 0.5 / options.rate ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}

class BenchmarkApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "osci-motion benchmark"; }
    const juce::String getApplicationVersion() override { return "1"; }
    void initialise(const juce::String&) override {
        setApplicationReturnValue(runBenchmark());
        quit();
    }
    void shutdown() override {}
};

START_JUCE_APPLICATION(BenchmarkApplication)
JUCE_MAIN_FUNCTION_DEFINITION
