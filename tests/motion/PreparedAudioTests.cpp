#include "../../Source/motion/model/PreparedAudio.h"
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool passed, const char* message) {
    if (!passed) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
bool near(float left, float right) { return std::abs(left - right) < 0.00001f; }
motion::PreparedAudio::Result mono(double rate, const std::vector<float>& samples) {
    const std::array<std::span<const float>, 1> channels { samples };
    return motion::PreparedAudio::fromPlanar(rate, channels);
}
}

int main() {
    using Audio = motion::PreparedAudio;
    const auto source = mono(4, { 0, 1, 0, -1 });
    check(static_cast<bool>(source), "valid mono PCM prepares");
    check(source.audio->duration() == 1 && source.audio->frameCount() == 4 && source.audio->channelCount() == 1, "source metadata remains exact");
    check(near(source.audio->sample(0.125).left, 0.5f), "linear resampling interpolates unequal source/output rates");
    for (int frame = 0; frame < 8; ++frame) {
        const auto sample = source.audio->sample(frame / 8.0);
        check(sample.left == sample.right, "mono is duplicated to stereo");
    }
    check(source.audio->sample(0.99).left == -1, "last sample is held inside final source cell");
    for (const auto time : { -1.0, 1.0, 100.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() }) {
        const auto sample = source.audio->sample(time);
        check(sample.left == 0 && sample.right == 0, "out-of-range and non-finite times are silent");
    }
    const auto lastTime = std::nextafter(source.audio->duration(), 0.0);
    check(source.audio->sample(lastTime).left == -1, "rounded endpoint cannot read beyond PCM");
    check(source.audio->querySeconds(0, lastTime, source.audio->duration()).minimum == -1, "tiny final waveform intervals preserve the last frame");
    const auto one = mono(44100, { 0.7f });
    check(one.audio->sample(0).left == 0.7f && one.audio->sample(0.5 / 44100).right == 0.7f, "single-frame sources interpolate safely");
    const auto silent = mono(48000, std::vector<float>(1000));
    const auto silence = silent.audio->queryFrames(0, 0, 1000);
    check(silence.minimum == 0 && silence.maximum == 0, "silent source has silent waveform");

    const std::vector<float> left { -2, 0.5f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN() };
    const std::vector<float> right { 2, -0.5f, -0.2f, 0.3f };
    const std::array<std::span<const float>, 2> stereo { left, right };
    const auto paired = Audio::fromPlanar(2, stereo);
    check(static_cast<bool>(paired), "valid stereo prepares");
    check(paired.audio->sample(0).left == -2 && paired.audio->sample(0).right == 2, "stereo channel order and finite over-range PCM are preserved");
    check(paired.audio->sample(1).left == 0 && paired.audio->sample(1.5).left == 0, "non-finite decoded samples sanitize to silence");
    check(paired.audio->queryFrames(0, 2, 4).maximum == 0, "waveform uses sanitized PCM");
    const auto extremes = mono(1, { std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() });
    check(extremes.audio->sample(0.5).left == 0, "interpolation between finite extremes avoids intermediate overflow");
    auto copied = paired.audio;
    check(copied == paired.audio && copied->sample(0).left == -2, "immutable decoded ownership is shared");

    int decodes = 0;
    const Audio::Decoder decoder = [&](float* const*, std::size_t, std::size_t, std::size_t) { ++decodes; return true; };
    for (const auto rate : { 0.0, -1.0, 384001.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() }) {
        check(!Audio::create(rate, 1, 10, decoder), "invalid sample rates reject before decode");
    }
    check(!Audio::create(48000, 0, 10, decoder) && !Audio::create(48000, 3, 10, decoder), "unsupported channels reject without truncation");
    check(!Audio::create(48000, 1, 0, decoder), "empty PCM rejects");
    check(!Audio::create(48000, 1, std::numeric_limits<std::uint64_t>::max(), decoder), "huge frame counts reject before arithmetic overflow");
    check(!Audio::create(48000, 2, Audio::maximumFrames, decoder), "combined PCM and waveform budget rejects before allocation");
    check(!Audio::create(48000, 1, Audio::maximumFrames, decoder), "waveform bytes count toward the memory budget");
    check(decodes == 0, "invalid metadata never invokes decoder");
    check(!Audio::create(48000, 1, 10, {}), "missing decoder rejects");
    const std::array<std::span<const float>, 2> unequal { left, std::span<const float>(right.data(), 2) };
    check(!Audio::fromPlanar(48000, unequal), "unequal channel lengths reject");
    check(!Audio::create(48000, 1, 10, [](float* const*, std::size_t, std::size_t, std::size_t) { return false; }), "decode failure exposes no partial source");
    check(!Audio::create(48000, 1, 10, [](float* const*, std::size_t, std::size_t, std::size_t) -> bool { throw std::runtime_error("decode failed"); }), "decoder exceptions become errors");

    std::array<std::vector<float>, 2> expected { std::vector<float>(12345), std::vector<float>(12345) };
    std::uint32_t state = 7;
    for (std::size_t index = 0; index < expected[0].size(); ++index) {
        state = state * 1664525u + 1013904223u;
        expected[0][index] = static_cast<float>(state % 2001) / 1000 - 1;
        expected[1][index] = -expected[0][index] * 0.4f;
    }
    expected[0][8192] = 9;
    expected[1][63] = -8;
    std::size_t nextFrame = 0;
    const auto longSource = Audio::create(44100, 2, expected[0].size(), [&](float* const* output, std::size_t channels, std::size_t first, std::size_t frames) {
        check(first == nextFrame && frames <= Audio::decodeBlockFrames && channels == 2, "decoder blocks are bounded and sequential");
        for (std::size_t channel = 0; channel < channels; ++channel) {
            std::copy_n(expected[channel].data() + first, frames, output[channel]);
        }
        nextFrame += frames;
        return true;
    });
    check(static_cast<bool>(longSource) && nextFrame == expected[0].size(), "decoder fills complete source including partial final block");
    for (std::size_t first = 0; first < expected[0].size(); first += 29) {
        for (const std::size_t count : { 1, 2, 63, 64, 65, 127, 257, 4096, 20000 }) {
            const auto last = std::min(expected[0].size(), first + count);
            for (std::size_t channel = 0; channel < 2; ++channel) {
                const auto minimum = *std::min_element(expected[channel].begin() + first, expected[channel].begin() + last);
                const auto maximum = *std::max_element(expected[channel].begin() + first, expected[channel].begin() + last);
                const auto peak = longSource.audio->queryFrames(channel, first, first + count);
                check(peak.minimum == minimum && peak.maximum == maximum, "pyramid matches exact PCM extrema across partial and dyadic blocks");
            }
        }
    }
    check(longSource.audio->queryFrames(0, 0, 12345).maximum == 9, "coarse left waveform preserves a one-sample transient");
    check(longSource.audio->queryFrames(1, 0, 12345).minimum == -8, "coarse right waveform preserves independent transient");
    check(source.audio->queryFrames(1, 0, 4).minimum == -1, "mono waveform duplicates right channel");
    const auto fractional = source.audio->querySeconds(0, 0.24, 0.26);
    check(fractional.minimum == 0 && fractional.maximum == 1, "fractional pixel interval preserves both overlapping cells");
    for (const auto peak : { source.audio->queryFrames(0, 2, 2), source.audio->queryFrames(0, 99, 100), source.audio->queryFrames(2, 0, 4), source.audio->querySeconds(0, 1, 2), source.audio->querySeconds(0, 0, std::numeric_limits<double>::infinity()) }) {
        check(peak.minimum == 0 && peak.maximum == 0, "empty and invalid waveform queries are silent");
    }
    std::cout << "Prepared audio contracts passed\n";
}
