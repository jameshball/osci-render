#pragma once

#include "../render/CompositionRenderer.h"
#include "../render/SampleClock.h"
#include <atomic>
#include <limits>

namespace motion {
// Synchronous offline writer. Invoke on a worker, retaining the immutable
// project/prepared snapshot for the call; progress and cancellation are atomic.
class SignalExporter {
public:
    static juce::Result write(Project snapshot, const juce::File& destination, double sampleRate, const std::atomic<bool>& cancel, std::atomic<double>* progress = nullptr) {
        if (progress != nullptr) {
            progress->store(0.0, std::memory_order_relaxed);
        }
        if (cancel.load(std::memory_order_relaxed)) {
            return cancelled();
        }
        return write(PreparedComposition(snapshot), destination, sampleRate, cancel, progress);
    }

    static juce::Result write(const PreparedComposition& composition, const juce::File& destination, double sampleRate, const std::atomic<bool>& cancel, std::atomic<double>* progress = nullptr) {
        if (progress != nullptr) {
            progress->store(0.0, std::memory_order_relaxed);
        }
        if (cancel.load(std::memory_order_relaxed)) {
            return cancelled();
        }
        constexpr auto bytesPerFrame = 5 * sizeof(float);
        if (!std::isfinite(sampleRate) || sampleRate <= 0.0 || sampleRate != std::floor(sampleRate)
            || sampleRate > static_cast<double>(std::numeric_limits<std::uint32_t>::max() / bytesPerFrame)) {
            return juce::Result::fail("Choose a positive whole-number sample rate that fits the WAV format (for example, 48000 Hz).");
        }
        // Integer sample indices must remain exact when converted to the double
        // clock, and the encoded file size must fit signed file offsets.
        const auto frameCount = sampleIndex(composition.duration, sampleRate);
        if (!frameCount.has_value() || *frameCount < 1) {
            return juce::Result::fail("Choose a finite positive composition duration that produces at least one sample and fits the export sample clock.");
        }
        if (destination == juce::File() || destination.isDirectory() || !destination.getParentDirectory().isDirectory()) {
            return juce::Result::fail("Choose a WAV file in an existing destination folder.");
        }
        const auto sampleCount = *frameCount;
        const auto frames = static_cast<double>(sampleCount);
        juce::TemporaryFile pending(destination);
        {
            // Keep the file stream alive after the writer has finalized its
            // header so destructor-time write failures are observable.
            juce::FileOutputStream fileStream(pending.getFile());
            if (!fileStream.openedOk()) {
                return juce::Result::fail("Cannot create the temporary WAV beside the destination: " + fileStream.getStatus().getErrorMessage());
            }
            std::unique_ptr<juce::OutputStream> output = std::make_unique<BorrowedOutputStream>(fileStream);
            juce::WavAudioFormat format;
            const auto options = juce::AudioFormatWriterOptions().withSampleRate(sampleRate).withChannelLayout(juce::AudioChannelSet::discreteChannels(5))
                .withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
            auto writer = format.createWriterFor(output, options);
            if (writer == nullptr) {
                return juce::Result::fail("Cannot create a five-channel floating-point WAV writer at the selected sample rate.");
            }
            constexpr int blockSize = 4096;
            juce::AudioBuffer<float> buffer(5, blockSize);
            for (juce::int64 first = 0; first < sampleCount; first += blockSize) {
                const auto count = static_cast<int>(std::min<juce::int64>(blockSize, sampleCount - first));
                for (int offset = 0; offset < count; ++offset) {
                    if ((offset & 255) == 0 && cancel.load(std::memory_order_relaxed)) {
                        return cancelled();
                    }
                    const auto index = static_cast<double>(first + offset);
                    const auto time = index / sampleRate;
                    const auto phase = std::fmod(index * 60.0 / sampleRate, 1.0);
                    const auto point = composition.sample(time, phase);
                    const std::array<float, 5> channels { point.x, point.y, point.r, point.g, point.b };
                    for (int channel = 0; channel < 5; ++channel) {
                        if (!std::isfinite(channels[static_cast<std::size_t>(channel)])) {
                            return juce::Result::fail("The composition produced a non-finite signal sample. Check its transforms and camera animation.");
                        }
                        buffer.setSample(channel, offset, channels[static_cast<std::size_t>(channel)]);
                    }
                }
                if (!writer->writeFromAudioSampleBuffer(buffer, 0, count)) {
                    return juce::Result::fail("Cannot write the WAV signal. Check destination permissions and available disk space.");
                }
                if (progress != nullptr) {
                    progress->store(0.99 * static_cast<double>(first + count) / frames, std::memory_order_relaxed);
                }
            }
            const bool writerFlushed = writer->flush();
            writer.reset();
            fileStream.flush();
            if (!writerFlushed || fileStream.getStatus().failed()) {
                return juce::Result::fail("Cannot finalize the WAV signal. Check destination permissions and available disk space.");
            }
        }
        if (cancel.load(std::memory_order_relaxed)) {
            return cancelled();
        }
        {
            juce::WavAudioFormat format;
            auto input = pending.getFile().createInputStream();
            if (input == nullptr) {
                return juce::Result::fail("Cannot reopen the completed temporary WAV for verification; the destination was left unchanged.");
            }
            std::unique_ptr<juce::AudioFormatReader> reader(format.createReaderFor(input.release(), true));
            if (reader == nullptr || reader->numChannels != 5 || reader->bitsPerSample != 32 || !reader->usesFloatingPointData
                || reader->lengthInSamples != sampleCount || reader->sampleRate != sampleRate) {
                return juce::Result::fail("The completed WAV could not be verified; the destination was left unchanged.");
            }
        }
        if (cancel.load(std::memory_order_relaxed)) {
            return cancelled();
        }
        if (!pending.overwriteTargetFileWithTemporary()) {
            return juce::Result::fail("Cannot replace the destination WAV. Close applications using it and check folder permissions.");
        }
        if (progress != nullptr) {
            progress->store(1.0, std::memory_order_relaxed);
        }
        return juce::Result::ok();
    }

private:
    class BorrowedOutputStream final : public juce::OutputStream {
    public:
        explicit BorrowedOutputStream(juce::FileOutputStream& stream) : stream(stream) {}
        void flush() override { stream.flush(); }
        bool setPosition(juce::int64 position) override { return stream.setPosition(position); }
        juce::int64 getPosition() override { return stream.getPosition(); }
        bool write(const void* data, std::size_t size) override { return stream.write(data, size); }

    private:
        juce::FileOutputStream& stream;
    };

    static juce::Result cancelled() { return juce::Result::fail("Signal export cancelled."); }
};
}
