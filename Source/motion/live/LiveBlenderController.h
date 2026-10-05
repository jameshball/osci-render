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
    LiveBlenderController(Document& document, std::function<void(std::shared_ptr<const LiveSourceFrames>)> publish)
        : document(document), publish(std::move(publish)) { startTimerHz(30); }
    ~LiveBlenderController() override { stopTimer(); }

    juce::Result listen(Id id, bool enabled) {
        prune();
        const auto asset = liveAsset(id);
        if (asset == nullptr) { return juce::Result::fail("The Blender source no longer exists."); }
        const auto* session = findSession(asset->liveIdentity);
        if (enabled) {
            for (const auto& item : sessions) {
                const auto state = item.input->status().state;
                if (item.identity != asset->liveIdentity && item.port == asset->blenderSettings.port
                    && (state == BlenderReceiver::State::listening || state == BlenderReceiver::State::connected)) {
                    return juce::Result::fail("Another Blender source is using this port. Choose a different port.");
                }
            }
            if (session == nullptr) {
                if (sessions.size() >= maximumInputs) { return juce::Result::fail("Motion supports up to 16 live Blender inputs in one session."); }
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
        const auto* asset = liveAsset(id);
        const auto* session = asset != nullptr ? findSession(asset->liveIdentity) : nullptr;
        if (session == nullptr || !listening(id)) { return juce::Result::fail("Start listening before recording a capture."); }
        return session->input->beginCapture(asset->blenderSettings.freezeOnDisconnect) ? juce::Result::ok() : juce::Result::fail("A capture is already running.");
    }
    std::unique_ptr<BlenderCapture> finishCapture(Id id) {
        const auto* asset = liveAsset(id);
        const auto* session = asset != nullptr ? findSession(asset->liveIdentity) : nullptr;
        return session != nullptr ? session->input->finishCapture() : nullptr;
    }
    void cancelCapture(Id id) {
        const auto* asset = liveAsset(id);
        const auto* session = asset != nullptr ? findSession(asset->liveIdentity) : nullptr;
        if (session != nullptr) { session->input->cancelCapture(); }
    }
    void cancelAllCaptures() { for (const auto& session : sessions) { session.input->cancelCapture(); } }
    bool capturing(Id id) const {
        const auto* asset = liveAsset(id);
        const auto* session = asset != nullptr ? findSession(asset->liveIdentity) : nullptr;
        return session != nullptr && session->input->capturing();
    }
    juce::String statusText(Id id) const {
        const auto asset = liveAsset(id);
        if (asset == nullptr) { return "Source unavailable"; }
        const auto* session = findSession(asset->liveIdentity);
        if (session == nullptr) { return "Offline - start listening to connect Blender"; }
        const auto status = session->input->status();
        if (session->input->capturing()) {
            const auto error = session->input->captureError();
            return error.isNotEmpty() ? error : "Recording capture | Stop to save to Assets";
        }
        if (session->input->preparationFailed()) { return "Could not prepare the incoming frame"; }
        if (status.message.isNotEmpty()) { return status.message; }
        switch (status.state) {
            case BlenderReceiver::State::connected: return "Connected | " + juce::String(status.acceptedFrames) + (status.acceptedFrames == 1 ? " frame received" : " frames received");
            case BlenderReceiver::State::listening: return "Listening on port " + juce::String(status.port) + " | Waiting for Blender";
            case BlenderReceiver::State::failed: return "Could not start the listener";
            case BlenderReceiver::State::stopped: return asset->blenderSettings.freezeOnDisconnect && session->input->frame().source != nullptr ? "Stopped | last frame frozen" : "Stopped";
        }
        return {};
    }
    bool listening(Id id) const {
        const auto asset = liveAsset(id);
        const auto* session = asset != nullptr ? findSession(asset->liveIdentity) : nullptr;
        if (session == nullptr) { return false; }
        const auto state = session->input->status().state;
        return state == BlenderReceiver::State::connected || state == BlenderReceiver::State::listening;
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
            auto frames = std::make_shared<const LiveSourceFrames>(entries);
            previous = std::move(entries);
            publish(std::move(frames));
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
    void prune() {
        std::erase_if(sessions, [this](const auto& session) {
            return std::none_of(document.mainProject().assets.begin(), document.mainProject().assets.end(), [&](const auto& asset) { return asset->liveIdentity == session.identity; });
        });
    }
    void timerCallback() override { poll(); }
    Document& document;
    std::function<void(std::shared_ptr<const LiveSourceFrames>)> publish;
    std::vector<Session> sessions;
    std::vector<LiveSourceFrames::Entry> previous;
};
}
