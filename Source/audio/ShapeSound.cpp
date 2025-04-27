#include "ShapeSound.h"

ShapeSound::ShapeSound(OscirenderAudioProcessor &p, std::shared_ptr<FileParser> parser) : parser(parser) {
    if (parser->isSample()) {
        producer = std::make_unique<FrameProducer>(*this, std::make_shared<FileParser>(p));
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

void ShapeSound::addFrame(std::vector<std::unique_ptr<osci::Shape>>& frame, bool force) {
    if (force) {
        frames.push(std::move(frame));
    } else {
        frames.try_push(std::move(frame));
    }
}

double ShapeSound::updateFrame(std::vector<std::unique_ptr<osci::Shape>>& frame) {
    if (frames.try_pop(frame)) {
        frameLength = osci::Shape::totalLength(frame);
    }

    return frameLength;
}
