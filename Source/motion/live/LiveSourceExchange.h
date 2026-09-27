#pragma once

#include "LiveSourceFrames.h"
#include "../../audio/PreparedState.h"

namespace motion {
class LiveSourceExchange {
public:
    struct Block {
        std::shared_ptr<const LiveSourceFrames> frames;
        std::uint64_t revision = 0;
    };
    // Message-thread only. Geometry is already prepared; neither the receiver
    // nor another worker may publish concurrently with this owner.
    void publish(std::shared_ptr<const LiveSourceFrames> frames) {
        auto block = std::make_unique<Block>(Block{frames, ++revision});
        exchange.publish(std::move(block));
        preview = std::move(frames);
    }
    std::shared_ptr<const LiveSourceFrames> previewSnapshot() const { return preview; }
    void collect() { exchange.collect(); }
    // Exactly once per audio block. Do not acquire again while a previous
    // borrowed Block is in use. No reference count changes on this path.
    const Block* acquire() noexcept { return exchange.acquire(); }
private:
    osci::PreparedState<Block> exchange;
    std::shared_ptr<const LiveSourceFrames> preview;
    std::uint64_t revision = 0;
};
}
