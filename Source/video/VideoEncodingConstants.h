#pragma once

#include <array>

enum class VideoCodec {
    H264 = 0,
    H265 = 1,
    VP9 = 2,
    ProRes = 3,
    ProRes4444 = 4,
    Count = 5,
};

namespace VideoEncodingConstants {

inline constexpr int frameWriteTimeoutMs = 3000;

namespace PixelFormat {

inline constexpr const char* rgba8 = "rgba";
inline constexpr const char* yuv4208Bit = "yuv420p";
inline constexpr const char* yuv42210BitLittleEndian = "yuv422p10le";
inline constexpr const char* yuva44410BitLittleEndian = "yuva444p10le";

}

struct VideoCodecInfo {
    VideoCodec codec;
    const char* displayName;
    const char* logName;
    const char* outputPixelFormat;
    const char* defaultFileExtension;
    bool proRes;
    bool supportsLosslessAudio;
};

inline constexpr std::array videoCodecs {
    VideoCodecInfo {
        .codec = VideoCodec::H264,
        .displayName = "H.264",
        .logName = "H264",
        .outputPixelFormat = PixelFormat::yuv4208Bit,
        .defaultFileExtension = "mp4",
        .proRes = false,
        .supportsLosslessAudio = true,
    },
    VideoCodecInfo {
        .codec = VideoCodec::H265,
        .displayName = "H.265/HEVC",
        .logName = "H265",
        .outputPixelFormat = PixelFormat::yuv4208Bit,
        .defaultFileExtension = "mp4",
        .proRes = false,
        .supportsLosslessAudio = true,
    },
    VideoCodecInfo {
        .codec = VideoCodec::VP9,
        .displayName = "VP9",
        .logName = "VP9",
        .outputPixelFormat = PixelFormat::yuv4208Bit,
        .defaultFileExtension = "mp4",
        .proRes = false,
        .supportsLosslessAudio = false,
    },
    VideoCodecInfo {
        .codec = VideoCodec::ProRes,
        .displayName = "ProRes 422 HQ",
        .logName = "ProRes 422 HQ",
        .outputPixelFormat = PixelFormat::yuv42210BitLittleEndian,
        .defaultFileExtension = "mov",
        .proRes = true,
        .supportsLosslessAudio = true,
    },
    VideoCodecInfo {
        .codec = VideoCodec::ProRes4444,
        .displayName = "ProRes 4444",
        .logName = "ProRes 4444",
        .outputPixelFormat = PixelFormat::yuva44410BitLittleEndian,
        .defaultFileExtension = "mov",
        .proRes = true,
        .supportsLosslessAudio = true,
    },
};

static_assert(videoCodecs.size() == static_cast<std::size_t>(VideoCodec::Count));

inline constexpr const VideoCodecInfo& getVideoCodecInfo(VideoCodec codec) {
    for (const auto& info : videoCodecs) {
        if (info.codec == codec) {
            return info;
        }
    }
    return videoCodecs.front();
}

inline constexpr VideoCodec videoCodecFromSerializedValue(int value) {
    for (const auto& info : videoCodecs) {
        if (static_cast<int>(info.codec) == value) {
            return info.codec;
        }
    }
    return VideoCodec::H264;
}

}
