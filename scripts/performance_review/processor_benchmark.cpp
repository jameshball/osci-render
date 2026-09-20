#include "Source/PluginProcessor.h"
#include "Source/parser/FileParser.h"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <cmath>
#include <iostream>
#include <numeric>
#include <stdexcept>

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
        reset();
    }
    ~AllocationProbe() { enable(0); }
};
#endif

struct Options {
    juce::String project, output, exportDefault, midi = "sustain", notes, fileCycle;
    double rate = 48000, ratio = 1, inputAmplitude = 0;
    int blockSize = 256, voices = 4, warmup = 2000, blocks = 5000, fileCycleBlocks = 256, velocity = 80;
};

Options parse(const juce::StringArray& args) {
    Options o;
    for (int i = 0; i < args.size(); ++i) {
        const auto key = args[i];
        if (++i >= args.size()) {
            throw std::runtime_error("Every option requires a value");
        }
        const auto value = args[i];
        if (key == "--project") { o.project = value; }
        else if (key == "--output") { o.output = value; }
        else if (key == "--export-default") { o.exportDefault = value; }
        else if (key == "--sample-rate") { o.rate = value.getDoubleValue(); }
        else if (key == "--input-amplitude") { o.inputAmplitude = value.getDoubleValue(); }
        else if (key == "--ratio") { o.ratio = value.getDoubleValue(); }
        else if (key == "--block-size") { o.blockSize = value.getIntValue(); }
        else if (key == "--voices") { o.voices = value.getIntValue(); }
        else if (key == "--warmup") { o.warmup = value.getIntValue(); }
        else if (key == "--blocks") { o.blocks = value.getIntValue(); }
        else if (key == "--midi") { o.midi = value; }
        else if (key == "--velocity") { o.velocity = value.getIntValue(); }
        else if (key == "--notes") { o.notes = value; }
        else if (key == "--file-cycle") { o.fileCycle = value; }
        else if (key == "--file-cycle-blocks") { o.fileCycleBlocks = value.getIntValue(); }
        else { throw std::runtime_error("Unknown option: " + key.toStdString()); }
    }
    if (!std::isfinite(o.inputAmplitude) || o.inputAmplitude < 0 || o.inputAmplitude > 1
        || !std::isfinite(o.rate) || !std::isfinite(o.ratio) || o.rate <= 0 || o.blockSize <= 0
        || o.velocity < 1 || o.velocity > 127 || o.voices < 1 || o.voices > 16 || o.warmup < 1 || o.blocks < 1 || o.fileCycleBlocks < 1
        || (o.midi != "sustain" && o.midi != "churn" && o.midi != "off")) {
        throw std::runtime_error("Invalid benchmark options");
    }
    return o;
}

uint64_t hashSampleBits(uint64_t hash, float sample) {
    const auto bits = std::bit_cast<uint32_t>(sample);
    for (int shift = 0; shift < 32; shift += 8) {
        hash = (hash ^ ((bits >> shift) & 0xff)) * UINT64_C(1099511628211);
    }
    return hash;
}

class PlayHead final : public juce::AudioPlayHead {
public:
    explicit PlayHead(double rateToUse) : rate(rateToUse) {}
    juce::Optional<PositionInfo> getPosition() const override {
        PositionInfo p;
        p.setTimeInSamples(samples);
        p.setTimeInSeconds(static_cast<double>(samples) / rate);
        p.setPpqPosition(static_cast<double>(samples) * 2.0 / rate);
        p.setBpm(120.0);
        p.setIsPlaying(true);
        p.setTimeSignature(TimeSignature { 4, 4 });
        return p;
    }
    int64_t samples = 0;
private:
    double rate;
};

void writeJson(const juce::String& path, juce::DynamicObject* object) {
    const auto json = juce::JSON::toString(juce::var(object));
    if (path.isNotEmpty()) {
        const juce::File file = juce::File::getCurrentWorkingDirectory().getChildFile(path);
        if (!file.getParentDirectory().createDirectory() || !file.replaceWithText(json)) {
            throw std::runtime_error("Could not write benchmark JSON");
        }
    }
    std::cout << json << std::endl;
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
        const auto constructorStart = juce::Time::getHighResolutionTicks();
        OscirenderAudioProcessor processor;
        const auto constructorEnd = juce::Time::getHighResolutionTicks();
        PlayHead playHead(options.rate);
        processor.setPlayHead(&playHead);

        if (options.exportDefault.isNotEmpty()) {
            juce::MemoryBlock state;
            processor.getStateInformation(state);
            const auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), static_cast<int>(state.getSize()));
            const auto prefix = juce::File::getCurrentWorkingDirectory().getChildFile(options.exportDefault);
            if (!prefix.getParentDirectory().createDirectory()
                || !prefix.withFileExtension("osci").replaceWithData(state.getData(), state.getSize())
                || xml == nullptr || !xml->writeTo(prefix.withFileExtension("xml"))) {
                throw std::runtime_error("Could not export default state");
            }
            processor.setPlayHead(nullptr);
            return 0;
        }
        const auto loadStart = juce::Time::getHighResolutionTicks();
        if (options.project.isNotEmpty()) {
            juce::MemoryBlock state;
            if (!juce::File::getCurrentWorkingDirectory().getChildFile(options.project).loadFileAsData(state)) {
                throw std::runtime_error("Could not read project");
            }
            processor.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        }
        const auto loadEnd = juce::Time::getHighResolutionTicks();
        processor.voices->setUnnormalisedValueNotifyingHost(options.voices);
        processor.midiEnabled->setBoolValue(options.midi != "off");
        processor.setRateAndBufferSizeDetails(options.rate, options.blockSize);
        processor.prepareToPlay(options.rate, options.blockSize);
        if (!processor.canSetInternalSampleRateRatio(options.ratio)) {
            throw std::runtime_error("Unsupported internal sample rate ratio");
        }
        processor.setInternalSampleRateRatio(options.ratio);
        if (std::abs(processor.getInternalSampleRateRatio() - options.ratio) > 0.000001) {
            throw std::runtime_error("Requested ratio was not applied");
        }

        const int channels = std::max({ 1, processor.getTotalNumInputChannels(), processor.getTotalNumOutputChannels() });
        juce::AudioBuffer<float> audio(channels, options.blockSize);
        const auto fillAudio = [&] {
            audio.clear();
            if (options.inputAmplitude > 0) {
                for (int sample = 0; sample < options.blockSize; ++sample) {
                    const auto value = static_cast<float>(options.inputAmplitude * std::sin(
                        juce::MathConstants<double>::twoPi * 440.0 * (playHead.samples + sample) / options.rate));
                    for (int channel = 0; channel < processor.getTotalNumInputChannels(); ++channel) {
                        audio.setSample(channel, sample, value);
                    }
                }
            }
        };
        juce::MidiBuffer midi, noteOns, noteOffs;
        midi.ensureSize(65536);
        juce::StringArray notes;
        if (options.notes.isNotEmpty()) {
            notes.addTokens(options.notes, ",", "");
        } else {
            for (int i = 0; i < options.voices; ++i) { notes.add(juce::String(60 + i)); }
        }
        for (const auto& noteText : notes) {
            const int note = noteText.getIntValue();
            if (note < 0 || note > 127) { throw std::runtime_error("MIDI note outside 0..127"); }
            noteOns.addEvent(juce::MidiMessage::noteOn(1, note, static_cast<juce::uint8>(options.velocity)), 0);
            noteOffs.addEvent(juce::MidiMessage::noteOff(1, note), 0);
        }
        const auto fillMidi = [&](int block) {
            midi.clear();
            if (options.midi != "off" && (block == 0 || (options.midi == "churn" && block % 128 == 0))) {
                midi.addEvents(noteOns, 0, -1, 0);
            } else if (options.midi == "churn" && block % 128 == 127) {
                midi.addEvents(noteOffs, 0, -1, options.blockSize - 1);
            }
        };
        // Give the asynchronous voice builder and parser setup time to complete before measuring.
        for (int i = 0; i < options.warmup; ++i) {
            fillAudio();
            fillMidi(i);
            processor.processBlock(audio, midi);
            playHead.samples += options.blockSize;
            if (i % 16 == 0) {
                juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
                juce::Thread::sleep(1);
            }
        }
        const auto voiceWaitStart = juce::Time::getMillisecondCounterHiRes();
        while (processor.getVoiceCountForProfiling() != options.voices + 1) {
            if (juce::Time::getMillisecondCounterHiRes() - voiceWaitStart > 10000) {
                throw std::runtime_error("Voice pool did not become ready");
            }
            fillAudio();
            midi.clear();
            processor.processBlock(audio, midi);
            playHead.samples += options.blockSize;
            juce::MessageManager::getInstance()->runDispatchLoopUntil(1);
            juce::Thread::sleep(1);
        }
        const auto voiceWaitMs = juce::Time::getMillisecondCounterHiRes() - voiceWaitStart;
        // Retrigger after warmup: voices may not have existed for the initial note-on.
        midi.clear();
        midi.addEvent(juce::MidiMessage::allSoundOff(1), 0);
        if (options.midi != "off") { midi.addEvents(noteOns, 0, -1, 0); }
        fillAudio();
        processor.processBlock(audio, midi);
        playHead.samples += options.blockSize;

        // allSoundOff retains a short kill-fade overlap voice; exclude that transition from steady-state timing.
        for (int i = 0; i < 64; ++i) {
            fillAudio();
            midi.clear();
            processor.processBlock(audio, midi);
            playHead.samples += options.blockSize;
        }

#if defined(OSCI_ALLOCATION_PROBE) && OSCI_ALLOCATION_PROBE
        AllocationProbe allocationProbe;
#endif
        std::vector<int> fileCycle;
        for (const auto& index : juce::StringArray::fromTokens(options.fileCycle, ",", "")) {
            const int file = index.getIntValue();
            if (!processor.getFileController().contains(file)) {
                throw std::runtime_error("File cycle index outside project files");
            }
            fileCycle.push_back(file);
        }
        juce::Array<juce::var> fileSegments;
        juce::DynamicObject* fileSegment = nullptr;
        double segmentEnergy = 0;
        std::vector<double> timings(static_cast<size_t>(options.blocks));
        std::vector<uint64_t> channelHashes(static_cast<size_t>(channels), UINT64_C(14695981039346656037));
        double checksum = 0, energy = 0, peak = 0;
        int64_t nonfinite = 0;
        int maxActiveVoices = 0;
        int maxUiActiveVoices = 0;
        for (int i = 0; i < options.blocks; ++i) {
            if (!fileCycle.empty() && i % options.fileCycleBlocks == 0) {
                if (fileSegment != nullptr) { fileSegment->setProperty("energy", segmentEnergy); }
                const int file = fileCycle[(i / options.fileCycleBlocks) % fileCycle.size()];
                processor.getFileController().selectFile(file);
                fileSegment = new juce::DynamicObject();
                fileSegment->setProperty("file", processor.getFileController().getCurrentFileName());
                fileSegment->setProperty("first_block", i);
                fileSegments.add(juce::var(fileSegment));
                segmentEnergy = 0;
            }
            fillAudio();
            fillMidi(i + 1);
#if defined(OSCI_ALLOCATION_PROBE) && OSCI_ALLOCATION_PROBE
            allocationProbe.enable(1);
#endif
            const auto start = juce::Time::getHighResolutionTicks();
            processor.processBlock(audio, midi);
            const auto end = juce::Time::getHighResolutionTicks();
#if defined(OSCI_ALLOCATION_PROBE) && OSCI_ALLOCATION_PROBE
            allocationProbe.enable(0);
#endif
            timings[static_cast<size_t>(i)] = juce::Time::highResolutionTicksToSeconds(end - start);
            playHead.samples += options.blockSize;
            int activeVoices = 0;
            for (const auto& active : processor.uiVoiceActive) { activeVoices += active.load(std::memory_order_relaxed) ? 1 : 0; }
            maxUiActiveVoices = std::max(maxUiActiveVoices, activeVoices);
            maxActiveVoices = std::max(maxActiveVoices, processor.getActiveVoiceCountForProfiling());
            for (int channel = 0; channel < audio.getNumChannels(); ++channel) {
                for (int sample = 0; sample < audio.getNumSamples(); ++sample) {
                    const float sampleValue = audio.getSample(channel, sample);
                    channelHashes[static_cast<size_t>(channel)] = hashSampleBits(channelHashes[static_cast<size_t>(channel)], sampleValue);
                    const double value = sampleValue;
                    if (!std::isfinite(value)) { ++nonfinite; continue; }
                    checksum += value * (1 + (sample % 31));
                    energy += value * value;
                    segmentEnergy += value * value;
                    peak = std::max(peak, std::abs(value));
                }
            }
        }
        if (fileSegment != nullptr) { fileSegment->setProperty("energy", segmentEnergy); }
        processor.releaseResources();
        processor.setPlayHead(nullptr);
        const auto mean = std::accumulate(timings.begin(), timings.end(), 0.0) / options.blocks;
        std::sort(timings.begin(), timings.end());
        const auto percentile = [&](double p) { return timings[static_cast<size_t>(std::ceil(p * options.blocks)) - 1]; };
        const double deadline = options.blockSize / options.rate;
        auto result = std::make_unique<juce::DynamicObject>();
#if defined(OSCI_ALLOCATION_PROBE) && OSCI_ALLOCATION_PROBE
        OsciAllocationCounts allocationCounts {};
        allocationProbe.read(&allocationCounts);
        result->setProperty("allocation_instrumented", true);
        auto allocatorOperations = new juce::DynamicObject();
        allocatorOperations->setProperty("malloc", static_cast<juce::int64>(allocationCounts.mallocCalls));
        allocatorOperations->setProperty("calloc", static_cast<juce::int64>(allocationCounts.callocCalls));
        allocatorOperations->setProperty("realloc", static_cast<juce::int64>(allocationCounts.reallocCalls));
        allocatorOperations->setProperty("free", static_cast<juce::int64>(allocationCounts.freeCalls));
        result->setProperty("allocator_operations", juce::var(allocatorOperations));
#else
        result->setProperty("allocation_instrumented", false);
#endif
        result->setProperty("project", options.project);
        result->setProperty("file_segments", fileSegments);
        result->setProperty("profile_home", home.getFullPathName());
        result->setProperty("processor_construction_ms", juce::Time::highResolutionTicksToSeconds(constructorEnd - constructorStart) * 1000);
        result->setProperty("project_restore_ms", juce::Time::highResolutionTicksToSeconds(loadEnd - loadStart) * 1000);
        result->setProperty("frequency_parameter_hz", processor.frequencyEffect->parameters[0]->getValueUnnormalised());
        result->setProperty("voice_setup_wait_ms", voiceWaitMs);
        result->setProperty("selected_file", processor.getFileController().getCurrentFileName());
        int enabledEffects = 0;
        for (const auto& effect : processor.toggleableEffects) {
            if (effect->selected->getBoolValue() && effect->enabled->getBoolValue()) {
                ++enabledEffects;
            }
        }
        result->setProperty("enabled_effects", enabledEffects);
        auto assignmentCounts = std::make_unique<juce::DynamicObject>();
        assignmentCounts->setProperty("lfo", static_cast<int>(processor.lfoParameters.getAssignments().size()));
        assignmentCounts->setProperty("env", static_cast<int>(processor.envelopeParameters.getAssignments().size()));
        assignmentCounts->setProperty("rng", static_cast<int>(processor.randomParameters.getAssignments().size()));
        assignmentCounts->setProperty("sc", static_cast<int>(processor.sidechainParameters.getAssignments().size()));
        result->setProperty("modulation_assignments", juce::var(assignmentCounts.release()));
        const auto parser = processor.getFileController().getCurrentParser();
        if (parser != nullptr) {
            juce::String sourceType = "unresolved";
            if (parser->getLua() != nullptr) { sourceType = "lua"; }
            else if (parser->getWav() != nullptr) { sourceType = "audio"; }
            else if (parser->getImg() != nullptr) { sourceType = "image"; }
            else if (parser->getObject() != nullptr) { sourceType = "obj"; }
            else if (parser->getSvg() != nullptr) { sourceType = "svg"; }
            else if (parser->getText() != nullptr) { sourceType = "text"; }
            else if (parser->getLineArt() != nullptr) { sourceType = "gpla"; }
#if OSCI_PREMIUM
            else if (parser->getFractal() != nullptr) { sourceType = "fractal"; }
            else if (parser->getLottie() != nullptr) { sourceType = "lottie"; }
#endif
            result->setProperty("loaded_parser", sourceType);
            result->setProperty("source_frames", parser->getNumFrames());
            result->setProperty("source_frame_at_end", parser->getCurrentFrame());
        }
        result->setProperty("sample_rate", options.rate);
        result->setProperty("block_size", options.blockSize);
        result->setProperty("ratio", options.ratio);
        result->setProperty("voices", options.voices);
        result->setProperty("midi", options.midi);
        result->setProperty("velocity", options.velocity);
        result->setProperty("host_input_amplitude", options.inputAmplitude);
        result->setProperty("notes", notes.joinIntoString(","));
        result->setProperty("warmup_blocks", options.warmup);
        result->setProperty("measured_blocks", options.blocks);
        result->setProperty("mean_us", mean * 1e6);
        result->setProperty("p50_us", percentile(0.50) * 1e6);
        result->setProperty("p95_us", percentile(0.95) * 1e6);
        result->setProperty("p99_us", percentile(0.99) * 1e6);
        result->setProperty("max_us", timings.back() * 1e6);
        result->setProperty("mean_deadline_fraction", mean / deadline);
        result->setProperty("p99_deadline_fraction", percentile(0.99) / deadline);
        result->setProperty("max_deadline_fraction", timings.back() / deadline);
        juce::Array<juce::var> sampleBitHashes;
        for (const auto hash : channelHashes) {
            sampleBitHashes.add(juce::String::toHexString(static_cast<juce::int64>(hash)).paddedLeft('0', 16));
        }
        result->setProperty("channel_sample_bit_hashes", sampleBitHashes);
        result->setProperty("sample_bit_hash_algorithm", "FNV-1a-64 over float32 bytes, least-significant byte first");
        result->setProperty("checksum", checksum);
        result->setProperty("energy", energy);
        result->setProperty("peak", peak);
        result->setProperty("nonfinite_samples", juce::var(nonfinite));
        result->setProperty("max_active_voices", maxActiveVoices);
        result->setProperty("max_ui_active_voices", maxUiActiveVoices);
        result->setProperty("active_audio_verified", energy > 1e-12 && nonfinite == 0);
#if defined(OSCI_ALLOCATION_PROBE) && OSCI_ALLOCATION_PROBE
        result->setProperty("timing_scope", "processBlock with allocator instrumentation; timings diagnostic only; no editor or audio device");
#else
        result->setProperty("timing_scope", "processBlock only; no editor or audio device; allocations not instrumented");
#endif
        result->setProperty("standalone_application", juce::JUCEApplicationBase::isStandaloneApp());
        writeJson(options.output, result.release());
        return nonfinite == 0 ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << std::endl;
        return 1;
    }
}

class BenchmarkApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "osci-render benchmark"; }
    const juce::String getApplicationVersion() override { return "1"; }
    void initialise(const juce::String&) override {
        setApplicationReturnValue(runBenchmark());
        quit();
    }
    void shutdown() override {}
};

START_JUCE_APPLICATION(BenchmarkApplication)
JUCE_MAIN_FUNCTION_DEFINITION
