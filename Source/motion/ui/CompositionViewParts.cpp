#include "CompositionView.h"
#include "../model/SourceParts.h"

// Picking parts of an object in the Scene, to extract them as one of their own.

std::vector<motion::Id> MotionCompositionView::clipsIn(juce::Rectangle<float> area) const {
    std::vector<motion::Id> ids;
    for (const auto& piece : screenPieces()) {
        const auto id = piece.clip->editorId();
        if (std::find(ids.begin(), ids.end(), id) == ids.end() && area.intersects(piece.line)) { ids.push_back(id); }
    }
    return ids;
}

const motion::PreparedDrawing* MotionCompositionView::partDrawing(const motion::PreparedClip& clip) const {
    if (!clip.ancestors.empty() || clip.liveIdentity != nullptr || clip.source == nullptr || clip.source->frameCount() != 1) { return nullptr; }
    return clip.source->drawingAt(0);
}

juce::String MotionCompositionView::partBlocker(motion::Id id) const {
    const auto& project = processor.document.project();
    const auto* clip = motion::findClip(project, id);
    if (clip == nullptr || clip->composition != 0) { return "Compositions are picked whole"; }
    const auto asset = motion::findAsset(project.assets, clip->asset);
    return asset != nullptr ? motion::parts::unavailableReason(*asset) : juce::String();
}

std::optional<MotionCompositionView::PartHit> MotionCompositionView::pickPart(juce::Point<float> position) const {
    std::optional<PartHit> hit;
    float nearest = 8;
    for (const auto& piece : screenPieces()) {
        if (piece.shape < 0 || partDrawing(*piece.clip) == nullptr) { continue; }
        juce::Point<float> closest;
        const auto distance = piece.line.getDistanceFromPoint(position, closest);
        if (distance < nearest) {
            nearest = distance;
            hit = PartHit {piece.clip->editorId(), static_cast<std::size_t>(piece.shape)};
        }
    }
    return hit;
}

// A shape is picked when the area crosses any of it on screen; picking
// pieces takes every piece the area crosses.
std::map<motion::Id, MotionCompositionView::Picked> MotionCompositionView::partsIn(const juce::Path& area, bool touching) const {
    std::map<motion::Id, Picked> found;
    if (prepared == nullptr) { return found; }
    const auto bounds = area.getBounds();
    juce::Path box;
    box.addRectangle(bounds);
    const auto rectangular = area == box;
    std::map<const motion::PreparedClip*, std::vector<signed char>> inside;
    for (const auto& piece : screenPieces()) {
        const auto* drawing = piece.shape >= 0 ? partDrawing(*piece.clip) : nullptr;
        if (drawing == nullptr) { continue; }
        auto& states = inside[piece.clip];
        states.resize(drawing->shapeCount(), -1);
        // Numbered by the source's shapes: a lit drawing's (outside part mode) are numbered as it cut them.
        if (static_cast<std::size_t>(piece.shape) >= states.size()) { continue; }
        auto& state = states[static_cast<std::size_t>(piece.shape)];
        // Anything the area crosses is picked, as the object marquee picks.
        const auto crossed = rectangular ? bounds.intersects(piece.line)
            : area.contains(piece.line.getStart()) || area.contains(piece.line.getEnd()) || area.contains(piece.line.getPointAlongLineProportionally(.5f));
        state = static_cast<signed char>(state == 1 || crossed ? 1 : 0);
    }
    for (const auto& [clip, states] : inside) {
        const auto paths = touching ? motion::parts::pathIndices(*partDrawing(*clip)) : std::vector<std::size_t>();
        std::set<std::size_t> touchedPaths;
        for (std::size_t shape = 0; shape < states.size(); ++shape) {
            if (states[shape] == 1 && touching) { touchedPaths.insert(paths[shape]); }
        }
        for (std::size_t shape = 0; shape < states.size(); ++shape) {
            if (touching ? !touchedPaths.contains(paths[shape]) : states[shape] != 1) { continue; }
            auto& entry = found[clip->editorId()];
            entry.source = clip->source;
            entry.shapes.insert(shape);
        }
    }
    return found;
}

void MotionCompositionView::pickParts(const juce::Path& area, bool add, bool touching) {
    if (prepared == nullptr) { return; }
    if (!add) { picked.clear(); }
    for (const auto& [id, entry] : partsIn(area, touching)) {
        auto& merged = picked[id];
        merged.source = entry.source;
        merged.shapes.insert(entry.shapes.begin(), entry.shapes.end());
    }
    refreshPartBar();
    repaint();
}

// The area being dragged, as it would pick: a box, or the lasso closed.
juce::Path MotionCompositionView::marqueeRegion() const {
    juce::Path region;
    if (partMode && partPick == PartPick::lasso) {
        region = lasso;
        region.closeSubPath();
    } else if (marquee.has_value()) {
        region.addRectangle(*marquee);
    }
    return region;
}

// Every shape of every object whose parts can be picked, on screen or not.
void MotionCompositionView::pickAllParts() {
    if (prepared == nullptr) { return; }
    picked.clear();
    const auto time = editingTime();
    for (const auto& clip : prepared->clips) {
        const auto* drawing = partDrawing(clip);
        if (drawing == nullptr || !clip.active(clipTime(clip, time))) { continue; }
        for (std::size_t shape = 0; shape < drawing->shapeCount(); ++shape) {
            auto* item = drawing->shape(shape);
            if (item == nullptr || !(item->length() > 0)) { continue; }
            auto& entry = picked[clip.editorId()];
            entry.source = clip.source;
            entry.shapes.insert(shape);
        }
    }
    refreshPartBar();
    repaint();
}

// What a click on `part` picks: the shape, or with pieces its whole piece.
std::set<std::size_t> MotionCompositionView::pickedBy(PartHit part) const {
    std::set<std::size_t> shapes {part.shape};
    if (partPick != PartPick::pieces || prepared == nullptr) { return shapes; }
    const auto clip = std::find_if(prepared->clips.begin(), prepared->clips.end(), [&](const auto& item) { return item.editorId() == part.clip && partDrawing(item) != nullptr; });
    if (clip == prepared->clips.end()) { return shapes; }
    const auto paths = motion::parts::pathIndices(*partDrawing(*clip));
    for (std::size_t shape = 0; shape < paths.size(); ++shape) {
        if (part.shape < paths.size() && paths[shape] == paths[part.shape]) { shapes.insert(shape); }
    }
    return shapes;
}

// A click picks one shape, or with pieces its whole piece; Shift toggles.
void MotionCompositionView::clickPart(PartHit part, bool adding) {
    const auto clip = std::find_if(prepared->clips.begin(), prepared->clips.end(), [&](const auto& item) { return item.editorId() == part.clip && partDrawing(item) != nullptr; });
    if (clip == prepared->clips.end()) { return; }
    if (part.shape >= partDrawing(*clip)->shapeCount()) { return; }
    const auto shapes = pickedBy(part);
    const auto existing = picked.find(part.clip);
    const auto already = existing != picked.end() && existing->second.shapes.contains(part.shape);
    if (adding && already) {
        for (const auto shape : shapes) { existing->second.shapes.erase(shape); }
        if (existing->second.shapes.empty()) { picked.erase(existing); }
    } else {
        if (!adding && !already) { picked.clear(); }
        auto& entry = picked[part.clip];
        entry.source = clip->source;
        entry.shapes.insert(shapes.begin(), shapes.end());
    }
    refreshPartBar();
}

// The whole path the shape is part of: a letter's outline, a polyline.
void MotionCompositionView::pickPath(PartHit hit, bool add) {
    const auto found = std::find_if(prepared->clips.begin(), prepared->clips.end(), [&](const auto& clip) { return clip.editorId() == hit.clip && partDrawing(clip) != nullptr; });
    if (found == prepared->clips.end()) { return; }
    const auto paths = motion::parts::pathIndices(*partDrawing(*found));
    if (hit.shape >= paths.size()) { return; }
    if (!add) { picked.clear(); }
    auto& entry = picked[hit.clip];
    entry.source = found->source;
    for (std::size_t shape = 0; shape < paths.size(); ++shape) {
        if (paths[shape] == paths[hit.shape]) { entry.shapes.insert(shape); }
    }
    refreshPartBar();
    repaint();
}

// Picks follow their source: a changed or removed source drops them.
void MotionCompositionView::prunePicked() {
    for (auto entry = picked.begin(); entry != picked.end();) {
        const auto still = prepared != nullptr && std::any_of(prepared->clips.begin(), prepared->clips.end(), [&](const auto& clip) {
            return clip.editorId() == entry->first && clip.source == entry->second.source && partDrawing(clip) != nullptr;
        });
        entry = still ? std::next(entry) : picked.erase(entry);
    }
    hoverPart.reset();
    refreshPartBar();
}

std::size_t MotionCompositionView::pickedPieceCount() const {
    std::size_t count = 0;
    if (prepared == nullptr) { return count; }
    for (const auto& [id, entry] : picked) {
        const auto clip = std::find_if(prepared->clips.begin(), prepared->clips.end(), [id = id](const auto& item) { return item.editorId() == id && item.ancestors.empty(); });
        const auto* drawing = clip != prepared->clips.end() ? partDrawing(*clip) : nullptr;
        if (drawing == nullptr) { continue; }
        const auto paths = motion::parts::pathIndices(*drawing);
        std::set<std::size_t> touched;
        for (const auto shape : entry.shapes) {
            if (shape < paths.size()) { touched.insert(paths[shape]); }
        }
        count += touched.size();
    }
    return count;
}

std::size_t MotionCompositionView::pickedPartCount() const {
    std::size_t count = 0;
    for (const auto& [id, entry] : picked) { count += entry.shapes.size(); }
    return count;
}

void MotionCompositionView::setPartMode(bool enabled) {
    if (partMode == enabled) { return; }
    if (enabled && !anythingPickable()) {
        if (onStatus) { onStatus("Text, SVG, OBJ and drawings at the playhead have parts to pick"); }
        // The tool strip shows the mode as it is.
        if (onPartModeChanged) { onPartModeChanged(false); }
        return;
    }
    setNavigating(false);
    cancelGesture();
    partMode = enabled;
    resetMarquee();
    setMouseCursor(enabled ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    hoverPart.reset();
    hoverShapes.clear();
    hoverHandle = -1;
    if (!enabled) { picked.clear(); }
    refreshPartBar();
    if (onPartModeChanged) { onPartModeChanged(enabled); }
    repaint();
}

void MotionCompositionView::extractPicked() {
    if (picked.empty()) {
        if (onStatus) { onStatus("Pick some parts first"); }
        return;
    }
    if (onExtractParts) { onExtractParts(picked); }
}

bool MotionCompositionView::anythingPickable() const {
    if (prepared == nullptr) { return false; }
    const auto time = editingTime();
    return std::any_of(prepared->clips.begin(), prepared->clips.end(), [&](const auto& clip) { return clip.active(clipTime(clip, time)) && partDrawing(clip) != nullptr; });
}

void MotionCompositionView::resetMarquee() {
    marqueeArmed = false;
    marquee.reset();
    marqueeParts.clear();
    marqueeClips.clear();
    lasso.clear();
    pressedPart.reset();
}

void MotionCompositionView::refreshPartBar() {
    const auto count = pickedPartCount();
    const auto shown = partMode && count > 0;
    // Picking pieces counts pieces, as they were clicked; a box or lasso
    // picks lines (which may be part of a piece).
    const auto pieces = partPick == PartPick::pieces;
    const auto shownCount = pieces ? pickedPieceCount() : count;
    partCount.setText(juce::String(static_cast<juce::uint64>(shownCount)) + (pieces ? (shownCount == 1 ? " piece" : " pieces") : (shownCount == 1 ? " line" : " lines")), juce::dontSendNotification);
    partCount.setVisible(shown);
    extractButton.setVisible(shown);
    resized();
    repaint();
}

// The part bar: the count, then Extract, 4 px inside a floating panel
// 12 px above the Scene's foot.
juce::Rectangle<int> MotionCompositionView::partBar() const {
    constexpr auto inset = motion::icons::ToolStrip::inset, cell = motion::icons::ToolStrip::cell;
    const auto labelWidth = juce::roundToInt(std::ceil(juce::TextLayout::getStringWidth(motion::style::body(), partCount.getText())));
    const auto width = inset + barTextInset + labelWidth + barTextInset + extractWidth + inset;
    return {(getWidth() - width) / 2, getHeight() - 8 - (cell + 2 * inset), width, cell + 2 * inset};
}
