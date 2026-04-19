#include "ShapeSound.h"
#include "../../parser/FileParser.h"
#include "../../parser/FrameProducer.h"

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

void ShapeSound::replaceQueueWith(std::vector<std::unique_ptr<osci::Shape>>& frame) {
    // flush() and addFrame() are separate mutex acquisitions on the queue.
    // This is safe because only one FrameProducer thread calls this per sound.
    frames.flush();
    addFrame(frame);
    freshFrameAvailable.store(true, std::memory_order_release);
}

bool ShapeSound::updateFrame(std::vector<std::unique_ptr<osci::Shape>>& frame) {
    if (frames.try_pop(frame)) {
        frameLength.store(osci::Shape::totalLength(frame), std::memory_order_relaxed);
        return true;
    }
    return false;
}

double ShapeSound::getFrameLength() const {
    return frameLength.load(std::memory_order_relaxed);
}

bool ShapeSound::consumeFreshFrame() {
    return freshFrameAvailable.exchange(false, std::memory_order_acquire);
}
