#include "GpuDevice.h"

#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <condition_variable>
#include <thread>

namespace motion::gpu {
namespace {
std::atomic<int> requestedMode {-1};
// Whether the device has been made (or found unavailable).
std::atomic<bool> made {false};

juce::String text(WGPUStringView view) {
    if (view.data == nullptr) { return {}; }
    return view.length == WGPU_STRLEN ? juce::String(view.data) : juce::String(view.data, view.length);
}
WGPUStringView view(const char* text) { return WGPUStringView {text, WGPU_STRLEN}; }

// The first device made, or why there is none; made once.
struct Made {
    std::unique_ptr<Device> device;
    juce::String reason;
};
}

std::optional<Mode> modeFrom(const juce::String& text) {
    const auto mode = text.trim().toLowerCase();
    if (mode == "auto" || mode.isEmpty()) { return Mode::automatic; }
    if (mode == "off" || mode == "cpu") { return Mode::off; }
    if (mode == "force" || mode == "on" || mode == "gpu") { return Mode::force; }
    return std::nullopt;
}

void setMode(Mode mode) { requestedMode.store(static_cast<int>(mode)); }

Mode mode() {
    const auto set = requestedMode.load();
    if (set >= 0) { return static_cast<Mode>(set); }
    return modeFrom(juce::SystemStats::getEnvironmentVariable("OSCI_MOTION_GPU", {})).value_or(Mode::automatic);
}

struct Device::Impl {
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    juce::String name;
    bool software = false;
    // A kernel's pipeline, by its source (none for one that failed).
    std::map<juce::String, WGPUComputePipeline> pipelines;
    // Compiling a kernel takes far longer than a spin is for.
    std::mutex compiling;
    // How many errors the device has reported (a kernel that failed to
    // compile or validate), and the last. Runs go on at once on any thread,
    // and an error can't be told apart by which run it came from: a run
    // during which any error came fails, and its frame bakes on the CPU.
    std::atomic<std::uint64_t> errors {0};
    juce::SpinLock errorLock;
    juce::String error;
    // Lost (a driver reset, a GPU unplugged): never used again.
    std::atomic<bool> lost {false};
    // With OSCI_MOTION_GPU_PROFILE set (and the GPU able to), each kernel of
    // a piece of work is timed and the times shown.
    bool profiling = false;

    ~Impl() {
        stopping.store(true);
        pollWake.notify_all();
        if (poller.joinable()) { poller.join(); }
        for (auto& each : pool) {
            wgpuBufferDestroy(each.buffer);
            wgpuBufferRelease(each.buffer);
        }
        for (auto& [source, pipeline] : pipelines) {
            if (pipeline != nullptr) { wgpuComputePipelineRelease(pipeline); }
        }
        if (queue != nullptr) { wgpuQueueRelease(queue); }
        if (device != nullptr) { wgpuDeviceRelease(device); }
        if (adapter != nullptr) { wgpuAdapterRelease(adapter); }
        if (instance != nullptr) { wgpuInstanceRelease(instance); }
    }

    WGPUComputePipeline pipelineFor(const juce::String& wgsl) {
        const std::lock_guard lock(compiling);
        const auto found = pipelines.find(wgsl);
        if (found != pipelines.end()) { return found->second; }
        const auto before = errors.load();
        WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
        source.code = view(wgsl.toRawUTF8());
        WGPUShaderModuleDescriptor moduleDescriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        moduleDescriptor.nextInChain = &source.chain;
        const auto module = wgpuDeviceCreateShaderModule(device, &moduleDescriptor);
        if (module == nullptr) { return pipelines.emplace(wgsl, nullptr).first->second; }
        WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        descriptor.compute.module = module;
        descriptor.compute.entryPoint = view("main");
        const auto pipeline = wgpuDeviceCreateComputePipeline(device, &descriptor);
        wgpuShaderModuleRelease(module);
        if (pipeline == nullptr || errors.load() != before) {
            if (pipeline != nullptr) { wgpuComputePipelineRelease(pipeline); }
            return pipelines.emplace(wgsl, nullptr).first->second;
        }
        return pipelines.emplace(wgsl, pipeline).first->second;
    }

    // Waits until `done()`, which a callback makes true. One thread polls
    // the device for everyone waiting, often: a blocking poll wakes only
    // every millisecond and a half or so, far longer than most kernels take,
    // and every thread polling at once would keep the device's lock from a
    // thread submitting work.
    std::atomic<int> waiters {0};
    std::atomic<bool> stopping {false};
    std::mutex pollMutex;
    std::condition_variable pollWake;
    std::thread poller;
    template <typename Done>
    void waitUntil(Done&& done) {
        if (done()) { return; }
        startPolling();
        waiters.fetch_add(1);
        pollWake.notify_one();
        for (int spins = 0; !done(); ++spins) {
            if (spins < 2000) {
                std::this_thread::yield();
            } else {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
        }
        waiters.fetch_sub(1);
    }
    void startPolling() {
        std::call_once(pollerStarted, [this] {
            poller = std::thread([this] {
                while (!stopping.load()) {
                    if (waiters.load() == 0) {
                        std::unique_lock lock(pollMutex);
                        pollWake.wait_for(lock, std::chrono::milliseconds(10), [this] { return waiters.load() > 0 || stopping.load(); });
                        continue;
                    }
                    wgpuDevicePoll(device, false, nullptr);
                    std::this_thread::sleep_for(std::chrono::microseconds(20));
                }
            });
        });
    }
    std::once_flag pollerStarted;

    // Buffers work is done with, kept to be used again: made and freed
    // every frame, buffers cost more than the kernels that use them (each
    // made one is filled with zeros first).
    struct Pooled {
        WGPUBuffer buffer;
        std::size_t size;
        WGPUBufferUsage usage;
    };
    juce::SpinLock poolLock;
    std::vector<Pooled> pool;
    std::size_t pooledBytes = 0;
    static constexpr std::size_t mostPooled = std::size_t {3} << 30;
    // Sizes come in steps of an eighth (and of four kilobytes), so a buffer
    // fits the next request nearly its size.
    static std::size_t pooledSize(std::size_t size) {
        size = std::max<std::size_t>(16, (size + 3) & ~std::size_t {3});
        std::size_t step = 4096;
        while (step * 16 <= size) { step *= 2; }
        return (size + step - 1) / step * step;
    }
    WGPUBuffer acquire(std::size_t size, WGPUBufferUsage usage) {
        {
            const juce::SpinLock::ScopedLockType lock(poolLock);
            auto best = pool.end();
            for (auto each = pool.begin(); each != pool.end(); ++each) {
                if (each->usage == usage && each->size == size) {
                    best = each;
                    break;
                }
            }
            if (best != pool.end()) {
                const auto found = best->buffer;
                pooledBytes -= best->size;
                pool.erase(best);
                return found;
            }
        }
        return buffer(size, usage);
    }
    void release(WGPUBuffer buffer, std::size_t size, WGPUBufferUsage usage) {
        {
            const juce::SpinLock::ScopedLockType lock(poolLock);
            if (pooledBytes + size <= mostPooled) {
                pool.push_back({buffer, size, usage});
                pooledBytes += size;
                return;
            }
        }
        wgpuBufferDestroy(buffer);
        wgpuBufferRelease(buffer);
    }

    WGPUBuffer buffer(std::size_t size, WGPUBufferUsage usage) {
        WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
        // Storage bindings may not be empty, and sizes keep to four bytes.
        descriptor.size = std::max<std::size_t>(16, (size + 3) & ~std::size_t {3});
        descriptor.usage = usage;
        return wgpuDeviceCreateBuffer(device, &descriptor);
    }
};

Device::Device(std::unique_ptr<Impl> made) : impl(std::move(made)) {}
Device::~Device() = default;

juce::String Device::describe() const { return impl->name; }
bool Device::software() const { return impl->software; }
juce::String Device::lastError() const {
    const juce::SpinLock::ScopedLockType lock(impl->errorLock);
    return impl->error;
}

namespace {
// Makes the device, or says why there is none.
Made make() {
    Made made;
    const auto wanted = mode();
    if (wanted == Mode::off) {
        made.reason = "the GPU is turned off (--gpu=off or OSCI_MOTION_GPU=off)";
        return made;
    }
    auto impl = std::make_unique<Device::Impl>();
    WGPUInstanceDescriptor instanceDescriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
    impl->instance = wgpuCreateInstance(&instanceDescriptor);
    if (impl->instance == nullptr) {
        made.reason = "WebGPU could not start";
        return made;
    }
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.powerPreference = WGPUPowerPreference_HighPerformance;
    WGPURequestAdapterCallbackInfo adapterCallback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    adapterCallback.mode = WGPUCallbackMode_AllowSpontaneous;
    adapterCallback.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView, void* into, void*) {
        if (status == WGPURequestAdapterStatus_Success) { *static_cast<WGPUAdapter*>(into) = adapter; }
    };
    adapterCallback.userdata1 = &impl->adapter;
    wgpuInstanceRequestAdapter(impl->instance, &options, adapterCallback);
    wgpuInstanceProcessEvents(impl->instance);
    if (impl->adapter == nullptr) {
        made.reason = "no GPU adapter";
        return made;
    }
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(impl->adapter, &info) == WGPUStatus_Success) {
        static const std::map<WGPUBackendType, const char*> backends {{WGPUBackendType_Metal, "Metal"}, {WGPUBackendType_Vulkan, "Vulkan"}, {WGPUBackendType_D3D12, "D3D12"}, {WGPUBackendType_D3D11, "D3D11"}, {WGPUBackendType_OpenGL, "OpenGL"}, {WGPUBackendType_OpenGLES, "OpenGL ES"}};
        const auto backend = backends.find(info.backendType);
        auto name = text(info.device);
        if (name.isEmpty()) { name = text(info.description); }
        impl->name = name + " (" + (backend != backends.end() ? backend->second : "unknown backend") + ")";
        impl->software = info.adapterType == WGPUAdapterType_CPU;
        wgpuAdapterInfoFreeMembers(info);
    }
    if (impl->software && wanted == Mode::automatic) {
        made.reason = "only a software stand-in (" + impl->name + "), slower than baking on the CPU";
        return made;
    }
    // As much as the adapter allows: the bake's kernels bind more storage
    // buffers, and larger, than WebGPU's defaults.
    WGPULimits limits = WGPU_LIMITS_INIT;
    const auto knowsLimits = wgpuAdapterGetLimits(impl->adapter, &limits) == WGPUStatus_Success;
    WGPUDeviceDescriptor deviceDescriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    if (knowsLimits) { deviceDescriptor.requiredLimits = &limits; }
    const WGPUFeatureName timestamps = WGPUFeatureName_TimestampQuery;
    if (juce::SystemStats::getEnvironmentVariable("OSCI_MOTION_GPU_PROFILE", {}).isNotEmpty() && wgpuAdapterHasFeature(impl->adapter, timestamps)) {
        deviceDescriptor.requiredFeatureCount = 1;
        deviceDescriptor.requiredFeatures = &timestamps;
        impl->profiling = true;
    }
    deviceDescriptor.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType, WGPUStringView message, void* owner, void*) {
        auto* into = static_cast<Device::Impl*>(owner);
        {
            const juce::SpinLock::ScopedLockType lock(into->errorLock);
            into->error = text(message);
        }
        into->errors.fetch_add(1);
        // Shown with OSCI_MOTION_GPU_DEBUG set, for a kernel that fails to compile.
        static const auto shown = juce::SystemStats::getEnvironmentVariable("OSCI_MOTION_GPU_DEBUG", {}).isNotEmpty();
        if (shown) { std::fprintf(stderr, "WebGPU error: %s\n", text(message).toRawUTF8()); }
        DBG("WebGPU error: " << text(message));
    };
    deviceDescriptor.uncapturedErrorCallbackInfo.userdata1 = impl.get();
    deviceDescriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    deviceDescriptor.deviceLostCallbackInfo.callback = [](WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView message, void* owner, void*) {
        // Released when the process ends: not a loss.
        if (reason == WGPUDeviceLostReason_Destroyed || reason == WGPUDeviceLostReason_CallbackCancelled) { return; }
        auto* into = static_cast<Device::Impl*>(owner);
        into->lost.store(true);
        DBG("WebGPU device lost: " << text(message));
    };
    deviceDescriptor.deviceLostCallbackInfo.userdata1 = impl.get();
    WGPURequestDeviceCallbackInfo deviceCallback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    deviceCallback.mode = WGPUCallbackMode_AllowSpontaneous;
    deviceCallback.callback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView, void* into, void*) {
        if (status == WGPURequestDeviceStatus_Success) { *static_cast<WGPUDevice*>(into) = device; }
    };
    deviceCallback.userdata1 = &impl->device;
    wgpuAdapterRequestDevice(impl->adapter, &deviceDescriptor, deviceCallback);
    wgpuInstanceProcessEvents(impl->instance);
    if (impl->device == nullptr) {
        made.reason = "the GPU (" + impl->name + ") gave no device";
        return made;
    }
    impl->queue = wgpuDeviceGetQueue(impl->device);
    made.device.reset(new Device(std::move(impl)));
    // A first check: a kernel that doubles some numbers must double them.
    const std::vector<float> numbers {1, 2, 3, 4, 5, 6, 7, 8};
    const std::array<Device::Input, 1> inputs {bytesOf(std::span<const float>(numbers))};
    const auto doubled = made.device->run(R"(
@group(0) @binding(0) var<storage, read> numbers: array<f32>;
@group(0) @binding(1) var<storage, read_write> twice: array<f32>;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    if (id.x < arrayLength(&numbers)) { twice[id.x] = numbers[id.x] * 2.0; }
}
)", inputs, {}, numbers.size() * sizeof(float), static_cast<std::uint32_t>(numbers.size()));
    bool right = doubled.has_value() && doubled->size() >= numbers.size() * sizeof(float);
    for (std::size_t index = 0; right && index < numbers.size(); ++index) {
        float value = 0;
        std::memcpy(&value, doubled->data() + index * sizeof(float), sizeof(float));
        right = value == numbers[index] * 2;
    }
    if (!right) {
        made.reason = "the GPU (" + made.device->describe() + ") failed its first check";
        made.device.reset();
    }
    return made;
}

Made& process() {
    static Made once = [] {
        auto result = make();
        made.store(true);
        return result;
    }();
    return once;
}
}

Device* Device::shared() {
    auto* device = process().device.get();
    return device != nullptr && !device->impl->lost.load() ? device : nullptr;
}

const Device* Device::existing() { return made.load() ? shared() : nullptr; }
juce::String Device::unavailableReason() {
    if (process().device == nullptr) { return process().reason; }
    return process().device->impl->lost.load() ? juce::String("the GPU (" + process().device->describe() + ") was lost") : juce::String();
}

std::optional<std::vector<std::byte>> Device::run(const juce::String& wgsl, std::span<const Input> inputs, std::span<const std::byte> uniforms, std::size_t outputBytes, std::uint32_t invocations) {
    if (impl->lost.load()) { return std::nullopt; }
    const auto errorsBefore = impl->errors.load();
    const auto pipeline = impl->pipelineFor(wgsl);
    if (pipeline == nullptr) { return std::nullopt; }
    std::vector<WGPUBuffer> buffers;
    std::vector<std::pair<std::size_t, WGPUBufferUsage>> pooled;
    // Buffers come from the pool and go back to it.
    const auto acquire = [&](std::size_t bytes, WGPUBufferUsage usage) {
        const auto size = Impl::pooledSize(bytes);
        buffers.push_back(impl->acquire(size, usage));
        pooled.push_back({size, usage});
        return buffers.back();
    };
    std::vector<WGPUBindGroupEntry> entries;
    const auto bind = [&](WGPUBuffer buffer, std::size_t size) {
        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = static_cast<std::uint32_t>(entries.size());
        entry.buffer = buffer;
        entry.size = std::max<std::size_t>(16, (size + 3) & ~std::size_t {3});
        entries.push_back(entry);
    };
    for (const auto& input : inputs) {
        const auto buffer = acquire(input.size(), WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
        if (!input.empty()) { wgpuQueueWriteBuffer(impl->queue, buffer, 0, input.data(), (input.size() + 3) & ~std::size_t {3}); }
        bind(buffer, input.size());
    }
    if (!uniforms.empty()) {
        // Uniform blocks are padded to sixteen bytes.
        std::vector<std::byte> padded(uniforms.begin(), uniforms.end());
        padded.resize((padded.size() + 15) & ~std::size_t {15});
        const auto buffer = acquire(padded.size(), WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
        wgpuQueueWriteBuffer(impl->queue, buffer, 0, padded.data(), padded.size());
        bind(buffer, padded.size());
    }
    const auto output = acquire(outputBytes, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    bind(output, outputBytes);
    const auto readBack = acquire(outputBytes, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    const auto layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroupDescriptor groupDescriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    groupDescriptor.layout = layout;
    groupDescriptor.entryCount = entries.size();
    groupDescriptor.entries = entries.data();
    const auto group = wgpuDeviceCreateBindGroup(impl->device, &groupDescriptor);
    wgpuBindGroupLayoutRelease(layout);
    const auto release = [&] {
        wgpuBindGroupRelease(group);
        for (std::size_t index = 0; index < buffers.size(); ++index) { impl->release(buffers[index], pooled[index].first, pooled[index].second); }
    };
    // Anything wrong so far (a buffer too big, a binding that doesn't fit)
    // must not be submitted: wgpu-native ends the process on a submission
    // that fails validation.
    if (impl->errors.load() != errorsBefore) {
        release();
        return std::nullopt;
    }
    WGPUCommandEncoderDescriptor encoderDescriptor = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    const auto encoder = wgpuDeviceCreateCommandEncoder(impl->device, &encoderDescriptor);
    WGPUComputePassDescriptor passDescriptor = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    // A pooled output may hold anything: it starts at zero, as a made one does.
    wgpuCommandEncoderClearBuffer(encoder, output, 0, Impl::pooledSize(outputBytes));
    const auto pass = wgpuCommandEncoderBeginComputePass(encoder, &passDescriptor);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
    // A dispatch may have at most 65535 workgroups along a side: more go along y.
    const auto groups = (invocations + 63) / 64;
    const auto across = std::min<std::uint32_t>(groups, 65535);
    wgpuComputePassEncoderDispatchWorkgroups(pass, across, (groups + across - 1) / std::max<std::uint32_t>(across, 1), 1);
    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);
    const auto copied = std::max<std::size_t>(16, (outputBytes + 3) & ~std::size_t {3});
    wgpuCommandEncoderCopyBufferToBuffer(encoder, output, 0, readBack, 0, copied);
    const auto commands = wgpuCommandEncoderFinish(encoder, nullptr);
    wgpuCommandEncoderRelease(encoder);
    if (impl->errors.load() != errorsBefore) {
        wgpuCommandBufferRelease(commands);
        release();
        return std::nullopt;
    }
    wgpuQueueSubmit(impl->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    std::atomic<int> mapped {0};
    WGPUBufferMapCallbackInfo mapCallback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    mapCallback.mode = WGPUCallbackMode_AllowSpontaneous;
    mapCallback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* into, void*) { static_cast<std::atomic<int>*>(into)->store(status == WGPUMapAsyncStatus_Success ? 1 : -1); };
    mapCallback.userdata1 = &mapped;
    wgpuBufferMapAsync(readBack, WGPUMapMode_Read, 0, copied, mapCallback);
    impl->waitUntil([&] { return mapped.load() != 0; });
    std::optional<std::vector<std::byte>> result;
    if (mapped.load() > 0 && impl->errors.load() == errorsBefore) {
        const auto* data = static_cast<const std::byte*>(wgpuBufferGetConstMappedRange(readBack, 0, copied));
        if (data != nullptr) { result.emplace(data, data + outputBytes); }
        wgpuBufferUnmap(readBack);
    }
    release();
    return result;
}
}

namespace motion::gpu {
struct Device::Work::State {
    Device& device;
    std::uint64_t errorsBefore;
    std::vector<WGPUBuffer> buffers;
    // Each buffer's size as bound, and as made (pooled), and its use.
    std::vector<std::size_t> sizes, made;
    std::vector<WGPUBufferUsage> usages;
    // Buffers not the work's to free: kept from earlier work, or for later.
    std::vector<bool> borrowed;
    std::vector<std::shared_ptr<Kept>> held;
    std::vector<WGPUBindGroup> groups;
    struct Read {
        WGPUBuffer buffer;
        std::size_t size, made;
    };
    std::vector<Read> reads;
    WGPUCommandEncoder encoder = nullptr;
    // Kernels run one after another in one pass, while nothing else comes
    // between them (a pass costs far more than a kernel's dispatch).
    WGPUComputePassEncoder pass = nullptr;
    bool failed = false;
    void endPass() {
        if (pass == nullptr) { return; }
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        pass = nullptr;
    }
    // Profiling: each kernel's timestamps, and what it binds (to name it).
    static constexpr std::uint32_t mostTimed = 256;
    WGPUQuerySet timestamps = nullptr;
    std::vector<juce::String> timed;

    explicit State(Device& device) : device(device), errorsBefore(device.impl->errors.load()) {
        WGPUCommandEncoderDescriptor descriptor = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
        encoder = wgpuDeviceCreateCommandEncoder(device.impl->device, &descriptor);
        failed = device.impl->lost.load() || encoder == nullptr;
    }
    ~State() {
        if (pass != nullptr) { wgpuComputePassEncoderRelease(pass); }
        if (encoder != nullptr) { wgpuCommandEncoderRelease(encoder); }
        if (timestamps != nullptr) { wgpuQuerySetRelease(timestamps); }
        for (auto group : groups) { wgpuBindGroupRelease(group); }
        for (std::size_t index = 0; index < buffers.size(); ++index) {
            if (!borrowed[index]) { device.impl->release(buffers[index], made[index], usages[index]); }
        }
        for (const auto& read : reads) { device.impl->release(read.buffer, read.made, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst); }
    }
    std::size_t add(std::size_t bytes, WGPUBufferUsage usage) {
        const auto size = Impl::pooledSize(bytes);
        buffers.push_back(device.impl->acquire(size, usage));
        sizes.push_back(std::max<std::size_t>(16, (bytes + 3) & ~std::size_t {3}));
        made.push_back(size);
        usages.push_back(usage);
        borrowed.push_back(false);
        return buffers.size() - 1;
    }
};

Device::Work::Work(Device& device) : state(std::make_unique<State>(device)) {}
Device::Work::~Work() = default;

std::size_t Device::Work::buffer(std::size_t bytes) {
    const auto index = scratch(bytes);
    clear(index);
    return index;
}

std::size_t Device::Work::scratch(std::size_t bytes) { return state->add(bytes, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst); }

std::size_t Device::Work::upload(std::span<const std::byte> data) {
    const auto index = scratch(data.size());
    if (!data.empty()) {
        // Writes keep to four bytes.
        std::vector<std::byte> padded(data.begin(), data.end());
        padded.resize((padded.size() + 3) & ~std::size_t {3});
        wgpuQueueWriteBuffer(state->device.impl->queue, state->buffers[index], 0, padded.data(), padded.size());
    }
    return index;
}

Device::Kept::~Kept() {
    auto* buffer = static_cast<WGPUBuffer>(handle);
    wgpuBufferDestroy(buffer);
    wgpuBufferRelease(buffer);
}

void Device::Work::clear(std::size_t buffer) {
    state->endPass();
    if (!state->failed) { wgpuCommandEncoderClearBuffer(state->encoder, state->buffers[buffer], 0, state->made[buffer]); }
}

std::size_t Device::Work::bind(std::shared_ptr<Kept> kept) {
    state->buffers.push_back(static_cast<WGPUBuffer>(kept->handle));
    state->sizes.push_back(kept->size());
    state->made.push_back(kept->size());
    state->usages.push_back(WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    state->borrowed.push_back(true);
    state->held.push_back(std::move(kept));
    return state->buffers.size() - 1;
}

std::shared_ptr<Device::Kept> Device::Work::keep(std::size_t buffer) {
    state->borrowed[buffer] = true;
    return std::shared_ptr<Kept>(new Kept(state->buffers[buffer], state->sizes[buffer]));
}

std::size_t Device::Work::uniform(std::span<const std::byte> data) {
    std::vector<std::byte> padded(data.begin(), data.end());
    padded.resize(std::max<std::size_t>(16, (padded.size() + 15) & ~std::size_t {15}));
    const auto index = state->add(padded.size(), WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
    wgpuQueueWriteBuffer(state->device.impl->queue, state->buffers[index], 0, padded.data(), padded.size());
    return index;
}

void Device::Work::run(const juce::String& wgsl, std::initializer_list<std::size_t> bindings, std::uint32_t invocations) {
    if (state->failed) { return; }
    const auto pipeline = state->device.impl->pipelineFor(wgsl);
    if (pipeline == nullptr) {
        state->failed = true;
        return;
    }
    std::vector<WGPUBindGroupEntry> entries;
    for (const auto index : bindings) {
        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = static_cast<std::uint32_t>(entries.size());
        entry.buffer = state->buffers[index];
        entry.size = state->sizes[index];
        entries.push_back(entry);
    }
    const auto layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroupDescriptor groupDescriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    groupDescriptor.layout = layout;
    groupDescriptor.entryCount = entries.size();
    groupDescriptor.entries = entries.data();
    const auto group = wgpuDeviceCreateBindGroup(state->device.impl->device, &groupDescriptor);
    wgpuBindGroupLayoutRelease(layout);
    state->groups.push_back(group);
    if (state->device.impl->errors.load() != state->errorsBefore) {
        state->failed = true;
        return;
    }
    if (invocations == 0) { return; }
    WGPUComputePassDescriptor passDescriptor = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUPassTimestampWrites writes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
    // Timed, a kernel has a pass of its own.
    const auto timing = state->device.impl->profiling && state->timed.size() < State::mostTimed;
    if (timing) {
        state->endPass();
        if (state->timestamps == nullptr) {
            WGPUQuerySetDescriptor descriptor = WGPU_QUERY_SET_DESCRIPTOR_INIT;
            descriptor.type = WGPUQueryType_Timestamp;
            descriptor.count = 2 * State::mostTimed;
            state->timestamps = wgpuDeviceCreateQuerySet(state->device.impl->device, &descriptor);
        }
        writes.querySet = state->timestamps;
        writes.beginningOfPassWriteIndex = static_cast<std::uint32_t>(2 * state->timed.size());
        writes.endOfPassWriteIndex = writes.beginningOfPassWriteIndex + 1;
        passDescriptor.timestampWrites = &writes;
        // Named by what it binds.
        juce::StringArray names;
        for (const auto& line : juce::StringArray::fromLines(wgsl)) {
            if (line.contains("@binding(")) { names.add(line.fromFirstOccurrenceOf(">", false, false).upToFirstOccurrenceOf(":", false, false).trim()); }
        }
        state->timed.push_back(juce::String(invocations) + " over " + names.joinIntoString(","));
    }
    if (state->pass == nullptr) { state->pass = wgpuCommandEncoderBeginComputePass(state->encoder, &passDescriptor); }
    wgpuComputePassEncoderSetPipeline(state->pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(state->pass, 0, group, 0, nullptr);
    // A dispatch may have at most 65535 workgroups along a side: more go along y.
    const auto groups = (invocations + 63) / 64;
    const auto across = std::min<std::uint32_t>(groups, 65535);
    wgpuComputePassEncoderDispatchWorkgroups(state->pass, across, (groups + across - 1) / across, 1);
    if (timing) { state->endPass(); }
}

std::size_t Device::Work::read(std::size_t buffer) { return read(buffer, state->sizes[buffer]); }

std::size_t Device::Work::read(std::size_t buffer, std::size_t bytes) {
    const auto size = std::clamp<std::size_t>((bytes + 3) & ~std::size_t {3}, 4, state->sizes[buffer]);
    const auto made = Impl::pooledSize(size);
    const auto readBack = state->device.impl->acquire(made, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    state->endPass();
    if (!state->failed) { wgpuCommandEncoderCopyBufferToBuffer(state->encoder, state->buffers[buffer], 0, readBack, 0, size); }
    state->reads.push_back({readBack, size, made});
    return state->reads.size() - 1;
}

std::optional<std::vector<std::vector<std::byte>>> Device::Work::finish() {
    auto& impl = *state->device.impl;
    state->endPass();
    if (state->failed || impl.lost.load() || impl.errors.load() != state->errorsBefore) { return std::nullopt; }
    const auto timedCount = state->timed.size();
    if (timedCount > 0 && state->timestamps != nullptr) {
        const auto bytes = 2 * timedCount * sizeof(std::uint64_t);
        const auto resolved = impl.acquire(Impl::pooledSize(bytes), WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc);
        state->buffers.push_back(resolved);
        state->sizes.push_back(bytes);
        state->made.push_back(Impl::pooledSize(bytes));
        state->usages.push_back(WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc);
        state->borrowed.push_back(false);
        wgpuCommandEncoderResolveQuerySet(state->encoder, state->timestamps, 0, static_cast<std::uint32_t>(2 * timedCount), resolved, 0);
        read(state->buffers.size() - 1, bytes);
    }
    const auto commands = wgpuCommandEncoderFinish(state->encoder, nullptr);
    wgpuCommandEncoderRelease(state->encoder);
    state->encoder = nullptr;
    if (commands == nullptr || impl.errors.load() != state->errorsBefore) {
        if (commands != nullptr) { wgpuCommandBufferRelease(commands); }
        return std::nullopt;
    }
    wgpuQueueSubmit(impl.queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    std::vector<std::atomic<int>> mapped(state->reads.size());
    for (std::size_t index = 0; index < state->reads.size(); ++index) {
        mapped[index].store(0);
        WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowSpontaneous;
        callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* into, void*) { static_cast<std::atomic<int>*>(into)->store(status == WGPUMapAsyncStatus_Success ? 1 : -1); };
        callback.userdata1 = &mapped[index];
        wgpuBufferMapAsync(state->reads[index].buffer, WGPUMapMode_Read, 0, state->reads[index].size, callback);
    }
    // Every kernel done, so its buffers can be used again, whatever is read.
    std::atomic<int> done {0};
    WGPUQueueWorkDoneCallbackInfo doneCallback = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
    doneCallback.mode = WGPUCallbackMode_AllowSpontaneous;
    doneCallback.callback = [](WGPUQueueWorkDoneStatus, WGPUStringView, void* into, void*) { static_cast<std::atomic<int>*>(into)->store(1); };
    doneCallback.userdata1 = &done;
    wgpuQueueOnSubmittedWorkDone(impl.queue, doneCallback);
    impl.waitUntil([&] { return done.load() != 0 && std::all_of(mapped.begin(), mapped.end(), [](const auto& each) { return each.load() != 0; }); });
    if (impl.errors.load() != state->errorsBefore) { return std::nullopt; }
    std::vector<std::vector<std::byte>> results;
    for (std::size_t index = 0; index < state->reads.size(); ++index) {
        if (mapped[index].load() < 0) { return std::nullopt; }
        const auto& read = state->reads[index];
        const auto* data = static_cast<const std::byte*>(wgpuBufferGetConstMappedRange(read.buffer, 0, read.size));
        if (data == nullptr) { return std::nullopt; }
        results.emplace_back(data, data + read.size);
        wgpuBufferUnmap(read.buffer);
    }
    if (timedCount > 0 && state->timestamps != nullptr) {
        // The last read is the timestamps.
        const auto& ticks = results.back();
        double total = 0;
        juce::String report;
        for (std::size_t index = 0; index < timedCount; ++index) {
            std::uint64_t begin = 0, end = 0;
            std::memcpy(&begin, ticks.data() + 16 * index, 8);
            std::memcpy(&end, ticks.data() + 16 * index + 8, 8);
            const auto milliseconds = static_cast<double>(end - begin) / 1.0e6;
            total += milliseconds;
            report << "    " << juce::String(milliseconds, 3) << " ms  " << state->timed[index] << "\n";
        }
        std::fprintf(stderr, "GPU work, %.3f ms in kernels:\n%s", total, report.toRawUTF8());
        results.pop_back();
    }
    return results;
}
}
