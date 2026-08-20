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

}
