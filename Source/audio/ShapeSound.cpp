#include "ShapeSound.h"

ShapeSound::ShapeSound(std::shared_ptr<FileParser> parser) : parser(parser) {
    if (parser->isSample()) {
        producer = std::make_unique<FrameProducer>(*this, std::make_shared<FileParser>());
    } else {
        producer = std::make_unique<FrameProducer>(*this, parser);
    }
    producer->startThread();
}

bool ShapeSound::appliesToNote(int note) {
    return true;
}

bool ShapeSound::appliesToChannel(int channel) {
    return true;
}

void ShapeSound::addFrame(std::vector<std::unique_ptr<Shape>>& frame) {
    const auto scope = frameFifo.write(1);

    if (scope.blockSize1 > 0) {
        frameBuffer[scope.startIndex1].clear();
        for (auto& shape : frame) {
            frameBuffer[scope.startIndex1].push_back(std::move(shape));
        }
    }

    if (scope.blockSize2 > 0) {
        frameBuffer[scope.startIndex2].clear();
        for (auto& shape : frame) {
            frameBuffer[scope.startIndex2].push_back(std::move(shape));
        }
    }
}

double ShapeSound::updateFrame(std::vector<std::unique_ptr<Shape>>& frame) {
    if (frameFifo.getNumReady() > 0) {
        {
            const auto scope = frameFifo.read(1);

            if (scope.blockSize1 > 0) {
                frame.swap(frameBuffer[scope.startIndex1]);
            } else if (scope.blockSize2 > 0) {
                frame.swap(frameBuffer[scope.startIndex2]);
            }

            frameLength = Shape::totalLength(frame);
        }
    }

    return frameLength;
}
