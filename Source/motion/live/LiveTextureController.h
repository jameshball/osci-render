#pragma once

#include "CaptureEnding.h"
#include "LiveSourceFrames.h"
#include "TextureSource.h"
#include "../import/BakedSourceArchive.h"
#include "../import/RasterBeamBuilder.h"
#include "../model/Document.h"
#include <osci_texture_interop/osci_texture_interop.h>
#include <functional>

namespace motion {

// What a capture of a shared picture holds: each traced frame and when it
// arrived, from the start of the capture, and why it ended if by itself.
struct TextureCapture {
    static constexpr std::size_t maximumBytes = 192 * 1024 * 1024;
    // What it is saved as, packed: room to spare in a baked source's archive,
    // since each frame is packed on its own to measure it.
    static constexpr std::uint64_t maximumPackedBytes = BakedSourceArchive::maximumCompressedBytes / 8 * 7;
    static constexpr double frameRate = 30;
    double duration = 0;
    std::size_t pointsPerFrame = 0;
    std::vector<double> times;
    std::vector<std::vector<PointSample>> frames; // Empty: nothing lit.
    std::size_t bytes = 0;
    CaptureEnding ending = CaptureEnding::none;

    // The steady frames `seconds` of it make.
    static std::uint64_t steadyFrames(double seconds) { return static_cast<std::uint64_t>(std::max(1.0, std::ceil(seconds * frameRate))); }
    // Takes a frame that arrived `time` seconds in, and how small it packs
    // (0 for a blank one), unless what it's saved as would then not fit.
    // A frame shows from the first steady frame at or after it arrived until
    // the next takes over, and the archive can't fold one repeat into the
    // next (deflate looks back only 32 KiB), so each repeat costs it again.
    bool add(double time, std::vector<PointSample>&& frame, std::size_t packedSize) {
        const auto start = times.empty() ? std::uint64_t {0} : std::max(lastStart, static_cast<std::uint64_t>(std::ceil(time * frameRate)));
        const auto settledThen = packed + lastPacked * (start - lastStart);
        if (start >= mostFrames(settledThen, start, packedSize)) { return false; }
        bytes += frame.size() * sizeof(PointSample);
        times.push_back(time);
        frames.push_back(std::move(frame));
        packed = settledThen;
        lastStart = start;
        lastPacked = packedSize;
        return true;
    }
    // Whether `seconds` of it fill all a saved capture can hold.
    bool full(double seconds) const { return steadyFrames(seconds) >= mostFrames(packed, lastStart, lastPacked); }

    // At a steady rate, each frame what had arrived by then. A capture that
    // ran on past what it can hold (by a moment) keeps what fits.
    PreparedPointFrames::Result steady() const {
        const auto count = std::min(steadyFrames(duration), mostFrames(packed, lastStart, lastPacked));
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

private:
    // The most steady frames it can save, packed: `settled` bytes before
    // frame `start`, then `each` for every frame from there.
    std::uint64_t mostFrames(std::uint64_t settled, std::uint64_t start, std::uint64_t each) const {
        auto most = std::min<std::uint64_t>(PreparedPointFrames::maximumFrames, PreparedPointFrames::maximumBytes / sizeof(PointSample) / std::max<std::size_t>(1, pointsPerFrame));
        if (settled > maximumPackedBytes) {
            most = std::min(most, start);
        } else if (each > 0) {
            most = std::min(most, start + (maximumPackedBytes - settled) / each);
        }
        return std::max<std::uint64_t>(1, most);
    }
    // Packed bytes of the steady frames before the newest arrival's first,
    // which is `lastStart`, and how small that one packs.
    std::uint64_t packed = 0, lastStart = 0, lastPacked = 0;
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
        sourceId = source.opaqueId;
        // Its own flag: the grabber may post that it stopped or failed after
        // it (or this) is gone, and only to this connection.
        stopped = std::make_shared<std::atomic<bool>>(false);
        grabber = std::make_unique<osci::texture::OpenGLTextureFrameGrabber>(source);
        // Frames flow once it has started (it says so with its first).
        grabber->inputStarted = [](juce::String, int, int) {};
        grabber->frameReady = [this](const std::vector<std::uint8_t>& rgba, int width, int height, bool bottomUp) { received(rgba, width, height, bottomUp); };
        grabber->inputStopped = [flag = stopped] { flag->store(true); };
        grabber->inputFailed = [flag = stopped](juce::String) { flag->store(true); };
        grabber->start();
    }
    void disconnect() {
        grabber = nullptr;
        const juce::SpinLock::ScopedLockType lock(latestLock);
        latest = nullptr;
    }
    // Stops receiving but keeps the last picture, until a sender is found again.
    void letGo() { grabber = nullptr; }
    // Which sender it receives from: one instance of it, while it lasts.
    const juce::String& connectedTo() const { return sourceId; }
    bool connected() const { return grabber != nullptr && !stopped->load(); }
    bool stoppedSending() const { return grabber != nullptr && stopped->load(); }
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
        limitLocked();
        if (capture != nullptr && capture->ending == CaptureEnding::none) { capture->duration = now() - captureStart; }
        return std::move(capture);
    }
    // Stops recording, keeping what came before: the sender gone (at its last
    // frame), or no room left (now).
    void endCapture(CaptureEnding why) {
        const juce::SpinLock::ScopedLockType lock(captureLock);
        endLocked(why);
    }
    // Stops recording once it holds all it can be saved with: frames that
    // arrive slowly, or a still picture, still fill it as time passes.
    void limitCapture() {
        const juce::SpinLock::ScopedLockType lock(captureLock);
        limitLocked();
    }
    bool capturing() const {
        const juce::SpinLock::ScopedLockType lock(captureLock);
        return capture != nullptr;
    }
    // A capture still waiting to be saved, which ended by itself.
    bool captureEnded() const {
        const juce::SpinLock::ScopedLockType lock(captureLock);
        return capture != nullptr && capture->ending != CaptureEnding::none;
    }
    double captureSeconds() const {
        const juce::SpinLock::ScopedLockType lock(captureLock);
        return capture != nullptr ? now() - captureStart : 0;
    }

private:
    static double now() { return juce::Time::getMillisecondCounterHiRes() / 1000.0; }
    // Ends a capture that holds all it can be saved with, now.
    void limitLocked() {
        if (capture != nullptr && capture->ending == CaptureEnding::none && capture->full(now() - captureStart)) { endLocked(CaptureEnding::size); }
    }
    void endLocked(CaptureEnding why) {
        if (capture == nullptr || capture->ending != CaptureEnding::none) { return; }
        capture->ending = why;
        const auto lastFrame = capture->times.empty() ? now() - captureStart : capture->times.back() + 1 / TextureCapture::frameRate;
        capture->duration = why == CaptureEnding::disconnected ? std::min(lastFrame, now() - captureStart) : now() - captureStart;
    }

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
            // Measured outside the lock, so the message thread never waits on it.
            const auto packed = !kept.empty() && capturing() ? packedSize(kept) : 0;
            const juce::SpinLock::ScopedLockType lock(captureLock);
            if (capture == nullptr || capture->ending != CaptureEnding::none) { continue; }
            const auto bytes = kept.size() * sizeof(PointSample);
            if (capture->bytes + bytes > TextureCapture::maximumBytes || capture->times.size() == capture->times.capacity()) {
                endLocked(capture->bytes + bytes > TextureCapture::maximumBytes ? CaptureEnding::size : CaptureEnding::updates);
                continue;
            }
            // A frame of another size is saved blank.
            const auto blank = kept.size() != capture->pointsPerFrame;
            if (!capture->add(now() - captureStart, std::move(kept), blank ? 0 : packed)) {
                endLocked(CaptureEnding::size);
                continue;
            }
            limitLocked();
        }
    }
    // How small a frame packs on its own, as its saved capture is packed.
    std::size_t packedSize(const std::vector<PointSample>& frame) {
        const auto size = frame.size() * sizeof(PointSample);
        packing.resize(compressBound(static_cast<uLong>(size)));
        auto packedBytes = static_cast<uLongf>(packing.size());
        const auto status = compress2(packing.data(), &packedBytes, reinterpret_cast<const Bytef*>(frame.data()), static_cast<uLong>(size), 6);
        return status == Z_OK ? static_cast<std::size_t>(packedBytes) : size;
    }

    std::unique_ptr<osci::texture::OpenGLTextureFrameGrabber> grabber;
    std::shared_ptr<std::atomic<bool>> stopped = std::make_shared<std::atomic<bool>>(false);
    juce::String sourceId;
    std::atomic<bool> stopping {false};
    std::atomic<int> dimension {RasterSettings().resolution};
    mutable juce::SpinLock settingsLock, fitLock, pendingLock, latestLock, captureLock;
    RasterSettings settings;
    std::vector<std::uint8_t> scratch, pending, packing;
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
        if (session != nullptr && session->input->capturing()) { return "Recording" + dot + juce::String(juce::roundToInt(session->input->captureSeconds())) + " s"; }
        if (session == nullptr && sessions.size() >= maximumInputs) { return "osci-motion receives at most 16 live pictures at once. Remove one to receive this one."; }
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
    bool captureEnded(Id id) const {
        const auto* session = sessionFor(id);
        return session != nullptr && session->input->captureEnded();
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
        // Listing or connecting to senders can run the message loop, and so
        // this timer: run again inside itself, it would change the sessions
        // this is walking.
        if (reconciling) { return; }
        const juce::ScopedValueSetter<bool> guard(reconciling, true);
        // Its own copy: an edit made while the loop runs replaces the project's.
        const auto assets = document.mainProject().assets;
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
            // A sender gone (its app quit or restarted) doesn't always say so:
            // let go of it once it's no longer listed, keeping its last
            // picture, so the one that takes its name is found.
            // Only once its frames have stopped: listing senders spins the
            // message loop, and costs nothing while pictures still arrive.
            if (look && found->input->connected() && found->rate == 0 && found->countedAt > 0 && found->input->connectedTo().isNotEmpty() && available()) {
                if (!sharing.has_value()) { sharing = osci::texture::listOpenGLSources(); }
                const auto listed = std::any_of(sharing->begin(), sharing->end(), [&](const auto& source) { return source.opaqueId == found->input->connectedTo(); });
                if (!listed) { found->input->letGo(); }
            }
            // Its sender gone (stopped, or let go above): a capture ends there.
            if (!found->input->connected()) { found->input->endCapture(CaptureEnding::disconnected); }
            found->input->limitCapture();
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
    bool reconciling = false;
};
}
