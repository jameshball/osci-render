#pragma once
#include <JuceHeader.h>

//==============================================================================
class AudioRecorder final {
public:
    AudioRecorder() {
        backgroundThread.startThread();
    }

    ~AudioRecorder() {
        stop();
    }

    void setSampleRate(double sampleRate) {
        stop();
        this->sampleRate = sampleRate;
    }

    //==============================================================================
    void startRecording(const juce::File& file) {
        stop();

        if (sampleRate > 0) {
            // Create an OutputStream to write to our destination file...
            file.deleteFile();

            if (auto fileStream = std::unique_ptr<juce::FileOutputStream>(file.createOutputStream())) {
                // Now create a WAV writer object that writes to our output stream...
                juce::WavAudioFormat wavFormat;

                if (auto writer = wavFormat.createWriterFor(fileStream.get(), sampleRate, 2, 32, {}, 0)) {
                    fileStream.release(); // (passes responsibility for deleting the stream to the writer object that is now using it)

                    // Now we'll create one of these helper objects which will act as a FIFO buffer, and will
                    // write the data to disk on our background thread.
                    threadedWriter.reset(new juce::AudioFormatWriter::ThreadedWriter(writer, backgroundThread, 32768));

                    nextSampleNum = 0;

                    // And now, swap over our active writer pointer so that the audio callback will start using it..
                    const juce::ScopedLock sl(writerLock);
                    activeWriter = threadedWriter.get();
                }
            }
        }
    }

    void stop() {
        // First, clear this pointer to stop the audio callback from using our writer object..
        {
            const juce::ScopedLock sl(writerLock);
            activeWriter = nullptr;
        }

        // Now we can delete the writer object. It's done in this order because the deletion could
        // take a little time while remaining data gets flushed to disk, so it's best to avoid blocking
        // the audio callback while this happens.
        threadedWriter.reset();
    }

    bool isRecording() const {
        return activeWriter.load() != nullptr;
    }

    void audioThreadCallback(const juce::AudioBuffer<float>& buffer) {
        if (nextSampleNum >= recordingLength * sampleRate) {
            stop();
            stopCallback();
            return;
        }

        const juce::ScopedLock sl(writerLock);
        int numSamples = buffer.getNumSamples();

        if (activeWriter.load() != nullptr) {
            activeWriter.load()->write(buffer.getArrayOfReadPointers(), numSamples);
            nextSampleNum += numSamples;
        }
    }

    void setRecordLength(double recordLength) {
        recordingLength = recordLength;
    }

    std::function<void()> stopCallback;

private:
    juce::TimeSliceThread backgroundThread { "Audio Recorder Thread" }; // the thread that will write our audio data to disk
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threadedWriter; // the FIFO used to buffer the incoming data
    juce::int64 nextSampleNum = 0;

    double recordingLength = 99999999999.0;
    double sampleRate = 192000;

    juce::CriticalSection writerLock;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter { nullptr };
};
