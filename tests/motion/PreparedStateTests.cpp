#include "../../Source/audio/PreparedState.h"
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {
const auto publisher = std::this_thread::get_id();
std::atomic<int> destroyed { 0 };
struct State {
    explicit State(int value) : value(value), checksum(value * 7) {}
    ~State() {
        if (std::this_thread::get_id() != publisher) {
            std::abort();
        }
        ++destroyed;
    }
    const int value;
    const int checksum;
};
}

int main() {
    constexpr int count = 20000;
    {
        osci::PreparedState<State, 4> exchange;
        std::atomic<bool> finished { false };
        std::atomic<int> last { 0 };
        std::thread render([&] {
            while (!finished.load() || last.load() != count) {
                const auto* state = exchange.acquire();
                if (state != nullptr) {
                    if (state->checksum != state->value * 7 || state->value < last.load()) {
                        std::abort();
                    }
                    last.store(state->value);
                }
                std::this_thread::yield();
            }
        });
        for (int i = 1; i <= count; ++i) {
            exchange.publish(std::make_unique<State>(i));
            if (i % 31 == 0) {
                std::this_thread::yield();
            }
        }
        finished.store(true);
        while (last.load() != count) {
            exchange.collect();
            std::this_thread::yield();
        }
        render.join();
    }
    if (destroyed.load() != count) {
        return 1;
    }
    std::cout << "Prepared state lifetime and publication contracts passed\n";
}
