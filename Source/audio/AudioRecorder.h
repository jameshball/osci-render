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

    void setNumChannels(int numChannels) {
        stop();
        this->numChannels = numChannels;
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

                if (auto writer = wavFormat.createWriterFor(fileStream.get(), sampleRate, numChannels, 32, {}, 0)) {
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
        } else {
            // Error: Invalid sample rate
            juce::AlertWindow::showMessageBoxAsync(
                juce::AlertWindow::WarningIcon,
                "Recording Error",
                "Cannot start recording: Invalid sample rate (" + juce::String(sampleRate) + "). Sample rate must be greater than 0.",
                "OK"
            );
            stop();
            stopCallback();
            return;
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
            // Ensure we only write the number of channels configured when recording started
            // This prevents corruption if the buffer channel count changes mid-recording
            if (buffer.getNumChannels() == numChannels) {
                // Fast path: buffer has exactly the right number of channels
                activeWriter.load()->write(buffer.getArrayOfReadPointers(), numSamples);
            } else {
                // Safety path: adapt the buffer to match the expected channel count
                // Resize the temp buffer if needed
                if (tempRecordBuffer.getNumChannels() != numChannels || tempRecordBuffer.getNumSamples() < numSamples) {
                    tempRecordBuffer.setSize(numChannels, numSamples, false, false, true);
                }
                
                // Copy channels from input buffer (or fill with zeros if not enough channels)
                for (int ch = 0; ch < numChannels; ++ch) {
                    if (ch < buffer.getNumChannels()) {
                        tempRecordBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);
                    } else {
                        tempRecordBuffer.clear(ch, 0, numSamples);
                    }
                }
                
                activeWriter.load()->write(tempRecordBuffer.getArrayOfReadPointers(), numSamples);
            }
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
    double sampleRate = -1;
    int numChannels = 2;
    
    juce::AudioBuffer<float> tempRecordBuffer; // Used when input buffer channel count doesn't match recording channel count

    juce::CriticalSection writerLock;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter { nullptr };
};
