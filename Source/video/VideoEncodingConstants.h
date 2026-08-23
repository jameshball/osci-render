#pragma once

enum class VideoCodec {
    H264,
    H265,
    VP9,
    ProRes,
    ProRes4444,
};

namespace VideoEncodingConstants {

inline constexpr int frameWriteTimeoutMs = 3000;

namespace PixelFormat {

inline constexpr const char* rgba8 = "rgba";
inline constexpr const char* yuv4208Bit = "yuv420p";
inline constexpr const char* yuv42210BitLittleEndian = "yuv422p10le";
inline constexpr const char* yuva44410BitLittleEndian = "yuva444p10le";

}

}
