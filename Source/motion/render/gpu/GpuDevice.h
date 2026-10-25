#pragma once

#include <JuceHeader.h>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
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
// Kernels are WGSL compute shaders run over storage buffers; any number of
// threads may run them at once, each waiting for its own results.
class Device {
public:
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    // The device, or none when the mode is off, no adapter suits it, the
    // device fails its first check, or it is lost later (a driver reset, a
    // GPU unplugged): from then on hidden lines bake on the CPU. Made once;
    // later calls are cheap.
    static Device* shared();
    // The device if it has been made and is usable, without making it (for
    // saying where frames bake, from a thread that mustn't wait).
    static const Device* existing();
    // Why there is no device, when there isn't.
    static juce::String unavailableReason();

    // "Apple M2 Pro (Metal)", and whether it is a software stand-in.
    juce::String describe() const;
    bool software() const;

    // What the device last reported wrong (a kernel that failed to compile
    // or validate), for diagnostics.
    juce::String lastError() const;

    // A buffer the kernel reads, at its binding (in order from 0).
    using Input = std::span<const std::byte>;
    // Runs `wgsl`'s `main` (workgroups of 64) over `invocations`, with
    // `inputs` bound in order as read-only storage, a uniform block (when
    // given) after them, then a read-write output of `outputBytes`, which
    // comes back. None when the device is lost or the kernel fails.
    std::optional<std::vector<std::byte>> run(const juce::String& wgsl, std::span<const Input> inputs, std::span<const std::byte> uniforms, std::size_t outputBytes, std::uint32_t invocations);

    // A buffer kept on the device once the work that made it is done, for
    // later work to bind; freed when the last holder lets it go.
    class Kept {
    public:
        ~Kept();
        Kept(const Kept&) = delete;
        Kept& operator=(const Kept&) = delete;
        std::size_t size() const { return bytes; }

    private:
        friend class Device;
        Kept(void* buffer, std::size_t bytes) : handle(buffer), bytes(bytes) {}
        void* handle;
        std::size_t bytes;
    };

    // Several kernels run in turn, in one submission, over buffers made for
    // the work and kept on the device between them: one kernel's output is
    // the next one's input there, and only what is asked for comes back.
    class Work {
    public:
        explicit Work(Device& device);
        ~Work();
        Work(const Work&) = delete;
        Work& operator=(const Work&) = delete;

        // A buffer for the work's kernels, by its index: `bytes` of zeros,
        // or a copy of `data` (made before any of the work's kernels run).
        std::size_t buffer(std::size_t bytes);
        // One whose contents are left as they come (for what a kernel
        // writes all of before anything reads it).
        std::size_t scratch(std::size_t bytes);
        std::size_t upload(std::span<const std::byte> data);
        template <typename T>
        std::size_t upload(const std::vector<T>& values) { return upload(std::as_bytes(std::span(values))); }
        // Zeroes a buffer between kernels (to use it again).
        void clear(std::size_t buffer);
        // A buffer kept from earlier work, bound like the work's own.
        std::size_t bind(std::shared_ptr<Kept> kept);
        // One of the work's buffers kept once it is done.
        std::shared_ptr<Kept> keep(std::size_t buffer);
        // A uniform block (padded to sixteen bytes).
        std::size_t uniform(std::span<const std::byte> data);
        template <typename T, std::size_t count>
        std::size_t uniform(const std::array<T, count>& values) { return uniform(std::as_bytes(std::span(values))); }
        // Runs `wgsl`'s `main` (workgroups of 64) over `invocations`, with
        // these buffers bound in order from 0.
        void run(const juce::String& wgsl, std::initializer_list<std::size_t> bindings, std::uint32_t invocations);
        // A buffer to read back once the work is done: its index among what
        // finish gives.
        std::size_t read(std::size_t buffer);
        // Just its first `bytes`.
        std::size_t read(std::size_t buffer, std::size_t bytes);
        // Submits the work and waits for it: what was asked to be read, in
        // order; none when the device is lost or anything failed (wgpu-native
        // ends the process on a submission that fails validation, so nothing
        // that failed is submitted).
        std::optional<std::vector<std::vector<std::byte>>> finish();

    private:
        struct State;
        std::unique_ptr<State> state;
    };

    // Made only by shared() (Impl is the source file's own).
    struct Impl;
    explicit Device(std::unique_ptr<Impl> impl);

private:
    std::unique_ptr<Impl> impl;
};

template <typename T>
Device::Input bytesOf(std::span<const T> values) { return std::as_bytes(values); }
}
