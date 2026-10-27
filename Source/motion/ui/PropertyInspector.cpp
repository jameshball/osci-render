#include "PropertyInspector.h"
#include "SourceKind.h"
#include "../model/KeyEdit.h"
#include "DocumentMenu.h"
#include "../model/LightPresets.h"
#include "../../parser/FileFormatRegistry.h"

namespace {
// How a surface can draw itself: a tile each, turning its property on.
struct SurfaceStyle {
    std::string_view property, name, tooltip;
    motion::ui::pictures::SurfaceLook look;
};
using Look = motion::ui::pictures::SurfaceLook;
constexpr std::array<SurfaceStyle, 6> surfaceStyles {{
    {"edges", "Edges", "Its own edges, as it is made", Look::edges},
    {"outline", "Outline", "Its outline, where it turns away from the eye", Look::outline},
    {"spiral", "Spiral", "A spiral winding round it from bottom to top, or in from its edge when it is flat", Look::spiral},
    {"noise", "Noise", "Contours of noise, which can morph as it plays (Noise flow)", Look::noise},
    {"slices", "Slices", "Level slices through it", Look::slices},
    {"rings", "Rings", "Rings centred on its front", Look::rings},
}};

// The heading each property group sits under (a generator's own numbers, a
// script's sliders and an L-system's depth open the page with none; anything
// else is the Scope's Picture).
constexpr std::array<std::pair<std::string_view, std::string_view>, 23> sectionOfGroup {{
    {"Position", "Transform"}, {"Rotation", "Transform"}, {"Scale", "Transform"}, {"Anchor", "Transform"},
    {"Colour", "Appearance"}, {"Opacity", "Appearance"}, {"Lit by lights", "Appearance"}, {"Beam share", "Appearance"},
    {"Edges", "Surface"}, {"Outline", "Surface"}, {"Spiral", "Surface"}, {"Noise", "Surface"}, {"Slices", "Surface"}, {"Rings", "Surface"},
    {"Fill detail", "Surface"}, {"Noise phase", "Surface"}, {"Noise flow", "Surface"}, {"Mirror", "Surface"},
    {"Solid", "Surface"}, {"Hatching", "Shading"}, {"Hatch detail", "Shading"},
    {"Gain", "Audio"}, {"Pan", "Audio"},
}};
}

MotionPropertyInspector::MotionPropertyInspector(MotionProcessor& owner) : processor(owner) {
    setName("Property inspector");
    viewport.setViewedComponent(&content, false);
    motion::style::fadeScrolling(viewport);
    addAndMakeVisible(viewport);
    // Title and kind sit in the panel's 30 px header, on the Scene's and
    // Scope's baseline.
    title.setFont(motion::style::title());
    title.setName("Inspector title");
    title.setBorderSize({});
    kind.setBorderSize({});
    title.onTextChange = [this] {
        const auto text = title.getText().trim();
        if (onRename && text.isNotEmpty()) { onRename(target, text); }
        refresh();
    };
    kind.setFont(motion::style::caption());
    kind.setColour(juce::Label::textColourId, osci::Colours::textMuted());
    kind.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(title);
    addAndMakeVisible(kind);
}

void MotionPropertyInspector::setLead(juce::Component* component, std::function<int()> height) {
    if (component == lead) { return; }
    if (lead != nullptr) { content.removeChildComponent(lead); }
    lead = component;
    leadHeight = std::move(height);
    if (lead != nullptr) { content.addChildComponent(lead); }
    layoutContent();
}

void MotionPropertyInspector::setTrail(juce::Component* component, std::function<int()> height, juce::String title, bool fullWidth) {
    trailTitle = std::move(title);
    trailFullWidth = fullWidth;
    if (component == trail) { layoutContent(); return; }
    if (trail != nullptr) { content.removeChildComponent(trail); }
    trail = component;
    trailHeight = std::move(height);
    if (trail != nullptr) { content.addChildComponent(trail); }
    layoutContent();
}

void MotionPropertyInspector::setHeading(std::optional<std::pair<juce::String, juce::String>> value) {
    if (value == heading) { return; }
    heading = std::move(value);
    refresh();
}

void MotionPropertyInspector::reveal(juce::Component& component, osci::RevealOptions options) {
    layoutContent();
    // Marked in motion's green, not the shared accent.
    if (!options.colour.has_value()) {
        options.colour = motion::style::accent();
    }
    osci::reveal(component, std::move(options));
}

void MotionPropertyInspector::setTarget(motion::Id id) {
    if (id != target) {
        title.hideEditor(true);
        target = id;
        cancelGesture();
    }
    refresh();
}

void MotionPropertyInspector::setSelectionCount(std::size_t count) {
    if (count == selectionCount) { return; }
    selectionCount = count;
    refresh();
}

void MotionPropertyInspector::refresh() {
    const auto found = motion::findPropertyTarget(processor.document.project(), target);
    const bool editable = found.has_value() && !found->isEffect;
    if (gesture.stale()) { cancelGesture(); }
    specList.clear();
    if (editable) {
        const auto* generator = targetGenerator();
        if (generator != nullptr) { specList.assign(generator->parameters.begin(), generator->parameters.end()); }
        const auto base = motion::propertySpecs(*found);
        const auto surface = motion::hasSurface(processor.document.project(), target);
        const auto live = motion::drawsLive(processor.document.project(), target);
        const auto light = generator != nullptr && generator->category == motion::GeneratorCategory::light;
        const auto routed = routedProperties();
        // In use: away from its default, keyed, or driven.
        const auto inUse = [&](const motion::PropertySpec& spec) {
            const std::string id(spec.id);
            if (isDriven(*found, routed, id)) { return true; }
            const auto* curve = found->curve(id);
            return curve != nullptr && (curve->animated() || std::abs(curve->evaluateBase(0) - spec.defaultValue) > 1.0e-9);
        };
        // Lit by lights does nothing without a light, and a light's opacity
        // only dims it as its intensity does: each shows only when there is
        // a light, or when it is already in use. The Graph still lists them.
        // Once shown, such a row stays while the same thing is selected: a
        // drag back to the default must not take the field from under it.
        std::optional<bool> lights;
        if (keptFor != target) {
            keptRows.clear();
            keptFor = target;
        }
        const auto used = [&](const motion::PropertySpec& spec) {
            const std::string id(spec.id);
            if (keptRows.contains(id) || inUse(spec)) {
                keptRows.insert(id);
                return true;
            }
            return false;
        };
        const auto shown = [&](const motion::PropertySpec& spec) {
            if (!motion::hasProperty(generator, spec.id, surface)) { return false; }
            if (spec.id == "lit") {
                if (live) { return false; }
                if (!lights.has_value()) { lights = projectHasLight(); }
                return *lights || used(spec);
            }
            return !(light && spec.id == "opacity") || used(spec);
        };
        std::copy_if(base.begin(), base.end(), std::back_inserter(specList), shown);
        for (const auto& slider : luaSliders()) { specList.push_back(slider); }
        const auto depth = fractalDepthSpec();
        if (depth.has_value()) { specList.insert(specList.begin(), *depth); }
        // What makes it opens the page, whatever kind of source it is.
        std::stable_partition(specList.begin(), specList.end(), [this](const auto& spec) { return isMakingSpec(spec); });
    }
    const auto specs = std::span<const motion::PropertySpec>(specList);
    const auto modes = currentModes();
    motionModes = modes.has_value();
    auto signature = editable ? juce::String(static_cast<int>(found->camera)) + juce::String(static_cast<int>(found->beam)) + juce::String(static_cast<int>(found->isAudio)) + juce::String(static_cast<int>(motionModes)) : juce::String();
    // Every spec, not only how many: two Lua clips can read different sliders,
    // and each fractal has its own default depth.
    for (const auto& spec : specList) {
        signature << ":" << juce::String(spec.id.data(), spec.id.size()) << "=" << spec.minimum << "," << spec.maximum << "," << spec.defaultValue << "," << spec.step;
    }
    // Two generators with as many parameters still differ.
    if (editable) {
        const auto* generator = targetGenerator();
        if (generator != nullptr) { signature << ":" << juce::String(generator->id.data(), generator->id.size()); }
    }
    if (signature != layoutSignature) {
        layoutSignature = signature;
        build(specs);
    }
    for (auto& row : rows) {
        if (row->mode == nullptr || !modes.has_value() || (row->group != "Position" && row->group != "Rotation")) { continue; }
        const bool path = row->group == "Position";
        const bool on = path ? modes->first : modes->second;
        const std::string prefix = path ? "position." : "rotation.";
        const auto* x = found->curve(prefix + "x");
        const auto* y = found->curve(prefix + "y");
        const auto* z = found->curve(prefix + "z");
        const bool keyed = x != nullptr && y != nullptr && z != nullptr && (x->animated() || y->animated() || z->animated());
        const auto times = [](const motion::Curve& curve) {
            std::vector<double> result;
            for (const auto& key : curve.keyframes()) { result.push_back(key.time); }
            return result;
        };
        row->misaligned = on && keyed && (times(*x) != times(*y) || times(*x) != times(*z));
        row->mode->setToggleState(on, juce::dontSendNotification);
        row->mode->setOnColour(row->misaligned ? osci::Colours::warning().withAlpha(.6f) : motion::style::onFill());
        const juce::String base = path ? "One smooth path through the keyed positions, at constant speed." : "Rotate along the shortest arc, free of gimbal lock.";
        row->mode->setTooltip(row->misaligned ? "Paused: X, Y and Z no longer share key times. Click to key every axis at each key time." : base + " Keys all three axes together.");
    }
    title.setEditable(false, editable && found->camera);
    title.setTooltip(editable && found->camera ? "Double-click to rename" : juce::String());
    // With nothing to name, the panel's own name, quietly.
    const auto unnamed = !editable && !heading.has_value();
    title.setColour(juce::Label::textColourId, unnamed ? osci::Colours::textMuted() : osci::Colours::text());
    if (!title.isBeingEdited()) { title.setText(editable ? juce::String(found->name.data(), found->name.size()) : heading.has_value() ? heading->first : juce::String("Properties"), juce::dontSendNotification); }
    // With several clips selected the header says so; edits apply to the
    // one named.
    auto kindText = !editable ? (heading.has_value() ? heading->second : juce::String()) : found->beam ? juce::String() : found->camera ? "Camera" : found->isGroup ? "Group" : found->isAudio ? "Audio" : sourceKind(target);
    // Never the same word twice: a camera named Camera is just Camera.
    if (kindText.equalsIgnoreCase(title.getText().trim())) { kindText.clear(); }
    const auto shownKind = editable && selectionCount > 1 ? "Editing 1 of " + juce::String(selectionCount) : kindText;
    if (shownKind != kind.getText()) {
        kind.setText(shownKind, juce::dontSendNotification);
        layoutHeader();
    }
    // Children repaint their own changes; only the empty message is painted here.
    const auto wasEmpty = std::exchange(empty, unnamed);
    if (empty != wasEmpty) { repaint(); }
    refreshValues();
}

void MotionPropertyInspector::refreshValues() {
    const auto found = motion::findPropertyTarget(processor.document.project(), target);
    if (!found.has_value() || found->isEffect) { return; }
    const auto time = keyTime(*found);
    const auto routed = routedProperties();
    for (auto& row : rows) {
        bool allKeyed = true, anyAnimated = false, modulated = false, earlier = false, later = false;
        for (auto& field : row->fields) {
            const auto property = std::string(field->spec.id);
            const auto* curve = found->curve(property);
            const auto driven = isDriven(*found, routed, property);
            modulated = modulated || driven;
            // A driven value says so down its left edge, in the Graph's lilac.
            field->editor.setAccent(driven ? std::optional<juce::Colour>(motion::style::result()) : std::nullopt);
            // A property the project never stored shows its default.
            if (curve == nullptr) { field->editor.setValue(field->spec.defaultValue); continue; }
            field->editor.setValue(curve->evaluateBase(time));
            anyAnimated = anyAnimated || curve->animated();
            allKeyed = allKeyed && curve->hasKeyAt(time);
            // Keys are in time order: the first and last say whether any is before or after.
            const auto& keys = curve->keyframes();
            if (!keys.empty()) {
                earlier = earlier || keys.front().time < time - 1.0e-9;
                later = later || keys.back().time > time + 1.0e-9;
            }
        }
        // A locked track's values show but do not edit.
        for (auto& field : row->fields) { field->editor.setEnabled(!found->locked); }
        row->key.setEnabled(!found->locked);
        if (row->swatch != nullptr) { row->swatch->setEnabled(!found->locked); }
        if (row->mode != nullptr) { row->mode->setEnabled(!found->locked); }
        row->showState(allKeyed && anyAnimated ? motion::ui::KeyframeButton::State::keyed
            : (anyAnimated ? motion::ui::KeyframeButton::State::animated : motion::ui::KeyframeButton::State::unanimated), target);
        // Key navigation shows only once there are keys, lit towards them.
        row->previous.setVisible(anyAnimated);
        row->next.setVisible(anyAnimated);
        row->previous.setEnabled(earlier);
        row->next.setEnabled(later);
        row->modulate.setToggleState(modulated, juce::dontSendNotification);
        if (row->group == "Beam share") {
            const auto share = beamShare ? beamShare(target) : std::nullopt;
            const auto note = !share.has_value() ? juce::String() : *share > .999 ? juce::String("all of the beam")
                : *share <= 0 ? juce::String("not drawn here") : juce::String(std::max(1, juce::roundToInt(*share * 100))) + "% of the beam";
            if (note != row->note) { row->note = note; row->repaint(); }
        }
        if (row->swatch != nullptr) {
            const auto rgb = colourOf(*row);
            row->swatch->setColour(juce::Colour::fromFloatRGBA(static_cast<float>(rgb[0]), static_cast<float>(rgb[1]), static_cast<float>(rgb[2]), 1.0f));
        }
    }
    if (refreshTiles(*found, time)) { layoutContent(); }
}

void MotionPropertyInspector::setModulatorDrag(bool active) {
    modulatorDrag = active;
    // Its own rows and its effects' rows alike.
    std::function<void(juce::Component&)> visit = [&](juce::Component& parent) {
        for (auto* child : parent.getChildren()) {
            auto* row = dynamic_cast<motion::ui::PropertyRow*>(child);
            if (row != nullptr) {
                row->setModulatorDrag(active);
            } else {
                visit(*child);
            }
        }
    };
    visit(content);
    if (!active) {
        dropTarget = nullptr;
        if (previewingRoute && onRoutePreview) { onRoutePreview(std::nullopt); }
        previewingRoute = false;
    }
    repaint();
}

std::optional<MotionPropertyInspector::RouteRequest> MotionPropertyInspector::routeRequest(const SourceDetails& details, juce::Component* found) const {
    if (found == nullptr) { return std::nullopt; }
    RouteRequest request;
    request.modulator = static_cast<motion::Id>(details.description.toString().fromFirstOccurrenceOf(":", false, false).getLargeIntValue());
    const auto& properties = found->getProperties();
    request.target = properties.contains("routeTarget") ? static_cast<motion::Id>(properties["routeTarget"].toString().getLargeIntValue()) : target;
    for (const auto& name : juce::StringArray::fromTokens(properties["routeProperties"].toString(), ",", "")) { request.properties.push_back(name.toStdString()); }
    return request;
}

void MotionPropertyInspector::itemDragMove(const SourceDetails& details) {
    auto* found = routeTargetAt(details.localPosition);
    if (found != dropTarget.getComponent()) {
        dropTarget = found;
        // The route plays as it would once dropped.
        const auto request = routeRequest(details, found);
        if ((request.has_value() || previewingRoute) && onRoutePreview) { onRoutePreview(request); }
        previewingRoute = request.has_value();
        repaint();
    }
}

void MotionPropertyInspector::itemDragExit(const SourceDetails&) {
    dropTarget = nullptr;
    if (previewingRoute && onRoutePreview) { onRoutePreview(std::nullopt); }
    previewingRoute = false;
    repaint();
}

void MotionPropertyInspector::itemDropped(const SourceDetails& details) {
    const auto request = routeRequest(details, routeTargetAt(details.localPosition));
    dropTarget = nullptr;
    if (previewingRoute && onRoutePreview) { onRoutePreview(std::nullopt); }
    previewingRoute = false;
    repaint();
    if (!request.has_value() || !onRouteModulator) { return; }
    onRouteModulator(request->modulator, request->target, request->properties);
}

void MotionPropertyInspector::paintOverChildren(juce::Graphics& g) {
    if (!modulatorDrag) { return; }
    g.reduceClipRegion(viewport.getBounds());
    std::function<void(juce::Component&)> outline = [&](juce::Component& parent) {
        for (auto* child : parent.getChildren()) {
            if (!child->isVisible()) { continue; }
            if (child->getProperties().contains("routeProperties") && dynamic_cast<motion::ui::ScrubField*>(child) != nullptr) {
                const auto area = getLocalArea(child, child->getLocalBounds()).toFloat();
                g.setColour(motion::style::accent().withAlpha(child == dropTarget.getComponent() ? .95f : .35f));
                g.drawRoundedRectangle(area.reduced(.5f), motion::style::radiusControl, child == dropTarget.getComponent() ? 2.0f : 1.0f);
            }
            outline(*child);
        }
    };
    outline(content);
    auto* row = dropTarget.getComponent();
    if (row != nullptr && dynamic_cast<motion::ui::ScrubField*>(row) == nullptr) {
        g.setColour(motion::style::accent().withAlpha(.9f));
        g.drawRoundedRectangle(getLocalArea(row, row->getLocalBounds()).toFloat().expanded(2), motion::style::radiusControl, 2.0f);
    }
}

void MotionPropertyInspector::paint(juce::Graphics& g) {
    if (!empty) { return; }
    g.setColour(osci::Colours::textMuted());
    g.setFont(motion::style::body());
    g.drawFittedText("Select an object, group, audio clip or camera to edit its properties.",
        getLocalBounds().withTrimmedTop(headerHeight + 6).reduced(motion::style::padding * 2, 0).removeFromTop(60), juce::Justification::topLeft, 3);
}

void MotionPropertyInspector::resized() {
    layoutHeader();
    viewport.setBounds(getLocalBounds().withTrimmedTop(headerHeight));
    layoutContent();
}

// The kind as wide as its text; the title has the rest.
void MotionPropertyInspector::layoutHeader() {
    auto header = getLocalBounds().removeFromTop(headerHeight).reduced(motion::style::padding, 3);
    const auto kindWidth = kind.getText().isEmpty() ? 0 : juce::roundToInt(std::ceil(juce::TextLayout::getStringWidth(motion::style::caption(), kind.getText())));
    kind.setBounds(header.removeFromRight(std::min(kindWidth, header.getWidth() / 2)));
    header.removeFromRight(kindWidth > 0 ? motion::style::padding : 0);
    title.setBounds(header);
}

MotionPropertyInspector::Swatch::Swatch() : juce::Button("Colour swatch") {
    setTitle("Colour swatch");
    setWantsKeyboardFocus(false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void MotionPropertyInspector::Swatch::paintButton(juce::Graphics& g, bool highlighted, bool) {
    const auto bounds = getLocalBounds().toFloat().reduced(.5f);
    g.setColour(colour);
    g.fillRoundedRectangle(bounds, motion::style::radiusSmall);
    g.setColour(juce::Colours::white.withAlpha(highlighted ? .5f : .22f));
    g.drawRoundedRectangle(bounds, motion::style::radiusSmall, 1.0f);
}

void MotionPropertyInspector::Swatch::setColour(juce::Colour value) {
    if (value != colour) {
        colour = value;
        repaint();
    }
}

void MotionPropertyInspector::Row::paint(juce::Graphics& g) {
    g.setFont(motion::style::caption());
    g.setColour(osci::Colours::textMuted());
    const motion::style::PropertyGrid grid(getWidth());
    g.drawText(group, compact() ? getLocalBounds().withRight(grid.value - motion::style::gap) : getLocalBounds().removeFromTop(16), juce::Justification::centredLeft);
    if (compact() && note.isNotEmpty()) {
        g.setColour(osci::Colours::textMuted().withMultipliedAlpha(.8f));
        g.drawText(note, getLocalBounds().withRight(grid.value - motion::style::gap), juce::Justification::centredRight);
    }
}

void MotionPropertyInspector::Row::resized() {
    const motion::style::PropertyGrid grid(getWidth());
    auto area = getLocalBounds();
    if (!compact()) {
        auto heading = area.removeFromTop(16);
        if (swatch != nullptr) {
            // Beside the heading, as After Effects places a colour's swatch.
            const auto label = juce::roundToInt(juce::TextLayout::getStringWidth(motion::style::caption(), group));
            swatch->setBounds(heading.getX() + label + 6, heading.getY() + 2, 24, 12);
        }
        // A mode sits over the row's modulation glyph, in the same column.
        if (mode != nullptr) { mode->setBounds(juce::Rectangle<int>(grid.modulate, heading.getY(), motion::style::PropertyGrid::modulateWidth + 2, heading.getHeight()).translated(-1, 0)); }
        area.removeFromTop(1);
    }
    auto line = area.removeFromTop(motion::style::controlHeight);
    layoutGutter(line, grid);
    if (compact()) {
        fields.front()->editor.setBounds(line.withX(grid.value).withWidth(grid.column));
        return;
    }
    for (std::size_t index = 0; index < fields.size(); ++index) {
        fields[index]->editor.setBounds(line.withX(static_cast<int>(index % 3) * (grid.column + motion::style::gap)).withWidth(grid.column));
    }
}

void MotionPropertyInspector::Content::paint(juce::Graphics& g) {
    for (const auto& section : sections) {
        g.setColour(motion::style::hairline());
        g.fillRect(0, section.y, getWidth(), 1);
        auto line = juce::Rectangle<int>(motion::style::padding, section.y + sectionGap, getWidth() - motion::style::padding * 2, sectionHeading - sectionGap);
        g.setFont(motion::style::heading());
        g.setColour(osci::Colours::text());
        const auto titleWidth = juce::roundToInt(std::ceil(juce::TextLayout::getStringWidth(motion::style::heading(), section.title)));
        g.drawText(section.title, line, juce::Justification::centredLeft, true);
        if (section.note.isEmpty()) { continue; }
        g.setFont(motion::style::caption());
        g.setColour(osci::Colours::textMuted().withAlpha(.6f));
        g.drawText(section.note, line.withTrimmedLeft(titleWidth + motion::style::padding), juce::Justification::centredRight, true);
    }
}

bool MotionPropertyInspector::isGeneratorSpec(const motion::PropertySpec& spec) const {
    const auto* generator = targetGenerator();
    if (generator == nullptr) { return false; }
    return std::any_of(generator->parameters.begin(), generator->parameters.end(), [&](const auto& own) { return own.id == spec.id; });
}

bool MotionPropertyInspector::isMakingSpec(const motion::PropertySpec& spec) const {
    if (spec.id.starts_with("slider.") || spec.id.starts_with("fractal.") || isGeneratorSpec(spec)) { return true; }
    // A light's colour is half of what it is.
    const auto* generator = targetGenerator();
    return spec.group == "Colour" && generator != nullptr && generator->category == motion::GeneratorCategory::light;
}

bool MotionPropertyInspector::projectHasLight() const {
    // Anywhere in the project: a light outside a composition being edited
    // still lights what it holds.
    const auto& project = processor.document.mainProject();
    const auto holdsLight = [&](const motion::Composition& composition) {
        return std::any_of(composition.tracks.begin(), composition.tracks.end(), [&](const auto& track) {
            return std::any_of(track.clips.begin(), track.clips.end(), [&](const auto& clip) {
                const auto asset = clip.asset != 0 ? motion::findAsset(project.assets, clip.asset) : nullptr;
                const auto* generator = asset != nullptr ? motion::generatorOf(*asset) : nullptr;
                return generator != nullptr && generator->category == motion::GeneratorCategory::light;
            });
        });
    };
    return holdsLight(project) || std::any_of(project.definitions.begin(), project.definitions.end(), [&](const auto& definition) { return definition != nullptr && holdsLight(*definition); });
}

bool MotionPropertyInspector::isAdvanced(const Row& row) {
    if (row.section == "Shading") { return true; }
    return row.section == "Surface" && !(row.fields.size() == 1 && row.fields.front()->spec.id == "reflects");
}

juce::String MotionPropertyInspector::sectionOf(const motion::PropertySpec& spec) const {
    // A light's colour sits with the rest of what makes it.
    if (isGeneratorSpec(spec) || (spec.group == "Colour" && isMakingSpec(spec))) {
        const auto* generator = targetGenerator();
        return juce::String(generator->name.data(), generator->name.size());
    }
    const auto found = std::find_if(sectionOfGroup.begin(), sectionOfGroup.end(), [&](const auto& entry) { return entry.first == spec.group; });
    if (found != sectionOfGroup.end()) { return juce::String(found->second.data(), found->second.size()); }
    if (spec.group == "Field of view") { return "Lens"; }
    if (spec.group.starts_with("Slider")) { return "Script sliders"; }
    if (spec.id.starts_with("fractal.")) { return "Fractal"; }
    // The Scope's own rows: its picture.
    return "Picture";
}

bool MotionPropertyInspector::isModulated(const std::string& property) const {
    const auto found = motion::findPropertyTarget(processor.document.project(), target);
    return found.has_value() && isDriven(*found, routedProperties(), property);
}

std::vector<std::string> MotionPropertyInspector::routedProperties() const {
    std::vector<std::string> routed;
    for (const auto& route : processor.document.project().routes) {
        if (route.target == target) { routed.push_back(route.property); }
    }
    return routed;
}

bool MotionPropertyInspector::isDriven(const motion::PropertyTarget& found, const std::vector<std::string>& routed, const std::string& property) {
    const auto* curve = found.curve(property);
    return (curve != nullptr && curve->link.has_value()) || std::find(routed.begin(), routed.end(), property) != routed.end();
}

void MotionPropertyInspector::build(std::span<const motion::PropertySpec> specs) {
    rows.clear();
    content.removeAllChildren();
    if (lead != nullptr) { content.addChildComponent(lead); }
    if (trail != nullptr) { content.addChildComponent(trail); }
    for (const auto& spec : specs) {
        // A generator's own Rings is not the Rings pattern.
        if (rows.empty() || rows.back()->group != juce::String(spec.group.data(), spec.group.size()) || rows.back()->first != isMakingSpec(spec)) {
            auto row = std::make_unique<Row>();
            row->group = juce::String(spec.group.data(), spec.group.size());
            row->section = sectionOf(spec);
            row->first = isMakingSpec(spec);
            row->key.setName("Key " + row->group.toLowerCase());
            row->key.setTitle(row->key.getName());
            row->previous.setName("Previous " + row->group.toLowerCase() + " key");
            row->next.setName("Next " + row->group.toLowerCase() + " key");
            row->modulate.setName("Modulate " + row->group.toLowerCase());
            row->modulate.setTitle(row->modulate.getName());
            row->modulate.setTooltip("Modulate " + row->group.toLowerCase() + " in the Graph");
            auto* raw = row.get();
            row->modulate.onClick = [this, raw] {
                if (raw->fields.empty() || !onModulate) { return; }
                // Open the first axis that is actually driven.
                auto property = std::string(raw->fields.front()->spec.id);
                for (const auto& field : raw->fields) {
                    const auto id = std::string(field->spec.id);
                    if (isModulated(id)) { property = id; break; }
                }
                onModulate(target, property);
            };
            row->key.onClick = [this, raw] { toggleKeys(*raw); };
            row->key.onPopupMenu = [this, raw] { showKeyMenu(*raw); };
            row->onContextMenu = [this, raw] { showKeyMenu(*raw, true); };
            if (motionModes && (row->group == "Position" || row->group == "Rotation")) {
                const bool path = row->group == "Position";
                row->mode = std::make_unique<motion::ui::Chip>(path ? "Spatial path" : "Quaternion rotation", path ? motion::icons::Icon::path : motion::icons::Icon::rotate);
                row->mode->setName(path ? "Spatial path" : "Quaternion rotation");
                row->mode->setTitle(row->mode->getName());
                // A glyph until hovered or on, so it never reads as a second gutter.
                row->mode->quiet = true;
                // A misaligned mode stays on and realigns its keys.
                row->mode->onClick = [this, raw, path] { setMotionMode(path, raw->mode->getToggleState() || raw->misaligned); };
                row->addAndMakeVisible(*row->mode);
            }
            // Scale's axes linked: one changed, the others follow in proportion,
            // as linked width and height do in other editors. Remembered for
            // every object, as a preference.
            if (row->group == "Scale") {
                row->mode = std::make_unique<motion::ui::Chip>("Link scale", motion::icons::Icon::link);
                row->mode->setName("Link scale");
                row->mode->setTitle("Link scale");
                row->mode->setTooltip("Link X, Y and Z: changing one scales the others in proportion");
                row->mode->quiet = true;
                row->mode->setToggleState(scaleLinked(), juce::dontSendNotification);
                row->mode->onClick = [this, raw] {
                    processor.globalSettings.set("linkScale", raw->mode->getToggleState());
                    processor.globalSettings.save();
                };
                row->addAndMakeVisible(*row->mode);
            }
            // The anchor: the point rotation and scale turn about.
            if (row->group == "Anchor" && onCentreAnchor) {
                row->mode = std::make_unique<motion::ui::Chip>("Centre anchor", motion::icons::Icon::centre);
                row->mode->setName("Centre anchor");
                row->mode->setTitle("Centre anchor");
                row->mode->setTooltip("Centre the anchor, keeping the object in place");
                row->mode->quiet = true;
                row->mode->setClickingTogglesState(false);
                row->mode->onClick = [this] { onCentreAnchor(target); };
                row->addAndMakeVisible(*row->mode);
            }
            if (row->group == "Colour") {
                row->swatch = std::make_unique<Swatch>();
                row->swatch->onClick = [this, raw] { openColourPicker(*raw); };
                row->addAndMakeVisible(*row->swatch);
            }
            row->previous.onClick = [this, raw] { jumpToKey(*raw, false); };
            row->next.onClick = [this, raw] { jumpToKey(*raw, true); };
            content.addAndMakeVisible(*row);
            rows.push_back(std::move(row));
        }
        auto field = std::make_unique<Field>(spec);
        auto& editor = field->editor;
        editor.setSpec(spec);
        editor.setName(juce::String(spec.id.data(), spec.id.size()));
        editor.setTitle(juce::String(spec.label.data(), spec.label.size()));
        editor.setComponentID("motion." + juce::String(spec.id.data(), spec.id.size()));
        if (spec.id == "beam") {
            editor.setTooltip("This object's share of the beam's time, as a multiple of its even share: more draws it slower and brighter; 0 leaves it out");
        }
        if (spec.axis.size() == 1) {
            editor.setPrefix(juce::String(spec.axis.data(), spec.axis.size()));
            editor.setAxisColour(motion::style::axisColour(spec.axis, motion::style::axisZ()));
        }
        const std::string property(spec.id);
        // Undo names the property: "Undo Change Position X".
        const auto change = "Change " + juce::String(spec.label.data(), spec.label.size());
        editor.onBegin = [this, property] { beginGesture(property); };
        editor.onChange = [this, property](double value) { previewValue(property, value); };
        editor.onEnd = [this, change] { endGesture(change); };
        editor.onCancel = [this] { cancelGesture(); refresh(); };
        editor.onCommit = [this, property, change](double value) { commitValue(property, value, change); };
        // A modulator dropped here drives this axis; on the row, all of them.
        editor.getProperties().set("routeProperties", juce::String(property));
        auto& routes = rows.back()->getProperties();
        routes.set("routeProperties", routes["routeProperties"].toString() + (routes["routeProperties"].toString().isEmpty() ? "" : ",") + juce::String(property));
        rows.back()->addAndMakeVisible(editor);
        rows.back()->fields.push_back(std::move(field));
    }
    // One order for every target: what makes it, then how it draws
    // (Surface, its pictures a reference to play with, in view without
    // scrolling), then (after the lead's timing) the specs' order:
    // Transform, Lens or Audio, Appearance, and Shading last. Mirror leads
    // Surface's rows, above its advanced ones.
    const auto surfaceBegin = std::find_if(rows.begin(), rows.end(), [](const auto& row) { return row->section == "Surface"; });
    const auto surfaceEnd = std::find_if(surfaceBegin, rows.end(), [](const auto& row) { return row->section != "Surface"; });
    std::stable_partition(surfaceBegin, surfaceEnd, [](const auto& row) { return !isAdvanced(*row); });
    const auto making = std::find_if(rows.begin(), rows.end(), [](const auto& row) { return !row->first; });
    if (making <= surfaceBegin) { std::rotate(making, surfaceBegin, surfaceEnd); }
    advanced.onClick = [this] {
        refreshValues();
        layoutContent();
    };
    content.addChildComponent(advanced);
    surfaceTiles.reset();
    solidTiles.reset();
    presetTiles.reset();
    shadingTiles.reset();
    if (std::any_of(specs.begin(), specs.end(), [](const auto& spec) { return spec.id == "hatching"; })) {
        using motion::ui::pictures::Shading;
        addChoiceTiles(shadingTiles, "Shading", "hatching", {
            {"Brightness", "Light shows as how bright each line is drawn", motion::ui::pictures::shadingPicture(Shading::brightness)},
            {"Hatching", "Light shows as how densely a curve over the frame covers what it falls on", motion::ui::pictures::shadingPicture(Shading::hatching)}},
            {"Shade by brightness", "Shade by hatching"});
    }
    if (std::any_of(specs.begin(), specs.end(), [](const auto& spec) { return spec.id == "solid"; })) {
        using motion::ui::pictures::solidityPicture;
        addChoiceTiles(solidTiles, "Solidity", "solid", {
            {"Wireframe", "Seen through: everything behind it shows", solidityPicture(false)},
            {"Solid", "Its faces hide what's behind them, its own far side too", solidityPicture(true)}},
            {"Make wireframe", "Make solid"});
    }
    if (std::any_of(specs.begin(), specs.end(), [](const auto& spec) { return motion::isLinesProperty(spec.id); })) {
        std::vector<motion::ui::TileGrid::Choice> choices;
        for (const auto& style : surfaceStyles) {
            choices.push_back({juce::String(style.name.data(), style.name.size()), juce::String(style.tooltip.data(), style.tooltip.size()) + ". Shift-click to combine styles", motion::ui::pictures::surfacePicture(style.look)});
        }
        // One row of pictures, so Surface stays near the top without
        // pushing Transform far down; three columns of them when the panel
        // is too narrow for six.
        surfaceTiles = std::make_unique<motion::ui::TileGrid>("Surface styles", std::move(choices), 6);
        surfaceTiles->setCompact(46, 40, 3);
        surfaceTiles->onClick = [this](std::size_t index) {
            const auto found = motion::findPropertyTarget(processor.document.project(), target);
            if (!found.has_value() || found->locked) { return; }
            // A click is a step of its own; on a keyed style it keys it on
            // or off at the playhead. A click picks one style, as a choice
            // reads; Shift-click (or Advanced's rows) combines them.
            const std::string property(surfaceStyles[index].property);
            const auto on = surfaceTiles->tile(index).getToggleState();
            const auto time = keyTime(*found);
            const auto name = surfaceTiles->nameOf(index);
            const auto combining = juce::ModifierKeys::currentModifiers.isShiftDown();
            std::vector<std::string> others;
            for (std::size_t other = 0; other < surfaceStyles.size() && !combining; ++other) {
                const std::string id(surfaceStyles[other].property);
                if (other != index && baseValue(*found, id, time) > 0) { others.push_back(id); }
            }
            const auto* curve = found->curve(property);
            const auto keyed = curve != nullptr && curve->animated();
            juce::String label;
            if (combining) {
                label = keyed ? "Key " + name + (on ? " off" : " on") : (on ? "Turn off " : "Turn on ") + name;
            } else if (on && others.empty()) {
                // Already its only style: the click just shows its curve.
                if (onPropertySelected) { onPropertySelected(target, property); }
                return;
            } else {
                label = "Draw as " + name;
            }
            const auto value = combining && on ? 0.0 : 100.0;
            processor.document.edit(label, [&](motion::Project& project) {
                apply(project, property, value, time);
                for (const auto& other : others) { apply(project, other, 0.0, time); }
            });
            if (onPropertySelected) { onPropertySelected(target, property); }
            if (onKeyTimeEdited) { onKeyTimeEdited(); }
        };
        content.addAndMakeVisible(*surfaceTiles);
    }
    const auto* generator = targetGenerator();
    if (generator != nullptr && generator->category == motion::GeneratorCategory::light) {
        std::vector<motion::ui::TileGrid::Choice> choices {{"Steady", "No preset: it shines as you set it", {motion::ui::pictures::lightPresetPicture(std::nullopt), {}}}};
        for (const auto preset : motion::lightPresets) {
            const auto& definition = motion::lightPresetDefinition(preset);
            const juce::String name(definition.name.data(), definition.name.size());
            choices.push_back({motion::ui::pictures::lightPresetShortName(preset), name + ": " + juce::String(definition.description.data(), definition.description.size()) + ". Sets its colour; its modulators are in the Graph.", {motion::ui::pictures::lightPresetPicture(preset), {}}});
        }
        // One at a time, in a row.
        presetTiles = std::make_unique<motion::ui::TileGrid>("Light presets", std::move(choices), 3, motion::ui::TileGrid::Look::chips, true);
        presetTiles->onClick = [this](std::size_t index) {
            const auto preset = index == 0 ? std::nullopt : std::optional(motion::lightPresets[index - 1]);
            if (preset == motion::currentLightPreset(processor.document.project(), target)) { return; }
            motion::editLightPreset(processor.document, target, preset);
        };
        content.addAndMakeVisible(*presetTiles);
    }
    layoutContent();
}

double MotionPropertyInspector::baseValue(const motion::PropertyTarget& found, std::string_view property, double time) const {
    const auto* curve = found.curve(std::string(property));
    if (curve != nullptr) { return curve->evaluateBase(time); }
    const auto spec = std::find_if(specList.begin(), specList.end(), [&](const auto& candidate) { return candidate.id == property; });
    return spec != specList.end() ? spec->defaultValue : 0.0;
}

bool MotionPropertyInspector::refreshTiles(const motion::PropertyTarget& found, double time) {
    bool changed = false;
    outOfPlay.clear();
    const auto wasAvailable = advancedAvailable;
    advancedAvailable = false;
    const auto& project = processor.document.project();
    const auto routed = routedProperties();
    const auto valueOf = [&](std::string_view property) { return baseValue(found, property, time); };
    // How a property's value moves: driven by a modulator or a link, keyed, or neither.
    const auto movesOf = [&](std::string_view property) {
        using Moves = motion::ui::TileGrid::Moves;
        const std::string id(property);
        if (isDriven(found, routed, id)) { return Moves::driven; }
        const auto* curve = found.curve(id);
        return curve != nullptr && curve->animated() ? Moves::keyed : Moves::no;
    };
    // How much, when part-way.
    const auto partly = [](double value) { return value > 0 && value < 99.5 ? juce::String(juce::roundToInt(value)) + "%" : juce::String(); };
    if (shadingTiles != nullptr) {
        const auto amount = valueOf("hatching");
        const auto moves = movesOf("hatching");
        shadingTiles->setState(0, !(amount > 0));
        shadingTiles->setState(1, amount > 0, partly(amount), moves);
        shadingTiles->setLocked(found.locked);
        // An XY-only scope shows brightness as the beam's time.
        shadingTiles->tile(0).setTooltip(project.scope.xyOnly ? "Light shows as how bright each line is: on this XY-only scope, by how slowly the beam draws it"
            : "Light shows as how bright each line is drawn");
        // Hatching's rows only while it hatches, or is keyed or driven to.
        const auto hatched = amount > 0 || moves != motion::ui::TileGrid::Moves::no;
        advancedAvailable = hatched;
        for (const auto& row : rows) {
            if (!hatched && row->section == "Shading" && !row->fields.empty()) { outOfPlay[std::string(row->fields.front()->spec.id)] = "Choose Hatching in Shading first"; }
        }
        const auto shown = hatched && advanced.getToggleState();
        for (auto& row : rows) {
            if (row->section != "Shading" || row->isVisible() == shown) { continue; }
            row->setVisible(shown);
            changed = true;
        }
    }
    if (solidTiles != nullptr) {
        const auto amount = valueOf("solid");
        solidTiles->setState(0, !(amount > 0));
        solidTiles->setState(1, amount > 0, partly(amount), movesOf("solid"));
        solidTiles->setLocked(found.locked);
    }
    if (presetTiles != nullptr) {
        const auto current = motion::currentLightPreset(project, target);
        presetTiles->setState(0, !current.has_value());
        for (std::size_t index = 0; index < motion::lightPresets.size(); ++index) { presetTiles->setState(index + 1, current == motion::lightPresets[index]); }
        presetTiles->setLocked(found.locked);
    }
    if (surfaceTiles == nullptr) { return changed || wasAvailable != advancedAvailable; }
    // Whether a property is in play: on, or keyed or driven to be.
    const auto live = [&](std::string_view property) { return valueOf(property) > 0 || movesOf(property) != motion::ui::TileGrid::Moves::no; };
    for (std::size_t index = 0; index < surfaceStyles.size(); ++index) {
        const auto value = valueOf(surfaceStyles[index].property);
        surfaceTiles->setState(index, value > 0, partly(value), movesOf(surfaceStyles[index].property));
    }
    surfaceTiles->setLocked(found.locked);
    bool patterned = false;
    for (auto index = motion::patternsIndex; index < motion::patternsIndex + motion::patternCount; ++index) { patterned = patterned || live(motion::objectPropertySpecs[index].id); }
    for (auto& row : rows) {
        if (row->section != "Surface" || row->fields.empty()) { continue; }
        // Mirror always shows: it is no style, but how much it reflects.
        const auto property = row->fields.front()->spec.id;
        const auto noiseMotion = property == "noisePhase" || property == "noiseFlow";
        // Every style's amount is there to mix styles, the tiles picking one,
        // and how solid it is, to fade between wireframe and solid.
        const auto style = property == "solid" || std::any_of(surfaceStyles.begin(), surfaceStyles.end(), [&](const auto& candidate) { return candidate.property == property; });
        const auto inPlay = style || (property == "fillDetail" ? patterned : noiseMotion ? live("noise") || valueOf(property) != 0 : live(property));
        if (isAdvanced(*row)) { advancedAvailable = advancedAvailable || inPlay; }
        if (isAdvanced(*row) && !inPlay) { outOfPlay[std::string(property)] = noiseMotion ? "Turn on Noise first" : "Turn on a pattern first"; }
        const auto shown = !isAdvanced(*row) || (inPlay && advanced.getToggleState());
        if (row->isVisible() != shown) {
            row->setVisible(shown);
            changed = true;
        }
    }
    return changed || wasAvailable != advancedAvailable;
}

void MotionPropertyInspector::layoutContent() {
    const auto width = viewport.getWidth() - (viewport.isVerticalScrollBarShown() ? 8 : 0);
    content.sections.clear();
    int y = 0;
    // Rows from `from` up to `to`, each group opening with a hairline and its
    // heading (unless `headed` is false); `after` when something is above them.
    const auto inner = width - motion::style::padding * 2;
    const auto place = [&](std::size_t from, std::size_t to, bool after, bool headed) {
        for (auto index = from; index < to; ++index) {
            auto& row = rows[index];
            if (index == from || row->section != rows[index - 1]->section) {
                if (index > from || after) { y += motion::style::gap; }
                if (headed) {
                    content.sections.push_back({y, row->section, {}});
                    y += sectionHeading + motion::style::gap;
                }
                // Surface (whether it is solid, then how it draws itself) and
                // Shading open with their tiles; their rows below are those on.
                const auto openWith = [&](motion::ui::TileGrid* tiles) {
                    if (tiles == nullptr) { return; }
                    const auto height = tiles->heightFor(inner);
                    tiles->setBounds(motion::style::padding, y, inner, height);
                    y += height + motion::style::padding;
                };
                if (row->section == "Surface") {
                    openWith(solidTiles.get());
                    openWith(surfaceTiles.get());
                } else if (row->section == "Shading") {
                    openWith(shadingTiles.get());
                }
            }
            // Before a tiled section's first advanced row, the fold that shows them.
            if (isAdvanced(*row) && (index == from || !isAdvanced(*rows[index - 1]) || row->section != rows[index - 1]->section)) {
                advanced.setVisible(advancedAvailable);
                if (advancedAvailable) {
                    advanced.setBounds(motion::style::padding, y, 120, 20);
                    y += 20 + motion::style::gap;
                }
            }
            if (!row->isVisible()) { continue; }
            row->setBounds(motion::style::padding, y, inner, row->preferredHeight());
            // One-line values stack closer, as a list.
            auto next = index + 1;
            while (next < to && !rows[next]->isVisible() && rows[next]->section == row->section) { ++next; }
            const auto nextCompact = next < to && rows[next]->compact() && rows[next]->section == row->section;
            y += row->preferredHeight() + (row->compact() && nextCompact ? motion::style::gap : motion::style::padding);
        }
    };
    advanced.setVisible(false);
    std::size_t first = 0;
    while (first < rows.size() && rows[first]->first) { ++first; }
    // What makes it opens the page under the panel's header, which already
    // names it (Sphere, Point light), so with no heading of its own.
    if (first > 0) { y = motion::style::padding; }
    place(0, first, false, false);
    // A light's presets, under what it is.
    if (presetTiles != nullptr) {
        if (first > 0) { y += motion::style::gap; }
        content.sections.push_back({y, "Presets", {}});
        y += sectionHeading + motion::style::gap;
        const auto height = presetTiles->heightFor(inner);
        presetTiles->setBounds(motion::style::padding, y, inner, height);
        y += height + motion::style::padding;
    }
    // How it draws, straight under what makes it.
    auto surfaceTo = first;
    while (surfaceTo < rows.size() && rows[surfaceTo]->section == "Surface") { ++surfaceTo; }
    place(first, surfaceTo, first > 0, true);
    const auto above = first > 0 || surfaceTo > first;
    const auto leading = lead != nullptr && leadHeight ? leadHeight() : 0;
    if (leading > 0) {
        // A rule, then the lead's own heading on the same rhythm as the
        // sections around it.
        if (above) { y += motion::style::gap; }
        content.sections.push_back({y, {}, {}});
        y += sectionGap;
    }
    if (lead != nullptr) {
        lead->setBounds(motion::style::padding, y, width - motion::style::padding * 2, leading);
        lead->setVisible(leading > 0);
        y += leading > 0 ? leading + motion::style::gap : 0;
    }
    place(surfaceTo, rows.size(), above && leading == 0, true);
    const auto trailing = trail != nullptr && trailHeight ? trailHeight() : 0;
    if (trail != nullptr) {
        // An empty trail can still say something beside its heading (no
        // effects: where they come from), on one line.
        // (or one of its parts', when the trail is a stack of them).
        auto note = trail->getProperties()[motion::style::headingNote].toString();
        for (auto* part : trail->getChildren()) {
            if (note.isEmpty()) { note = part->getProperties()[motion::style::headingNote].toString(); }
        }
        if ((trailing > 0 || note.isNotEmpty()) && trailTitle.isNotEmpty()) {
            if (!rows.empty()) { y += motion::style::gap; }
            content.sections.push_back({y, trailTitle, note});
            y += sectionHeading + (trailing > 0 ? motion::style::gap : 0);
        }
        const auto inset = trailFullWidth ? 0 : motion::style::padding;
        trail->setBounds(inset, y, width - inset * 2, trailing);
        trail->setVisible(trailing > 0);
        y += trailing > 0 ? trailing + motion::style::padding : 0;
    }
    content.setSize(std::max(0, width), y + motion::style::padding);
    content.repaint();
}

double MotionPropertyInspector::keyTime(const motion::PropertyTarget& found) const {
    if (selectedKeyTime) {
        const auto selected = selectedKeyTime(found.id);
        if (selected.has_value()) { return *selected; }
    }
    return found.localTime(processor.document.project().frameTime(processor.position.load()));
}

void MotionPropertyInspector::apply(motion::Project& project, const std::string& property, double value, double time) const {
    auto* curve = motion::ensurePropertyCurve(project, target, property);
    if (curve == nullptr) { curve = createSlider(project, property); }
    if (curve == nullptr) { return; }
    motion::keyedit::setValue(*curve, time, value);
}

bool MotionPropertyInspector::scaleLinked() const {
    return processor.globalSettings.getBool("linkScale", false);
}

void MotionPropertyInspector::applyEdit(motion::Project& project, const std::string& property, double value, double time) const {
    const auto found = scaleLinked() && property.starts_with("scale.") ? motion::findPropertyTarget(std::as_const(project), target) : std::nullopt;
    if (!found.has_value()) {
        apply(project, property, value, time);
        return;
    }
    // From the values before the edit, so a drag keeps the proportions it started with.
    const auto from = baseValue(*found, property, time);
    std::vector<std::pair<std::string, double>> values;
    for (const auto* axis : {"scale.x", "scale.y", "scale.z"}) {
        if (std::none_of(specList.begin(), specList.end(), [&](const auto& spec) { return spec.id == axis; })) { continue; }
        const auto other = baseValue(*found, axis, time);
        values.emplace_back(axis, axis == property ? value : std::abs(from) > 1.0e-12 ? other * value / from : value);
    }
    for (const auto& [axis, next] : values) { apply(project, axis, next, time); }
}

void MotionPropertyInspector::beginGesture(const std::string& property) {
    const auto found = motion::findPropertyTarget(processor.document.project(), target);
    if (!found.has_value() || found->locked) { return; }
    gesture.begin();
    gestureTarget = target;
    gestureTime = keyTime(*found);
    if (onPropertySelected) { onPropertySelected(target, property); }
}

void MotionPropertyInspector::previewValue(const std::string& property, double value) {
    if (!gesture.active() || gestureTarget != target) { return; }
    // The project changed under the drag (an undo, a finished import): the
    // drag ends rather than rebuild from what it started on.
    if (gesture.stale()) {
        gesture.reset();
        return;
    }
    gesture.preview([&](motion::Project& project) { applyEdit(project, property, value, gestureTime); });
}

void MotionPropertyInspector::endGesture(const juce::String& name) {
    if (gesture.commit(name) && onKeyTimeEdited) { onKeyTimeEdited(); }
    refresh();
}

void MotionPropertyInspector::cancelGesture() {
    gesture.cancel();
}

void MotionPropertyInspector::addChoiceTiles(std::unique_ptr<motion::ui::TileGrid>& slot, const juce::String& title, const char* property, std::vector<motion::ui::TileGrid::Choice> choices, std::array<juce::String, 2> undoLabels) {
    slot = std::make_unique<motion::ui::TileGrid>(title, std::move(choices), 2, motion::ui::TileGrid::Look::chips, true);
    slot->onClick = [this, &slot, property, undoLabels](std::size_t index) {
        if (slot->tile(index).getToggleState()) { return; }
        const auto found = motion::findPropertyTarget(processor.document.project(), target);
        if (!found.has_value() || found->locked) { return; }
        const auto time = keyTime(*found);
        processor.document.edit(undoLabels[index], [&](motion::Project& project) { apply(project, property, index == 0 ? 0.0 : 100.0, time); });
        if (onKeyTimeEdited) { onKeyTimeEdited(); }
    };
    content.addAndMakeVisible(*slot);
}

osci::ColourPicker::Rgb MotionPropertyInspector::colourOf(const Row& row) {
    osci::ColourPicker::Rgb rgb {1.0, 1.0, 1.0};
    for (const auto& field : row.fields) {
        const auto id = std::string(field->spec.id);
        const auto index = id == "red" ? 0 : id == "green" ? 1 : id == "blue" ? 2 : -1;
        if (index >= 0) { rgb[static_cast<std::size_t>(index)] = std::clamp(field->editor.getValue(), 0.0, 1.0); }
    }
    return rgb;
}

void MotionPropertyInspector::openColourPicker(Row& row) {
    if (row.swatch == nullptr || !onShowPopover) { return; }
    auto picker = std::make_unique<osci::ColourPicker>(colourOf(row));
    picker->onBegin = [this] { beginGesture("red"); };
    picker->onChange = [this](osci::ColourPicker::Rgb rgb) {
        if (!gesture.active() || gestureTarget != target) { return; }
        if (gesture.stale()) {
            gesture.reset();
            return;
        }
        gesture.preview([&](motion::Project& project) {
            for (const auto& [property, value] : std::initializer_list<std::pair<const char*, double>> {{"red", rgb[0]}, {"green", rgb[1]}, {"blue", rgb[2]}}) {
                apply(project, property, value, gestureTime);
            }
        });
    };
    picker->onEnd = [this] { endGesture("Change colour"); };
    // Beside the whole row, so its red, green and blue stay in view.
    onShowPopover(std::move(picker), row);
}

void MotionPropertyInspector::commitValue(const std::string& property, double value, const juce::String& label) {
    const auto found = motion::findPropertyTarget(processor.document.project(), target);
    if (!found.has_value() || found->locked) {
        refresh();
        return;
    }
    const auto time = keyTime(*found);
    // Arrow-key steps in a row are one undo step.
    processor.document.editCoalesced(label, "property:" + juce::String(target) + ":" + juce::String(property), [&](motion::Project& project) { applyEdit(project, property, value, time); });
    if (onPropertySelected) { onPropertySelected(target, property); }
    if (onKeyTimeEdited) { onKeyTimeEdited(); }
}

std::vector<MotionPropertyInspector::SearchableRow> MotionPropertyInspector::searchableRows() const {
    std::vector<SearchableRow> found;
    for (const auto& row : rows) {
        if (row->fields.empty()) { continue; }
        SearchableRow made {std::string(row->fields.front()->spec.id), row->group, row->section, {}, isAdvanced(*row), {}};
        const auto out = outOfPlay.find(made.property);
        if (out != outOfPlay.end()) { made.unavailable = out->second; }
        for (const auto& field : row->fields) { made.labels.addIfNotAlreadyThere(juce::String(field->spec.label.data(), field->spec.label.size())); }
        // Folded under Advanced, or there only while in use.
        if (row->isVisible() || made.advanced) { found.push_back(std::move(made)); }
    }
    return found;
}

bool MotionPropertyInspector::revealProperty(const std::string& property) {
    const auto found = std::find_if(rows.begin(), rows.end(), [&](const auto& row) {
        return std::any_of(row->fields.begin(), row->fields.end(), [&](const auto& field) { return field->spec.id == property; });
    });
    if (found == rows.end()) { return false; }
    auto& row = **found;
    if (isAdvanced(row) && !advanced.getToggleState()) { advanced.setToggleState(true, juce::sendNotification); }
    if (!row.isVisible()) { return false; }
    osci::RevealOptions options;
    options.align = osci::RevealOptions::Align::centre;
    juce::Component::SafePointer<juce::Component> first(&row.fields.front()->editor);
    options.onArrived = [first] {
        if (first != nullptr && first->isShowing()) { first->grabKeyboardFocus(); }
    };
    reveal(row, std::move(options));
    return true;
}

bool MotionPropertyInspector::showsGroup(const juce::String& group) const {
    return std::any_of(rows.begin(), rows.end(), [&](const auto& row) { return row->group == group && row->isVisible(); });
}

bool MotionPropertyInspector::toggleGroupKeys(const juce::String& group) {
    for (auto& row : rows) {
        if (row->group == group && row->isVisible()) {
            toggleKeys(*row);
            return true;
        }
    }
    return false;
}

void MotionPropertyInspector::toggleKeys(Row& row) {
    const auto found = motion::findPropertyTarget(processor.document.project(), target);
    if (!found.has_value() || found->locked) { return; }
    const auto time = keyTime(*found);
    bool allKeyed = true;
    for (auto& field : row.fields) {
        const auto* curve = found->curve(std::string(field->spec.id));
        allKeyed = allKeyed && curve != nullptr && curve->hasKeyAt(time);
    }
    // Named as its button is: "Key position", "Remove position keys".
    const auto what = row.group.toLowerCase();
    processor.document.edit(allKeyed ? "Remove " + what + " keys" : "Key " + what, [&](motion::Project& project) {
        for (auto& field : row.fields) {
            auto* curve = motion::findPropertyCurve(project, target, std::string(field->spec.id));
            if (curve == nullptr) { curve = createSlider(project, std::string(field->spec.id)); }
            if (curve == nullptr) { continue; }
            motion::keyedit::setKeyed(*curve, time, !allKeyed);
        }
    });
    if (!row.fields.empty() && onPropertySelected) { onPropertySelected(target, std::string(row.fields.front()->spec.id)); }
    if (onKeyTimeEdited) { onKeyTimeEdited(); }
}

// The row's menu: Reset (to its defaults, keys gone), and removing its
// keyframes (the value at the playhead kept).
void MotionPropertyInspector::showKeyMenu(Row& row, bool atPointer) {
    const auto found = motion::findPropertyTarget(processor.document.project(), target);
    if (!found.has_value()) { return; }
    std::vector<std::string> properties;
    std::vector<std::pair<std::string, double>> defaults;
    bool animated = false, changed = false, driven = false;
    const auto routed = routedProperties();
    for (auto& field : row.fields) {
        properties.emplace_back(field->spec.id);
        // The field's own default: a fractal's depth is its source's.
        defaults.emplace_back(field->spec.id, field->spec.defaultValue);
        const auto* curve = found->curve(properties.back());
        animated = animated || (curve != nullptr && curve->animated());
        changed = changed || (curve != nullptr && (curve->animated() || std::abs(curve->base - field->spec.defaultValue) > 1.0e-9));
        driven = driven || isDriven(*found, routed, properties.back());
    }
    const auto what = row.group.toLowerCase();
    juce::PopupMenu menu;
    menu.addItem(2, "Reset " + what, changed && !found->locked);
    menu.addItem(1, "Remove " + what + " keyframes", animated && !found->locked);
    // Reset keeps what drives it: said, so a value still moving isn't a surprise.
    if (driven) {
        menu.addSeparator();
        menu.addItem(3, "Modulation stays (remove it in the Graph)", false);
    }
    const juce::Component::SafePointer<MotionPropertyInspector> owner(this);
    auto options = juce::PopupMenu::Options().withTargetComponent(&row.key);
    if (atPointer) { options = options.withMousePosition(); }
    motion::ui::showDocumentMenu(menu, row.key, processor.document, options, [owner, id = target, properties, defaults, what](int result) {
        if (owner == nullptr) { return; }
        if (result == 1) { owner->processor.document.removeKeyframes(id, properties, owner->processor.position.load(), what); }
        if (result == 2) { owner->processor.document.resetProperties(id, defaults, what); }
    });
}

void MotionPropertyInspector::jumpToKey(Row& row, bool forward) {
    const auto found = motion::findPropertyTarget(processor.document.project(), target);
    if (!found.has_value() || found->rate == 0) { return; }
    const auto now = keyTime(*found);
    std::optional<double> best;
    for (auto& field : row.fields) {
        const auto* curve = found->curve(std::string(field->spec.id));
        if (curve == nullptr) { continue; }
        for (const auto& key : curve->keyframes()) {
            const bool candidate = forward ? key.time > now + 1.0e-6 : key.time < now - 1.0e-6;
            if (candidate && (!best.has_value() || (forward ? key.time < *best : key.time > *best))) { best = key.time; }
        }
    }
    if (!best.has_value()) { return; }
    const auto projectTime = found->projectTime(*best);
    processor.seek(std::clamp(projectTime, 0.0, processor.document.project().duration));
}

juce::String MotionPropertyInspector::sourceKind(motion::Id clip) const {
    const auto& project = processor.document.project();
    const auto* item = motion::findClip(project, clip);
    if (item == nullptr) { return "Object"; }
    if (item->composition != 0) { return "Composition"; }
    const auto found = motion::findAsset(project.assets, item->asset);
    if (found == nullptr) { return "Object"; }
    // A generator by its own name (Point light, not Light): renamed, the
    // clip still says what it is.
    const auto* generator = motion::generatorOf(*found);
    return generator != nullptr ? juce::String(generator->name.data(), generator->name.size()) : motion::ui::sourceKind(*found);
}

std::vector<motion::PropertySpec> MotionPropertyInspector::luaSliders() const {
    std::vector<motion::PropertySpec> result;
    const auto& project = processor.document.project();
    const auto* clip = motion::findClip(project, target);
    if (clip == nullptr || clip->composition != 0) { return result; }
    const auto asset = motion::findAsset(project.assets, clip->asset);
    if (asset == nullptr || !asset->extension.equalsIgnoreCase(".lua")) { return result; }
    const auto script = asset->data.toString();
    for (const auto& spec : motion::luaSliderSpecs) {
        const auto name = juce::String("slider_") + juce::String::charToString(static_cast<juce::juce_wchar>(spec.id.back()));
        if (script.contains(name) || clip->properties.contains(std::string(spec.id))) { result.push_back(spec); }
    }
    return result;
}

std::optional<motion::PropertySpec> MotionPropertyInspector::fractalDepthSpec() const {
    const auto& project = processor.document.project();
    const auto* clip = motion::findClip(project, target);
    if (clip == nullptr || clip->composition != 0) { return std::nullopt; }
    const auto asset = motion::findAsset(project.assets, clip->asset);
    if (asset == nullptr || !motion::isFractal(asset->extension)) { return std::nullopt; }
    auto spec = motion::fractalPropertySpecs[0];
    spec.defaultValue = asset->fractalDepth;
    return spec;
}

const motion::GeneratorDefinition* MotionPropertyInspector::targetGenerator() const {
    return motion::generatorFor(processor.document.project(), target);
}

motion::Curve* MotionPropertyInspector::createSlider(motion::Project& project, const std::string& property) const {
    if (property.starts_with("generator.") || property.starts_with("light.")) { return motion::ensurePropertyCurve(project, target, property); }
    if (!property.starts_with("slider.") && !property.starts_with("fractal.")) { return nullptr; }
    auto found = motion::findPropertyTarget(project, target);
    if (!found.has_value() || found->properties == nullptr) { return nullptr; }
    // A depth not yet stored is the one the source was imported at.
    if (property.starts_with("fractal.") && !found->properties->contains(property)) {
        const auto depth = fractalDepthSpec();
        return &found->properties->emplace(property, motion::Curve(depth.has_value() ? depth->defaultValue : 3)).first->second;
    }
    return &(*found->properties)[property];
}

std::optional<std::pair<bool, bool>> MotionPropertyInspector::currentModes() const {
    const auto& project = processor.document.project();
    const auto* group = motion::findGroup(project, target);
    if (group != nullptr) { return std::make_pair(group->spatialPath, group->quaternionRotation); }
    const auto* track = motion::findClipTrack(project, target);
    if (track == nullptr || track->kind != motion::TrackKind::visual) { return std::nullopt; }
    const auto* clip = motion::findClip(project, target);
    return std::make_pair(clip->spatialPath, clip->quaternionRotation);
}

void MotionPropertyInspector::setMotionMode(bool path, bool enabled) {
    const auto id = target;
    const auto found = motion::findPropertyTarget(processor.document.project(), id);
    if (!found.has_value() || found->locked) { return; }
    const std::string prefix = path ? "position." : "rotation.";
    processor.document.edit(path ? (enabled ? "Use spatial path" : "Use separate position axes") : (enabled ? "Use quaternion rotation" : "Use Euler rotation"),
        [id, path, enabled, prefix](motion::Project& project) {
        const auto apply = [&](auto& owner) {
            (path ? owner.spatialPath : owner.quaternionRotation) = enabled;
            if (!enabled) { return; }
            std::set<double> times;
            for (const auto axis : {"x", "y", "z"}) {
                for (const auto& key : owner.properties[prefix + axis].keyframes()) { times.insert(key.time); }
            }
            for (const auto axis : {"x", "y", "z"}) {
                auto& curve = owner.properties[prefix + axis];
                const auto copy = curve;
                for (const auto time : times) {
                    if (std::none_of(copy.keyframes().begin(), copy.keyframes().end(), [time](const auto& key) { return key.time == time; })) {
                        curve.setKeyValue(time, copy.evaluateBase(time));
                    }
                }
            }
        };
        for (auto& group : project.groups) { if (group.id == id) { apply(group); } }
        auto* clip = motion::changeClip(project, id);
        if (clip != nullptr) { apply(*clip); }
    });
}

juce::Component* MotionPropertyInspector::routeTargetAt(juce::Point<int> point) {
    auto* component = getComponentAt(point);
    while (component != nullptr && component != this) {
        if (component->getProperties().contains("routeProperties")) { return component; }
        component = component->getParentComponent();
    }
    return nullptr;
}
