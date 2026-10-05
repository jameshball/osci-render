#pragma once

#include "BlenderFrame.h"
#include "../model/PreparedSource.h"

namespace motion {
// Receiver decoding and this geometry construction both belong off the audio
// thread. An intentional empty frame remains empty, never a fallback shape.
inline std::shared_ptr<const PreparedSource> prepareBlenderFrame(const BlenderFrame& frame) {
    std::vector<std::unique_ptr<osci::Shape>> lines;
    lines.reserve(frame.segments.size());
    for (const auto& segment : frame.segments) {
        lines.push_back(std::make_unique<osci::Line>(osci::Point(segment.x1, segment.y1, 0), osci::Point(segment.x2, segment.y2, 0)));
    }
    auto drawing = std::make_shared<const motion::PreparedDrawing>(std::move(lines));
    return std::make_shared<const PreparedSource>(std::vector<std::shared_ptr<const motion::PreparedDrawing>>{std::move(drawing)}, frame.frameRate);
}
}
