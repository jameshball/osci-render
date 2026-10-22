#pragma once

#include "LiveSourceFrames.h"
#include "PreparedBlenderInput.h"
#include "../model/Document.h"
#include <functional>

namespace motion {
// All methods and timer callbacks belong to the message thread. Sessions are
// explicitly started; loading a document never opens a network listener.
class LiveBlenderController final : private juce::Timer {
public:
    LiveBlenderController(Document& document, std::function<void(std::vector<LiveSourceFrames::Entry>)> publish)
        : document(document), publish(std::move(publish)) { startTimerHz(30); }
    ~LiveBlenderController() override { stopTimer(); }

    juce::Result listen(Id id, bool enabled) {
        prune();
        const auto asset = liveAsset(id);
        if (asset == nullptr) { return juce::Result::fail("The Blender source no longer exists."); }
        const auto* session = findSession(asset->liveIdentity);
        if (enabled) {
            for (const auto& item : sessions) {
                if (item.identity != asset->liveIdentity && item.port == asset->blenderSettings.port && active(item.input->status().state)) {
                    return juce::Result::fail("Another Blender source is using this port. Choose a different port.");
                }
            }
            // Only those listening count: a stopped one holds at most its frozen frame.
            const auto listeningNow = std::count_if(sessions.begin(), sessions.end(), [](const auto& item) { return active(item.input->status().state); });
            if (session == nullptr || !active(session->input->status().state)) {
                if (static_cast<std::size_t>(listeningNow) >= maximumInputs) { return juce::Result::fail("osci-motion listens to at most 16 Blender sources at once. Stop listening on one first."); }
            }
            if (session == nullptr) {
                sessions.push_back({asset->liveIdentity, asset->blenderSettings.port, std::make_unique<PreparedBlenderInput>()});
                session = &sessions.back();
            }
            session->input->listen(asset->blenderSettings.port);
        } else if (session != nullptr) { session->input->stop(); }
        poll();
        return juce::Result::ok();
    }
    juce::Result beginCapture(Id id) {
        prune();
        for (const auto& item : sessions) {
            if (item.input->capturing()) { return juce::Result::fail("Finish or cancel the current Blender capture first."); }
        }
        const auto* session = sessionFor(id);
        if (session == nullptr || !active(session->input->status().state)) { return juce::Result::fail("Start listening before recording a capture."); }
        return session->input->beginCapture(liveAsset(id)->blenderSettings.freezeOnDisconnect) ? juce::Result::ok() : juce::Result::fail("A capture is already running.");
    }
    std::unique_ptr<BlenderCapture> finishCapture(Id id) {
        const auto* session = sessionFor(id);
        return session != nullptr ? session->input->finishCapture() : nullptr;
    }
    void cancelCapture(Id id) {
        const auto* session = sessionFor(id);
        if (session != nullptr) { session->input->cancelCapture(); }
    }
    void cancelAllCaptures() { for (const auto& session : sessions) { session.input->cancelCapture(); } }
    bool capturing(Id id) const {
        const auto* session = sessionFor(id);
        return session != nullptr && session->input->capturing();
    }
    bool captureEnded(Id id) const {
        const auto* session = sessionFor(id);
        return session != nullptr && session->input->captureEnded();
    }
    // What the library says of a source: its capture while one runs, else
    // its connection.
    juce::String statusText(Id id) const {
        if (!capturing(id)) { return connectionText(id); }
        const auto error = captureError(id);
        return error.isNotEmpty() ? error : juce::String("Recording capture");
    }
    juce::String captureError(Id id) const {
        const auto* session = sessionFor(id);
        return session != nullptr ? session->input->captureError() : juce::String();
    }
    juce::String connectionText(Id id) const {
        const auto asset = liveAsset(id);
        if (asset == nullptr) { return "Source unavailable"; }
        const auto dot = juce::String::fromUTF8(separator.data(), static_cast<int>(separator.size()));
        const auto* session = findSession(asset->liveIdentity);
        if (session == nullptr) { return "Offline" + dot + "start listening to connect Blender"; }
        const auto status = session->input->status();
        if (session->input->preparationFailed()) { return "Could not prepare the incoming frame"; }
        if (status.message.isNotEmpty()) { return status.message; }
        switch (status.state) {
            case BlenderReceiver::State::connected: return "Connected" + dot + juce::String(status.acceptedFrames) + (status.acceptedFrames == 1 ? " frame received" : " frames received");
            case BlenderReceiver::State::listening: return "Listening on port " + juce::String(status.port) + dot + "waiting for Blender";
            case BlenderReceiver::State::failed: return "Could not start the listener";
            case BlenderReceiver::State::stopped: return asset->blenderSettings.freezeOnDisconnect && session->input->frame().source != nullptr ? "Stopped" + dot + "last frame frozen" : juce::String("Stopped");
        }
        return {};
    }
    bool listening(Id id) const {
        const auto* session = sessionFor(id);
        return session != nullptr && active(session->input->status().state);
    }
    void poll() {
        prune();
        for (const auto& session : sessions) { session.input->checkCaptureTime(); }
        std::vector<LiveSourceFrames::Entry> entries;
        for (const auto& asset : document.mainProject().assets) {
            const auto* session = findSession(asset->liveIdentity);
            if (session == nullptr) { continue; }
            const auto status = session->input->status();
            const auto frame = session->input->frame();
            const bool visible = asset->blenderSettings.freezeOnDisconnect
                || (status.state == BlenderReceiver::State::connected && frame.connection == status.connection);
            entries.push_back({asset->liveIdentity, visible ? frame.source : nullptr});
        }
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
        int port;
        std::unique_ptr<PreparedBlenderInput> input;
    };
    static constexpr std::size_t maximumInputs = 16;
    const Asset* liveAsset(Id id) const {
        const auto asset = findAsset(document.mainProject().assets, id);
        return asset != nullptr && asset->liveIdentity != nullptr ? asset.get() : nullptr;
    }
    // Sessions own their inputs through pointers, so a const session still
    // controls its input.
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
    static bool active(BlenderReceiver::State state) { return state == BlenderReceiver::State::listening || state == BlenderReceiver::State::connected; }
    void prune() {
        std::erase_if(sessions, [this](const auto& session) {
            return std::none_of(document.mainProject().assets.begin(), document.mainProject().assets.end(), [&](const auto& asset) { return asset->liveIdentity == session.identity; });
        });
    }
    void timerCallback() override { poll(); }
    Document& document;
    std::function<void(std::vector<LiveSourceFrames::Entry>)> publish;
    std::vector<Session> sessions;
    std::vector<LiveSourceFrames::Entry> previous;
};
}
