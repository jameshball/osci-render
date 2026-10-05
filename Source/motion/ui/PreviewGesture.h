#pragma once

#include "../model/Document.h"
#include <functional>
#include <optional>

namespace motion::ui {
// An edit shown live while a control is dragged and recorded as one undo step
// when it ends; cancelled, it puts back what it started from. Either applies
// only while nothing else has changed the document since the last preview.
class PreviewGesture {
public:
    explicit PreviewGesture(Document& owner) : document(owner) {}

    void begin() {
        before = document.project();
        revision = document.revision();
        changed = false;
    }
    bool active() const { return before.has_value(); }
    // Something else changed the document mid-gesture.
    bool stale() const { return active() && document.revision() != revision; }
    const Project& start() const { return *before; }

    // Shows `change` applied to the starting project. When `differs` is false
    // the start is shown again and ending the gesture records nothing.
    void preview(const std::function<void(Project&)>& change, bool differs = true) {
        if (!active()) { return; }
        auto updated = *before;
        if (differs) { change(updated); }
        show(std::move(updated), differs);
    }
    // Shows a project the caller made from start(); `differs` says whether
    // ending the gesture should record it.
    void show(Project updated, bool differs = true) {
        if (!active()) { return; }
        document.preview(std::move(updated));
        revision = document.revision();
        changed = differs;
    }
    // True when the gesture became an undo step.
    bool commit(const juce::String& label) {
        const auto record = active() && changed && !stale();
        if (record) { document.commit(label, std::move(*before)); }
        reset();
        return record;
    }
    void cancel() {
        if (active() && changed && !stale()) { document.preview(std::move(*before)); }
        reset();
    }
    void reset() {
        before.reset();
        changed = false;
    }

private:
    Document& document;
    std::optional<Project> before;
    std::uint64_t revision = 0;
    bool changed = false;
};
}
