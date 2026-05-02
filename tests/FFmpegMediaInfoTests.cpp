#include <JuceHeader.h>
#include "../Source/video/FFmpegMediaInfo.h"

class FFmpegMediaInfoTest : public juce::UnitTest
{
public:
    FFmpegMediaInfoTest() : juce::UnitTest("FFmpeg Media Info", "Video") {}

    void runTest() override
    {
        beginTest("ffmpeg parses video stream metadata");
        {
            const auto info = osci::video::detail::parseFFmpegInputOutput(
                "Input #0, mov,mp4,m4a,3gp,3g2,mj2, from 'clip.mp4':\n"
                "  Duration: 00:00:01.00, start: 0.000000, bitrate: 1024 kb/s\n"
                "  Stream #0:0: Video: h264, yuv420p(progressive), 640x360 [SAR 1:1 DAR 16:9], 29.97 fps, 29.97 tbr, 90k tbn\n"
                "  Stream #0:1: Audio: aac, 48000 Hz, stereo, fltp\n");

            expectEquals(info.width, 640);
            expectEquals(info.height, 360);
            expectWithinAbsoluteError(info.frameRate, 29.97, 0.0001);
        }
    }
};

static FFmpegMediaInfoTest ffmpegMediaInfoTest;