#include "GpuDevice.h"

#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#include <array>
#include <atomic>
#include <cstring>
#include <map>
#include <mutex>

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

    ~Impl() {
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
    deviceDescriptor.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType, WGPUStringView message, void* owner, void*) {
        auto* into = static_cast<Device::Impl*>(owner);
        {
            const juce::SpinLock::ScopedLockType lock(into->errorLock);
            into->error = text(message);
        }
        into->errors.fetch_add(1);
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
    std::vector<WGPUBindGroupEntry> entries;
    const auto bind = [&](WGPUBuffer buffer, std::size_t size) {
        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = static_cast<std::uint32_t>(entries.size());
        entry.buffer = buffer;
        entry.size = std::max<std::size_t>(16, (size + 3) & ~std::size_t {3});
        entries.push_back(entry);
        buffers.push_back(buffer);
    };
    for (const auto& input : inputs) {
        const auto buffer = impl->buffer(input.size(), WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);
        if (!input.empty()) { wgpuQueueWriteBuffer(impl->queue, buffer, 0, input.data(), (input.size() + 3) & ~std::size_t {3}); }
        bind(buffer, input.size());
    }
    if (!uniforms.empty()) {
        // Uniform blocks are padded to sixteen bytes.
        std::vector<std::byte> padded(uniforms.begin(), uniforms.end());
        padded.resize((padded.size() + 15) & ~std::size_t {15});
        const auto buffer = impl->buffer(padded.size(), WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst);
        wgpuQueueWriteBuffer(impl->queue, buffer, 0, padded.data(), padded.size());
        bind(buffer, padded.size());
    }
    const auto output = impl->buffer(outputBytes, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
    bind(output, outputBytes);
    const auto readBack = impl->buffer(outputBytes, WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst);
    buffers.push_back(readBack);
    const auto layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroupDescriptor groupDescriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    groupDescriptor.layout = layout;
    groupDescriptor.entryCount = entries.size();
    groupDescriptor.entries = entries.data();
    const auto group = wgpuDeviceCreateBindGroup(impl->device, &groupDescriptor);
    wgpuBindGroupLayoutRelease(layout);
    WGPUCommandEncoderDescriptor encoderDescriptor = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    const auto encoder = wgpuDeviceCreateCommandEncoder(impl->device, &encoderDescriptor);
    WGPUComputePassDescriptor passDescriptor = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
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
    wgpuQueueSubmit(impl->queue, 1, &commands);
    wgpuCommandBufferRelease(commands);
    std::atomic<int> mapped {0};
    WGPUBufferMapCallbackInfo mapCallback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    mapCallback.mode = WGPUCallbackMode_AllowSpontaneous;
    mapCallback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* into, void*) { static_cast<std::atomic<int>*>(into)->store(status == WGPUMapAsyncStatus_Success ? 1 : -1); };
    mapCallback.userdata1 = &mapped;
    wgpuBufferMapAsync(readBack, WGPUMapMode_Read, 0, copied, mapCallback);
    while (mapped.load() == 0) { wgpuDevicePoll(impl->device, true, nullptr); }
    std::optional<std::vector<std::byte>> result;
    if (mapped.load() > 0 && impl->errors.load() == errorsBefore) {
        const auto* data = static_cast<const std::byte*>(wgpuBufferGetConstMappedRange(readBack, 0, copied));
        if (data != nullptr) { result.emplace(data, data + outputBytes); }
        wgpuBufferUnmap(readBack);
    }
    wgpuBindGroupRelease(group);
    for (auto buffer : buffers) {
        wgpuBufferDestroy(buffer);
        wgpuBufferRelease(buffer);
    }
    return result;
}
}
