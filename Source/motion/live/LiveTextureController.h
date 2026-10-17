#pragma once

#include "LiveSourceFrames.h"
#include "TextureSource.h"
#include "../import/RasterBeamBuilder.h"
#include "../model/Document.h"
#include <osci_texture_interop/osci_texture_interop.h>
#include <functional>

namespace motion {

// What a capture of a shared picture holds: each traced frame and when it
// arrived, from the start of the capture.
struct TextureCapture {
    static constexpr std::size_t maximumBytes = 192 * 1024 * 1024;
    static constexpr double frameRate = 30;
    double duration = 0;
    std::size_t pointsPerFrame = 0;
    std::vector<double> times;
    std::vector<std::vector<PointSample>> frames; // Empty: nothing lit.
    std::size_t bytes = 0;
    bool full = false;

    // At a steady rate, each frame what had arrived by then.
    PreparedPointFrames::Result steady() const {
        const auto count = static_cast<std::uint64_t>(std::max(1.0, std::ceil(duration * frameRate)));
        const auto error = PreparedPointFrames::validate(frameRate, count, pointsPerFrame);
        if (!error.empty()) { return {nullptr, error}; }
        std::vector<PointSample> points;
        points.reserve(static_cast<std::size_t>(count) * pointsPerFrame);
        std::size_t source = 0;
        for (std::uint64_t index = 0; index < count; ++index) {
            const auto time = static_cast<double>(index) / frameRate;
            while (source + 1 < times.size() && times[source + 1] <= time) { ++source; }
            const auto* frame = source < frames.size() && frames[source].size() == pointsPerFrame ? &frames[source] : nullptr;
            if (frame != nullptr) {
                points.insert(points.end(), frame->begin(), frame->end());
            } else {
                points.resize(points.size() + pointsPerFrame);
            }
        }
        return PreparedPointFrames::create(frameRate, count, pointsPerFrame, std::move(points));
    }
};

// One shared picture: received on its own GL thread and fitted small at
// once, then traced on a thread of its own, the newest frame only, so it
// never falls behind.
class LiveTextureInput final : private juce::Thread {
public:
    LiveTextureInput() : juce::Thread("osci-motion live picture") { startThread(juce::Thread::Priority::normal); }
    ~LiveTextureInput() override {
        disconnect();
        signalThreadShouldExit();
        wake.signal();
        stopThread(4000);
    }

    // Message thread.
    void setSettings(const RasterSettings& value) {
        const juce::SpinLock::ScopedLockType lock(settingsLock);
        settings = value;
        dimension.store(value.resolution);
    }
    void connect(const osci::texture::SourceInfo& source) {
        disconnect();
        stopped.store(false);
        grabber = std::make_unique<osci::texture::OpenGLTextureFrameGrabber>(source);
        // Frames flow once it has started (it says so with its first).
        grabber->inputStarted = [](juce::String, int, int) {};
        grabber->frameReady = [this](const std::vector<std::uint8_t>& rgba, int width, int height, bool bottomUp) { received(rgba, width, height, bottomUp); };
        grabber->inputStopped = [this] { stopped.store(true); };
        grabber->inputFailed = [this](juce::String) { stopped.store(true); };
        grabber->start();
    }
    void disconnect() {
        grabber = nullptr;
        const juce::SpinLock::ScopedLockType lock(latestLock);
        latest = nullptr;
    }
    bool connected() const { return grabber != nullptr && !stopped.load(); }
    bool stoppedSending() const { return grabber != nullptr && stopped.load(); }
    std::uint64_t tracedFrames() const { return traced.load(); }
    std::shared_ptr<const PreparedSource> frame() const {
        const juce::SpinLock::ScopedLockType lock(latestLock);
        return latest;
    }

    void beginCapture() {
        auto made = std::make_unique<TextureCapture>();
        {
            const juce::SpinLock::ScopedLockType lock(settingsLock);
            made->pointsPerFrame = settings.pointsPerFrame;
        }
        made->times.reserve(8192);
        made->frames.reserve(8192);
        const juce::SpinLock::ScopedLockType lock(captureLock);
        capture = std::move(made);
        captureStart = now();
    }
    std::unique_ptr<TextureCapture> finishCapture() {
        const juce::SpinLock::ScopedLockType lock(captureLock);
        if (capture != nullptr) { capture->duration = now() - captureStart; }
        return std::move(capture);
    }
    bool capturing() const {
        const juce::SpinLock::ScopedLockType lock(captureLock);
        return capture != nullptr;
    }
    // Seconds recorded, and whether the capture has no room left.
    std::pair<double, bool> captureProgress() const {
        const juce::SpinLock::ScopedLockType lock(captureLock);
        return {capture != nullptr ? now() - captureStart : 0, capture != nullptr && capture->full};
    }

private:
    static double now() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }

    // The GL thread (the first frame, the message thread): the picture
    // fitted small, for the tracer to take.
    void received(const std::vector<std::uint8_t>& rgba, int width, int height, bool bottomUp) {
        const juce::SpinLock::ScopedLockType fitting(fitLock);
        const auto size = dimension.load();
        texture::fit(rgba.data(), width, height, bottomUp, size, scratch);
        {
            const juce::SpinLock::ScopedLockType lock(pendingLock);
            std::swap(pending, scratch);
            pendingSize = size;
            hasPending = true;
        }
        wake.signal();
    }

    void run() override {
        std::vector<std::uint8_t> picture;
        while (!threadShouldExit()) {
            wake.wait(250);
            int size = 0;
            {
                const juce::SpinLock::ScopedLockType lock(pendingLock);
                if (!hasPending) { continue; }
                std::swap(picture, pending);
                size = pendingSize;
                hasPending = false;
            }
            RasterSettings chosen;
            {
                const juce::SpinLock::ScopedLockType lock(settingsLock);
                chosen = settings;
            }
            if (size != chosen.resolution) { continue; }
            auto beam = RasterBeamBuilder::build(picture.data(), picture.size(), size, size, chosen, &stopping);
            std::shared_ptr<const PreparedSource> source;
            std::vector<PointSample> kept;
            if (beam) {
                kept = beam.points;
                auto points = PreparedPointFrames::create(TextureCapture::frameRate, 1, chosen.pointsPerFrame, std::move(beam.points));
                if (points) { source = std::make_shared<const PreparedSource>(points.source); }
            }
            {
                const juce::SpinLock::ScopedLockType lock(latestLock);
                latest = source;
            }
            traced.fetch_add(1);
            const juce::SpinLock::ScopedLockType lock(captureLock);
            if (capture == nullptr || capture->full) { continue; }
            const auto bytes = kept.size() * sizeof(PointSample);
            if (capture->bytes + bytes > TextureCapture::maximumBytes || capture->times.size() == capture->times.capacity()) {
                capture->full = true;
                continue;
            }
            capture->times.push_back(now() - captureStart);
            capture->frames.push_back(std::move(kept));
            capture->bytes += bytes;
        }
    }

    std::unique_ptr<osci::texture::OpenGLTextureFrameGrabber> grabber;
    std::atomic<bool> stopped {false};
    std::atomic<bool> stopping {false};
    std::atomic<int> dimension {RasterSettings().resolution};
    mutable juce::SpinLock settingsLock, fitLock, pendingLock, latestLock, captureLock;
    RasterSettings settings;
    std::vector<std::uint8_t> scratch, pending;
    int pendingSize = 0;
    bool hasPending = false;
    juce::WaitableEvent wake;
    std::shared_ptr<const PreparedSource> latest;
    std::atomic<std::uint64_t> traced {0};
    std::unique_ptr<TextureCapture> capture;
    double captureStart = 0;
};

// All methods and timer callbacks belong to the message thread. A source
// connects whenever its app is sharing the picture, and again when it comes
// back.
class LiveTextureController final : private juce::Timer {
public:
    LiveTextureController(Document& document, std::function<void(std::vector<LiveSourceFrames::Entry>)> publish)
        : document(document), publish(std::move(publish)) { startTimerHz(60); }
    ~LiveTextureController() override { stopTimer(); }

    static bool available() { return osci::texture::getOpenGLBackendStatus().isAvailable(); }
    // The pictures apps are sharing now.
    static std::vector<texture::Sender> senders() {
        std::vector<texture::Sender> result;
        if (!available()) { return result; }
        for (const auto& source : osci::texture::listOpenGLSources()) { result.push_back({source.displayName, source.applicationName}); }
        return result;
    }

    juce::String statusText(Id id) const {
        const auto* asset = liveAsset(id);
        if (asset == nullptr) { return "Source unavailable"; }
        if (!available()) { return texture::technology() + " isn't available on this computer"; }
        const auto sender = texture::decode(asset->data);
        const auto* session = findSession(asset->liveIdentity);
        const auto dot = juce::String::fromUTF8(" \xc2\xb7 ");
        if (session != nullptr && session->input->capturing()) {
            const auto [seconds, full] = session->input->captureProgress();
            return full ? "Recording" + dot + "full, stop to keep it" : "Recording" + dot + juce::String(juce::roundToInt(seconds)) + " s";
        }
        if (session == nullptr || !session->input->connected()) {
            return "Waiting for " + (sender.has_value() ? sender->describe() : juce::String("the app")) + " to share its picture";
        }
        return session->rate > 0 ? "Live" + dot + juce::String(juce::roundToInt(session->rate)) + " fps" : juce::String("Connecting...");
    }
    bool live(Id id) const {
        const auto* session = sessionFor(id);
        return session != nullptr && session->input->connected() && session->rate > 0;
    }
    std::shared_ptr<const PreparedSource> frame(Id id) const {
        const auto* session = sessionFor(id);
        return session != nullptr ? session->input->frame() : nullptr;
    }

    juce::Result beginCapture(Id id) {
        for (const auto& session : sessions) {
            if (session.input->capturing()) { return juce::Result::fail("Finish or cancel the current capture first."); }
        }
        const auto* session = sessionFor(id);
        if (session == nullptr || !session->input->connected()) { return juce::Result::fail("The picture isn't arriving yet, so there's nothing to record."); }
        session->input->beginCapture();
        return juce::Result::ok();
    }
    std::unique_ptr<TextureCapture> finishCapture(Id id) {
        const auto* session = sessionFor(id);
        return session != nullptr ? session->input->finishCapture() : nullptr;
    }
    void cancelCapture(Id id) { finishCapture(id); }
    void cancelAllCaptures() { for (const auto& session : sessions) { session.input->finishCapture(); } }
    bool capturing(Id id) const {
        const auto* session = sessionFor(id);
        return session != nullptr && session->input->capturing();
    }

    void poll() {
        reconcile();
        std::vector<LiveSourceFrames::Entry> entries;
        for (const auto& session : sessions) { entries.push_back({session.identity, session.input->frame()}); }
        bool changed = entries.size() != previous.size();
        for (std::size_t index = 0; !changed && index < entries.size(); ++index) {
            changed = entries[index].identity != previous[index].identity || entries[index].source != previous[index].source;
        }
        if (changed) {
            previous = entries;
            publish(std::move(entries));
        }
    }

private:
    struct Session {
        std::shared_ptr<const LiveSourceIdentity> identity;
        texture::Sender sender;
        RasterSettings settings;
        std::unique_ptr<LiveTextureInput> input;
        std::uint64_t counted = 0;
        double countedAt = 0, rate = 0;
    };
    static constexpr std::size_t maximumInputs = 16;

    const Asset* liveAsset(Id id) const {
        const auto asset = findAsset(document.mainProject().assets, id);
        return asset != nullptr && asset->liveIdentity != nullptr && texture::isSource(asset->extension) ? asset.get() : nullptr;
    }
    const Session* findSession(const std::shared_ptr<const LiveSourceIdentity>& identity) const {
        for (const auto& session : sessions) {
            if (identity != nullptr && session.identity == identity) { return &session; }
        }
        return nullptr;
    }
    const Session* sessionFor(Id id) const {
        const auto* asset = liveAsset(id);
        return asset != nullptr ? findSession(asset->liveIdentity) : nullptr;
    }

    // A session for each source the project has, settings kept current, and
    // (now and then) connected when its app is sharing.
    void reconcile() {
        const auto& assets = document.mainProject().assets;
        std::erase_if(sessions, [&](const auto& session) {
            return std::none_of(assets.begin(), assets.end(), [&](const auto& asset) { return asset->liveIdentity == session.identity; });
        });
        const auto time = juce::Time::getMillisecondCounterHiRes() / 1000.0;
        const auto look = time - lastLook > .5;
        std::optional<std::vector<osci::texture::SourceInfo>> sharing;
        for (const auto& asset : assets) {
            if (asset->liveIdentity == nullptr || !texture::isSource(asset->extension)) { continue; }
            const auto sender = texture::decode(asset->data);
            if (!sender.has_value()) { continue; }
            auto found = std::find_if(sessions.begin(), sessions.end(), [&](const auto& session) { return session.identity == asset->liveIdentity; });
            if (found == sessions.end()) {
                if (sessions.size() >= maximumInputs) { continue; }
                sessions.push_back({asset->liveIdentity, *sender, asset->rasterSettings, std::make_unique<LiveTextureInput>()});
                found = std::prev(sessions.end());
                found->input->setSettings(asset->rasterSettings);
            }
            if (found->settings.mode != asset->rasterSettings.mode || found->settings.shading != asset->rasterSettings.shading || found->settings.threshold != asset->rasterSettings.threshold
                || found->settings.invert != asset->rasterSettings.invert || found->settings.resolution != asset->rasterSettings.resolution || found->settings.pointsPerFrame != asset->rasterSettings.pointsPerFrame) {
                found->settings = asset->rasterSettings;
                found->input->setSettings(asset->rasterSettings);
            }
            // Frames a second, over the last second or so.
            if (time - found->countedAt >= 1) {
                const auto traced = found->input->tracedFrames();
                found->rate = found->countedAt > 0 && found->input->connected() ? static_cast<double>(traced - found->counted) / (time - found->countedAt) : 0;
                found->counted = traced;
                found->countedAt = time;
            }
            if (!look || found->input->connected() || !available()) { continue; }
            if (!sharing.has_value()) { sharing = osci::texture::listOpenGLSources(); }
            const auto match = std::find_if(sharing->begin(), sharing->end(), [&](const auto& source) {
                return source.displayName == sender->name && (sender->application.isEmpty() || source.applicationName == sender->application);
            });
            if (match != sharing->end()) {
                found->input->connect(*match);
                found->rate = 0;
            } else if (found->input->stoppedSending()) {
                found->input->disconnect();
            }
        }
        if (look) { lastLook = time; }
    }
    void timerCallback() override { poll(); }

    Document& document;
    std::function<void(std::vector<LiveSourceFrames::Entry>)> publish;
    std::vector<Session> sessions;
    std::vector<LiveSourceFrames::Entry> previous;
    double lastLook = 0;
};
}
