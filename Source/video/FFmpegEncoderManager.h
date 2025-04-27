#pragma once

#include <JuceHeader.h>

#include "../visualiser/RecordingSettings.h"

class FFmpegEncoderManager {
public:
    FFmpegEncoderManager(juce::File& ffmpegExecutable);
    ~FFmpegEncoderManager() = default;

    struct EncoderDetails {
        juce::String name;
        juce::String description;
        bool isHardwareAccelerated;
        bool isSupported;
    };

    // FFMPEG command builder
    juce::String buildVideoEncodingCommand(
        VideoCodec codec,
        int crf,
        int videoToolboxQuality,
        int width,
        int height,
        double frameRate,
        const juce::String& compressionPreset,
        const juce::File& outputFile);

    // Get available encoders for a given codec
    juce::Array<EncoderDetails> getAvailableEncodersForCodec(VideoCodec codec);

    // Check if a hardware encoder is available
    bool isHardwareEncoderAvailable(const juce::String& encoderName);

    // Get the best encoder for a given codec
    juce::String getBestEncoderForCodec(VideoCodec codec);

private:
    juce::File ffmpegExecutable;
    std::map<VideoCodec, juce::Array<EncoderDetails>> availableEncoders;

    // Query available encoders from FFmpeg
    void queryAvailableEncoders();

    // Parse encoder output from FFmpeg
    void parseEncoderList(const juce::String& output);

    // Run FFmpeg with given arguments and return output
    juce::String runFFmpegCommand(const juce::StringArray& args);

    // Common base command builder to reduce duplication
    juce::String buildBaseEncodingCommand(
        int width,
        int height,
        double frameRate,
        const juce::File& outputFile);

    // H.264 encoder settings helper
    juce::String addH264EncoderSettings(
        juce::String cmd,
        const juce::String& encoderName,
        int crf,
        const juce::String& compressionPreset);

    // H.265 encoder settings helper
    juce::String addH265EncoderSettings(
        juce::String cmd,
        const juce::String& encoderName,
        int crf,
        int videoToolboxQuality,
        const juce::String& compressionPreset);

    // Build H.264 encoding command
    juce::String buildH264EncodingCommand(
        int crf,
        int width,
        int height,
        double frameRate,
        const juce::String& compressionPreset,
        const juce::File& outputFile);

    // Build H.265 encoding command
    juce::String buildH265EncodingCommand(
        int crf,
        int videoToolboxQuality,
        int width,
        int height,
        double frameRate,
        const juce::String& compressionPreset,
        const juce::File& outputFile);

    // Build VP9 encoding command
    juce::String buildVP9EncodingCommand(
        int crf,
        int width,
        int height,
        double frameRate,
        const juce::String& compressionPreset,
        const juce::File& outputFile);

#if JUCE_MAC
    // Build ProRes encoding command
    juce::String buildProResEncodingCommand(
        int width,
        int height,
        double frameRate,
        const juce::File& outputFile);
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FFmpegEncoderManager)
};