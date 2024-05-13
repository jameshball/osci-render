#include "ShapeSound.h"

ShapeSound::ShapeSound(std::shared_ptr<FileParser> parser) : parser(parser) {
    if (parser->isSample()) {
        producer = std::make_unique<FrameProducer>(*this, std::make_shared<FileParser>());
    } else {
        producer = std::make_unique<FrameProducer>(*this, parser);
    }
    producer->startThread();
}

ShapeSound::ShapeSound() {}

ShapeSound::~ShapeSound() {
    frames.kill();
    if (producer != nullptr) {
        producer->stopThread(1000);
    }
}

bool ShapeSound::appliesToNote(int note) {
    return true;
}

bool ShapeSound::appliesToChannel(int channel) {
    return true;
}

void ShapeSound::addFrame(std::vector<std::unique_ptr<Shape>>& frame, bool force) {
    if (force) {
        frames.push(std::move(frame));
    } else {
        frames.try_push(std::move(frame));
    }
}

// Update to next frame in queue
double ShapeSound::updateFrame(std::vector<std::unique_ptr<Shape>>& frame) {
    if (frames.try_pop(frame)) {
        frameLength = Shape::totalLength(frame);
    }

    return frameLength;
}

// Update to newest frame
double ShapeSound::flushFrame(std::vector<std::unique_ptr<Shape>>& frame) {
    while (frames.try_pop(frame)) {
        frameLength = Shape::totalLength(frame);
    }

    return frameLength;
}
