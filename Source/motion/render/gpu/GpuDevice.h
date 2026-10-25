#pragma once

#include <JuceHeader.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace motion::gpu {
// Whether hidden lines bake on the GPU: on one when there is one (and it is
// hardware, not a software stand-in), never, or on whatever WebGPU offers
// (failing when it offers nothing). Set from --gpu in the command-line tool,
// or OSCI_MOTION_GPU, before the device is first asked for.
enum class Mode { automatic, off, force };
std::optional<Mode> modeFrom(const juce::String& text);
void setMode(Mode mode);
Mode mode();

// The process's WebGPU device (through wgpu-native), made on first use and
// kept: Metal on macOS, D3D12 or Vulkan on Windows, Vulkan or GL on Linux.
// Kernels are WGSL compute shaders run over storage buffers; any thread may
// run one, one at a time (each waits for its results).
class Device {
public:
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    // The device, or none when the mode is off, no adapter suits it, or the
    // device fails its first check. Made once; later calls are cheap.
    static Device* shared();
    // Why there is no device, when there isn't.
    static juce::String unavailableReason();

    // "Apple M2 Pro (Metal)", and whether it is a software stand-in.
    juce::String describe() const;
    bool software() const;

    // A buffer the kernel reads, at its binding (in order from 0).
    using Input = std::span<const std::byte>;
    // Runs `wgsl`'s `main` (workgroups of 64) over `invocations`, with
    // `inputs` bound in order as read-only storage, a uniform block (when
    // given) after them, then a read-write output of `outputBytes`, which
    // comes back. None when the device is lost or the kernel fails.
    std::optional<std::vector<std::byte>> run(const juce::String& wgsl, std::span<const Input> inputs, std::span<const std::byte> uniforms, std::size_t outputBytes, std::uint32_t invocations);

    // Made only by shared() (Impl is the source file's own).
    struct Impl;
    explicit Device(std::unique_ptr<Impl> impl);

private:
    std::unique_ptr<Impl> impl;
};

template <typename T>
Device::Input bytesOf(std::span<const T> values) { return std::as_bytes(values); }
}
