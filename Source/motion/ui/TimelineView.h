#pragma once

#include "../MotionProcessor.h"
#include <optional>
#include <limits>

class MotionTimelineView : public juce::Component, public juce::DragAndDropTarget, public juce::SettableTooltipClient {
public:
    explicit MotionTimelineView(MotionProcessor& processor) : processor(processor) {
        setName("Composition timeline");
        setWantsKeyboardFocus(true);
        setTooltip("V: Move / trim. S: Slip content. R: Stretch duration. Alt: disable snapping. Command/Ctrl + wheel: zoom. F: fit project. Escape: cancel edit.");
    }
    std::function<void(motion::Id)> onSelection;
    std::function<void(motion::Id, motion::Id)> onEffectAdded;
    motion::Id selected = 0;
    double pixelsPerSecond = 70;
    double scrollTime = 0;
    int scrollRows = 0;
    static constexpr int namesWidth = 170;
    static constexpr int rulerHeight = 26;
    static constexpr int rowHeight = 40;

    bool isInterestedInDragSource(const SourceDetails& details) override {
        const auto description = details.description.toString();
        return description.startsWith("motion-asset:") || description.startsWith("motion-effect:");
    }

    void itemDragEnter(const SourceDetails& details) override { itemDragMove(details); }
    void itemDragMove(const SourceDetails& details) override {
        dropPosition = details.localPosition;
        dropEffect = details.description.toString().startsWith("motion-effect:") ? details.description.toString().fromFirstOccurrenceOf(":", false, false).toStdString() : std::string();
        dropAssetId = static_cast<motion::Id>(details.description.toString().fromFirstOccurrenceOf(":", false, false).getLargeIntValue());
        repaint();
    }
    void itemDragExit(const SourceDetails&) override {
        dropPosition.reset();
        repaint();
    }

    void itemDropped(const SourceDetails& details) override {
        dropPosition.reset();
        if (details.description.toString().startsWith("motion-effect:")) {
            insertEffect(details.description.toString().fromFirstOccurrenceOf(":", false, false).toStdString(), details.localPosition);
            repaint();
            return;
        }
        insertAsset(static_cast<motion::Id>(details.description.toString().fromFirstOccurrenceOf(":", false, false).getLargeIntValue()), details.localPosition.x, details.localPosition.y);
        repaint();
    }

    void insertAsset(motion::Id assetId, int x, int y) {
        const auto& project = processor.document.project();
        const auto asset = std::find_if(project.assets.begin(), project.assets.end(), [assetId](const auto& item) { return item->id == assetId; });
        if (asset == project.assets.end()) {
            return;
        }
        const auto time = x < 0 ? processor.position.load() : std::max(0.0, scrollTime + (x - namesWidth) / pixelsPerSecond);
        const auto snapped = std::round(time * project.frameRate) / project.frameRate;
        const auto row = y < rulerHeight ? -1 : (y - rulerHeight) / rowHeight + scrollRows;
        auto clip = motion::Document::makeClip(processor.document.newId(), **asset, snapped);
        const auto id = clip.id;
        if (row >= 0 && row < static_cast<int>(project.tracks.size()) && !project.tracks[row].canPlace(clip)) {
            return;
        }
        const auto trackId = processor.document.newId();
        processor.document.edit("Add object clip", [&](motion::Project& updated) {
            updated.duration = std::max(updated.duration, clip.end());
            if (row >= 0 && row < static_cast<int>(updated.tracks.size())) {
                updated.tracks[row].insert(std::move(clip));
            } else {
                motion::Track track;
                track.id = trackId;
                track.name = clip.name;
                track.insert(std::move(clip));
                updated.tracks.push_back(std::move(track));
            }
        });
        selected = id;
        if (onSelection) {
            onSelection(id);
        }
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(osci::Colours::veryDark());
        auto area = getLocalBounds();
        g.setColour(osci::Colours::dark());
        g.fillRect(area.removeFromTop(rulerHeight));
        g.setFont(12.0f);
        pixelsPerSecond = std::isfinite(pixelsPerSecond) ? std::clamp(pixelsPerSecond, 0.000001, 500.0) : 70.0;
        scrollTime = std::isfinite(scrollTime) ? std::max(0.0, scrollTime) : 0.0;
        const auto visibleSeconds = std::max(0, getWidth() - namesWidth) / pixelsPerSecond;
        const auto rawStep = 70.0 / pixelsPerSecond;
        const auto magnitude = std::pow(10.0, std::floor(std::log10(rawStep)));
        const auto normalizedStep = rawStep / magnitude;
        const double step = magnitude * (normalizedStep <= 1 ? 1 : (normalizedStep <= 2 ? 2 : (normalizedStep <= 5 ? 5 : 10)));
        const auto decimals = std::max(0, static_cast<int>(std::ceil(-std::log10(step))));
        const auto firstTick = std::floor(scrollTime / step) * step;
        const auto tickCount = std::clamp(static_cast<int>(std::ceil(visibleSeconds / step)) + 2, 0, 1000);
        for (int tick = 0; tick < tickCount; ++tick) {
            const auto time = firstTick + tick * step;
            const auto x = timeX(time);
            if (x < namesWidth) {
                continue;
            }
            g.setColour(juce::Colours::white.withAlpha(0.06f));
            g.drawVerticalLine(x, rulerHeight, static_cast<float>(getHeight()));
            g.setColour(osci::Colours::text().withAlpha(0.7f));
            g.drawText(juce::String(time, decimals) + "s", x + 5, 0, 50, rulerHeight, juce::Justification::centredLeft);
        }
        g.setColour(osci::Colours::dark());
        g.fillRect(0, 0, namesWidth, rulerHeight);
        g.setColour(osci::Colours::text());
        g.drawText(toolName(), 12, 0, namesWidth - 34, rulerHeight, juce::Justification::centredLeft);
        juce::Path toolArrow;
        toolArrow.addTriangle(namesWidth - 21.0f, 11.0f, namesWidth - 13.0f, 11.0f, namesWidth - 17.0f, 15.0f);
        g.fillPath(toolArrow);
        const auto& tracks = processor.document.project().tracks;
        scrollRows = std::clamp(scrollRows, 0, std::max(0, static_cast<int>(tracks.size()) - 1));
        for (int index = scrollRows; index < static_cast<int>(tracks.size()); ++index) {
            const auto y = rowY(index);
            if (y >= getHeight()) {
                break;
            }
            g.setColour(osci::Colours::dark());
            g.fillRect(0, y, namesWidth - 1, rowHeight - 1);
            g.setColour(osci::Colours::text());
            g.drawText(juce::String(tracks[index].name), 12, y, namesWidth - 22, rowHeight, juce::Justification::centredLeft);
            juce::Graphics::ScopedSaveState scope(g);
            g.reduceClipRegion(namesWidth, y, getWidth() - namesWidth, rowHeight);
            for (const auto& clip : tracks[index].clips) {
                const auto bounds = clipBounds(clip, index).toFloat().reduced(1, 4);
                const auto active = clip.id == selected;
                g.setColour(active ? juce::Colour(0xff347b52) : juce::Colour(0xff354c45));
                g.fillRoundedRectangle(bounds, 4);
                g.setColour(active ? juce::Colour(0xff70da91) : juce::Colour(0xff647d71));
                g.drawRoundedRectangle(bounds, 4, 1);
                g.setColour(juce::Colours::white);
                if (!clip.effects.empty() && bounds.getWidth() > 90) {
                    g.setColour(juce::Colours::white.withAlpha(0.7f));
                    g.drawText(juce::String(static_cast<int>(clip.effects.size())) + " fx", bounds.withLeft(bounds.getRight() - 38), juce::Justification::centred);
                    g.setColour(juce::Colours::white);
                }
                auto label = juce::String(clip.name);
                if (active && tool == Tool::slip) {
                    label += "  offset " + juce::String(clip.offset, 2) + "s";
                } else if (active && tool == Tool::stretch) {
                    label += "  " + juce::String(clip.rate, 2) + "x";
                }
                const auto labelBounds = bounds.reduced(8, 0).withTrimmedRight(!clip.effects.empty() && bounds.getWidth() > 90 ? 32.0f : 0.0f);
                g.drawText(label, labelBounds, juce::Justification::centredLeft);
            }
        }
        if (dropPosition.has_value() && !dropEffect.empty()) {
            int row = 0;
            const auto* clip = clipAt(*dropPosition, row);
            if (dropPosition->x < namesWidth && dropPosition->y >= rulerHeight) { row = (dropPosition->y - rulerHeight) / rowHeight + scrollRows; }
            if (row >= 0 && row < static_cast<int>(tracks.size()) && (clip != nullptr || dropPosition->x < namesWidth)) {
                const auto bounds = clip != nullptr ? clipBounds(*clip, row) : juce::Rectangle<int>(0, rowY(row), namesWidth, rowHeight);
                g.setColour(osci::Colours::accentColor());
                g.drawRoundedRectangle(bounds.toFloat().reduced(2), 4, 2);
            }
        } else if (dropPosition.has_value()) {
            const auto row = std::clamp((dropPosition->y - rulerHeight) / rowHeight + scrollRows, 0, static_cast<int>(tracks.size()));
            const auto time = std::max(0.0, scrollTime + (dropPosition->x - namesWidth) / pixelsPerSecond);
            motion::Clip candidate;
            candidate.id = std::numeric_limits<motion::Id>::max();
            candidate.start = std::round(time * processor.document.project().frameRate) / processor.document.project().frameRate;
            const auto& assets = processor.document.project().assets;
            const auto asset = std::find_if(assets.begin(), assets.end(), [&](const auto& item) { return item->id == dropAssetId; });
            if (asset != assets.end()) {
                candidate = motion::Document::makeClip(candidate.id, **asset, candidate.start);
            }
            const auto allowed = asset != assets.end() && (row >= static_cast<int>(tracks.size()) || tracks[row].canPlace(candidate));
            const auto bounds = clipBounds(candidate, row).toFloat().reduced(1, 4);
            juce::Graphics::ScopedSaveState scope(g);
            g.reduceClipRegion(namesWidth, rulerHeight, getWidth() - namesWidth, getHeight() - rulerHeight);
            g.setColour((allowed ? juce::Colour(0xff70da91) : juce::Colour(0xffe98080)).withAlpha(0.2f));
            g.fillRoundedRectangle(bounds, 4);
            g.setColour(allowed ? juce::Colour(0xff70da91) : juce::Colour(0xffe98080));
            g.drawRoundedRectangle(bounds, 4, 1);
            g.drawText(allowed ? "Add object" : "Clips cannot overlap", bounds.reduced(8, 0), juce::Justification::centredLeft);
        }
        const auto playhead = timeX(processor.position.load());
        if (playhead >= namesWidth && playhead <= getWidth()) {
            g.setColour(juce::Colour(0xff7de5a0));
            g.drawVerticalLine(playhead, 0, static_cast<float>(getHeight()));
            juce::Path head;
            head.addTriangle(playhead - 5, 0, playhead + 5, 0, playhead, 8);
            g.fillPath(head);
        }
        if (tracks.empty()) {
            g.setColour(osci::Colours::text().withAlpha(0.55f));
            g.drawText("Import an object to start your composition", getLocalBounds().withTrimmedTop(rulerHeight), juce::Justification::centred);
        }
    }

    void mouseDown(const juce::MouseEvent& event) override {
        grabKeyboardFocus();
        cancelGesture();
        if (event.y < rulerHeight && event.x < namesWidth) {
            showToolMenu(true);
            return;
        }
        if (event.mods.isPopupMenu()) {
            int row = 0;
            const auto* clip = clipAt(event.getPosition(), row);
            if (clip != nullptr) {
                selectClip(clip->id);
            }
            showToolMenu();
            return;
        }
        if (!event.mods.isLeftButtonDown()) {
            return;
        }
        if (event.y < rulerHeight && event.x >= namesWidth) {
            scrubbing = true;
            seek(event.x);
            return;
        }
        int row = 0;
        const auto* clip = clipAt(event.getPosition(), row);
        if (clip == nullptr) {
            return;
        }
        original = *clip;
        originalRow = row;
        downX = event.x;
        const auto bounds = clipBounds(original, row);
        mode = tool == Tool::slip ? Mode::slip : (tool == Tool::stretch ? Mode::stretch
            : (event.x < bounds.getX() + 8 ? Mode::left : (event.x > bounds.getRight() - 8 ? Mode::right : Mode::move)));
        before = processor.document.project();
        expectedRevision = processor.document.revision();
        changed = false;
        selectClip(original.id);
    }

    void mouseMove(const juce::MouseEvent& event) override {
        int row = 0;
        const auto* clip = clipAt(event.getPosition(), row);
        if (event.y < rulerHeight && event.x < namesWidth) {
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
        } else if (clip != nullptr) {
            const auto bounds = clipBounds(*clip, row);
            const bool edge = event.x < bounds.getX() + 8 || event.x > bounds.getRight() - 8;
            setMouseCursor(tool != Tool::move || edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor);
        } else {
            setMouseCursor(juce::MouseCursor::NormalCursor);
        }
    }

    void mouseDrag(const juce::MouseEvent& event) override {
        if (scrubbing) {
            seek(event.x);
            return;
        }
        if (!gestureIsCurrent()) {
            return;
        }
        auto candidate = original;
        auto delta = (event.x - downX) / pixelsPerSecond;
        if (!event.mods.isAltDown()) {
            delta = std::round(delta * before->frameRate) / before->frameRate;
        }
        if (delta != 0.0) {
            if (mode == Mode::move) {
                candidate.start = std::max(0.0, original.start + delta);
            } else if (mode == Mode::left) {
                if (!candidate.trim(std::max(0.0, original.start + delta), original.end())) {
                    return;
                }
            } else if (mode == Mode::right) {
                if (!candidate.trim(original.start, original.end() + delta)) {
                    return;
                }
            } else if (mode == Mode::slip) {
                // Offset is content time; preserve placement, duration and speed.
                candidate.offset = original.offset + delta * original.rate;
                if (!std::isfinite(candidate.offset)) {
                    return;
                }
            } else if (!candidate.stretch(original.duration + delta)) {
                return;
            }
        }
        const auto target = mode == Mode::move
            ? std::clamp((event.y - rulerHeight) / rowHeight + scrollRows, 0, static_cast<int>(before->tracks.size()) - 1) : originalRow;
        const bool candidateChanged = candidate.start != original.start || candidate.duration != original.duration
            || candidate.offset != original.offset || candidate.rate != original.rate || target != originalRow;
        if (!candidateChanged) {
            if (changed) {
                processor.document.preview(*before);
                expectedRevision = processor.document.revision();
                changed = false;
                repaint();
            }
            return;
        }
        auto updated = *before;
        auto& source = updated.tracks[originalRow].clips;
        source.erase(std::remove_if(source.begin(), source.end(), [&](const auto& clip) { return clip.id == original.id; }), source.end());
        if (updated.tracks[target].insert(candidate)) {
            updated.duration = std::max(updated.duration, candidate.end());
            changed = true;
            processor.document.preview(std::move(updated));
            expectedRevision = processor.document.revision();
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent&) override {
        scrubbing = false;
        if (!gestureIsCurrent()) {
            return;
        }
        auto originalProject = std::move(*before);
        before.reset();
        if (changed) {
            const auto label = mode == Mode::move ? "Move clip" : (mode == Mode::slip ? "Slip clip" : (mode == Mode::stretch ? "Stretch clip" : "Trim clip"));
            processor.document.commit(label, std::move(originalProject));
        }
        changed = false;
    }

    void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override {
        if (before.has_value() || scrubbing) {
            return;
        }
        if (event.mods.isCommandDown() || event.mods.isCtrlDown()) {
            const auto anchorX = std::clamp(event.x - namesWidth, 0, std::max(0, getWidth() - namesWidth));
            const auto anchorTime = scrollTime + anchorX / pixelsPerSecond;
            pixelsPerSecond = std::clamp(pixelsPerSecond * std::exp(wheel.deltaY * 3), 0.000001, 500.0);
            scrollTime = std::max(0.0, anchorTime - anchorX / pixelsPerSecond);
        } else if (event.mods.isShiftDown() || std::abs(wheel.deltaX) > std::abs(wheel.deltaY)) {
            scrollTime = std::max(0.0, scrollTime - (wheel.deltaX + wheel.deltaY) * 8);
        } else {
            const auto last = std::max(0, static_cast<int>(processor.document.project().tracks.size()) - 1);
            scrollRows = std::clamp(scrollRows + (wheel.deltaY < 0 ? 1 : -1), 0, last);
        }
        repaint();
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::escapeKey && (before.has_value() || scrubbing)) {
            cancelGesture();
            return true;
        }
        if (!key.getModifiers().isCommandDown() && !key.getModifiers().isCtrlDown() && !key.getModifiers().isAltDown()) {
            const auto character = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
            if (character == 'v' || character == 's' || character == 'r') {
                cancelGesture();
                tool = character == 'v' ? Tool::move : (character == 's' ? Tool::slip : Tool::stretch);
                repaint();
                return true;
            }
            if (character == 'f') {
                fitProject();
                return true;
            }
        }
        if (key == juce::KeyPress::spaceKey) {
            processor.playing.store(!processor.playing.load());
            return true;
        }
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
            cancelGesture();
            const auto& tracks = processor.document.project().tracks;
            const auto exists = std::any_of(tracks.begin(), tracks.end(), [&](const auto& track) {
                return std::any_of(track.clips.begin(), track.clips.end(), [&](const auto& clip) { return clip.id == selected; });
            });
            if (!exists) {
                return false;
            }
            processor.document.edit("Delete clip", [&](motion::Project& project) {
                for (auto& track : project.tracks) {
                    std::erase_if(track.clips, [&](const auto& clip) { return clip.id == selected; });
                }
            });
            selected = 0;
            if (onSelection) {
                onSelection(0);
            }
            return true;
        }
        return false;
    }

private:
    void insertEffect(const std::string& type, juce::Point<int> position) {
        const auto* definition = motion::effectDefinition(type);
        if (definition == nullptr || position.y < rulerHeight) { return; }
        int row = 0;
        const auto* clip = clipAt(position, row);
        const auto& tracks = processor.document.project().tracks;
        if (position.x < namesWidth) { row = (position.y - rulerHeight) / rowHeight + scrollRows; }
        if (row < 0 || row >= static_cast<int>(tracks.size()) || (clip == nullptr && position.x >= namesWidth)) { return; }
        const auto owner = clip != nullptr ? clip->id : tracks[row].id;
        const auto* effects = motion::findEffectOwner(processor.document.project(), owner);
        if (effects == nullptr || effects->size() >= motion::maximumEffectsPerOwner) { return; }
        if (clip != nullptr) { selectClip(clip->id); }
        const auto effect = motion::makeEffect(processor.document.newId(), *definition);
        processor.document.edit("Add " + juce::String(definition->name), [owner, effect](motion::Project& project) {
            auto* destination = motion::findEffectOwner(project, owner);
            if (destination != nullptr) { destination->push_back(effect); }
        });
        if (onEffectAdded) { onEffectAdded(owner, effect.id); }
    }
    enum class Tool { move, slip, stretch };
    enum class Mode { move, left, right, slip, stretch };

    juce::String toolName() const {
        return tool == Tool::move ? "Move / Trim (V)" : (tool == Tool::slip ? "Slip (S)" : "Stretch (R)");
    }

    void showToolMenu(bool atHeader = false) {
        juce::PopupMenu menu;
        menu.addSectionHeader("Clip editing tool");
        menu.addItem(1, "Move / trim edges (V)", true, tool == Tool::move);
        menu.addItem(2, "Slip content inside clip (S)", true, tool == Tool::slip);
        menu.addItem(3, "Stretch duration and speed (R)", true, tool == Tool::stretch);
        menu.addSeparator();
        menu.addItem(4, "Fit project (F)");
        menu.addSectionHeader("Alt: disable snapping   Escape: cancel");
        const juce::Component::SafePointer<MotionTimelineView> owner(this);
        const auto options = juce::PopupMenu::Options().withTargetComponent(this);
        const auto placement = atHeader ? options.withTargetScreenArea(localAreaToGlobal(juce::Rectangle<int>(0, 0, namesWidth, rulerHeight))) : options.withMousePosition();
        menu.showMenuAsync(placement, [owner](int result) {
            if (owner == nullptr || result == 0) {
                return;
            }
            owner->cancelGesture();
            if (result == 4) {
                owner->fitProject();
            } else {
                owner->tool = result == 1 ? Tool::move : (result == 2 ? Tool::slip : Tool::stretch);
                owner->repaint();
            }
        });
    }

    void fitProject() {
        cancelGesture();
        const auto& project = processor.document.project();
        auto end = project.duration;
        for (const auto& track : project.tracks) {
            for (const auto& clip : track.clips) {
                end = std::max(end, clip.end());
            }
        }
        const auto duration = std::isfinite(end) ? std::max(0.001, end) : 1.0;
        pixelsPerSecond = std::clamp(std::max(1, getWidth() - namesWidth - 20) / duration, 0.000001, 500.0);
        scrollTime = 0.0;
        scrollRows = 0;
        repaint();
    }

    bool gestureIsCurrent() {
        if (!before.has_value()) {
            return false;
        }
        if (processor.document.revision() != expectedRevision) {
            before.reset();
            changed = false;
            repaint();
            return false;
        }
        return true;
    }

    void cancelGesture() {
        scrubbing = false;
        if (gestureIsCurrent()) {
            auto restore = std::move(*before);
            before.reset();
            if (changed) {
                processor.document.preview(std::move(restore));
            }
        }
        changed = false;
        repaint();
    }

    void selectClip(motion::Id id) {
        selected = id;
        if (onSelection) {
            onSelection(id);
        }
        repaint();
    }

    const motion::Clip* clipAt(juce::Point<int> position, int& row) const {
        if (position.x < namesWidth || position.y < rulerHeight) {
            return nullptr;
        }
        row = (position.y - rulerHeight) / rowHeight + scrollRows;
        const auto& tracks = processor.document.project().tracks;
        if (row < 0 || row >= static_cast<int>(tracks.size())) {
            return nullptr;
        }
        for (const auto& clip : tracks[row].clips) {
            if (clipBounds(clip, row).contains(position)) {
                return &clip;
            }
        }
        return nullptr;
    }
    static int boundedPixel(double value) {
        constexpr double limit = 1000000.0;
        if (!std::isfinite(value)) {
            return std::signbit(value) ? -static_cast<int>(limit) : static_cast<int>(limit);
        }
        return juce::roundToInt(std::clamp(value, -limit, limit));
    }
    int timeX(double time) const { return namesWidth + boundedPixel((time - scrollTime) * pixelsPerSecond); }
    int rowY(int row) const { return rulerHeight + (row - scrollRows) * rowHeight; }
    juce::Rectangle<int> clipBounds(const motion::Clip& clip, int row) const {
        return { timeX(clip.start), rowY(row), std::max(2, boundedPixel(clip.duration * pixelsPerSecond)), rowHeight };
    }
    void seek(int x) {
        processor.seek(std::clamp(scrollTime + (x - namesWidth) / pixelsPerSecond, 0.0, processor.document.project().duration));
        repaint();
    }
    MotionProcessor& processor;
    std::optional<motion::Project> before;
    motion::Clip original;
    int originalRow = 0;
    int downX = 0;
    Tool tool = Tool::move;
    Mode mode = Mode::move;
    std::uint64_t expectedRevision = 0;
    bool scrubbing = false;
    bool changed = false;
    std::optional<juce::Point<int>> dropPosition;
    motion::Id dropAssetId = 0;
    std::string dropEffect;
};
