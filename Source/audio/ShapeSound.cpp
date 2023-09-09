#include "ShapeSound.h"

ShapeSound::ShapeSound(std::shared_ptr<FileParser> parser) : parser(parser) {
    if (parser->isSample()) {
        producer = std::make_unique<FrameProducer>(*this, std::make_shared<FileParser>());
    } else {
        producer = std::make_unique<FrameProducer>(*this, parser);
    }
    producer->startThread();
}

ShapeSound::~ShapeSound() {
    frames.kill();
    producer->stopThread(1000);
}

bool ShapeSound::appliesToNote(int note) {
    return true;
}

bool ShapeSound::appliesToChannel(int channel) {
    return true;
}

void ShapeSound::addFrame(std::vector<std::unique_ptr<Shape>>& frame) {
    frames.push(std::move(frame));
}

double ShapeSound::updateFrame(std::vector<std::unique_ptr<Shape>>& frame) {
    if (frames.try_pop(frame)) {
        frameLength = Shape::totalLength(frame);
    }

    return frameLength;
}
