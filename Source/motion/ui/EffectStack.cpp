#include "EffectStack.h"

void MotionEffectStack::setOwner(std::optional<motion::Id> value, motion::Id clip) {
    if (value != owner || clip != context) {
        cancelGesture();
        cancelledGesture = false;
        owner = value;
        context = clip;
    }
    refresh();
}

void MotionEffectStack::setDragActive(bool active) {
    if (dragActive == active) { return; }
    dragActive = active;
    resized();
    if (onHeightChanged) { onHeightChanged(); }
    repaint();
}

int MotionEffectStack::preferredHeight() const {
    if (!owner.has_value()) { return 0; }
    int height = 0;
    if (!cards.empty() || dragActive) { height += headingHeight; }
    for (const auto& card : cards) { height += card->preferredHeight() + motion::style::gap; }
    if (dragActive && cards.empty()) { height += dropZoneHeight; }
    if (!stages.empty()) { height += chipHeight + motion::style::padding; }
    return height;
}

void MotionEffectStack::refresh() {
    const auto& project = processor.document.project();
    const auto* effects = owner.has_value() ? motion::findEffectOwner(project, *owner) : nullptr;
    std::vector<motion::Id> ids;
    if (effects != nullptr) {
        for (const auto& effect : *effects) { ids.push_back(effect.id); }
    }
    std::optional<motion::Id> added;
    if (ids.size() == listed.size() + 1 && std::find(ids.begin(), ids.end(), ids.back()) != ids.end()) {
        for (const auto id : ids) {
            if (std::find(listed.begin(), listed.end(), id) == listed.end()) { added = id; }
        }
    }
    if (ids != listed) {
        cancelGesture();
        cancelledGesture = false;
        listed = ids;
        cards.clear();
        cardShift.clear();
        // A hover over the old cards no longer names a card here.
        dragged = gapIndex = -1;
        for (const auto id : ids) {
            auto card = std::make_unique<Card>(*this, id);
            addAndMakeVisible(*card);
            cards.push_back(std::move(card));
        }
    }
    for (auto& card : cards) { card->update(); }
    refreshStages();
    resized();
    if (onHeightChanged) { onHeightChanged(); }
    if (added.has_value() && onReveal) {
        for (auto& card : cards) {
            if (card->getEffectId() == *added) { onReveal(*card); }
        }
    }
    repaint();
}

void MotionEffectStack::resized() {
    auto area = getLocalBounds();
    if (!cards.empty() || dragActive) { area.removeFromTop(headingHeight); }
    for (auto& card : cards) {
        card->setBounds(area.removeFromTop(card->preferredHeight()));
        area.removeFromTop(motion::style::gap);
    }
    dropZone = dragActive && cards.empty() ? area.removeFromTop(dropZoneHeight) : juce::Rectangle<int>();
    if (!stages.empty()) {
        area.removeFromTop(motion::style::padding);
        auto row = area.removeFromTop(chipHeight);
        for (auto& chip : stages) {
            const auto width = juce::GlyphArrangement::getStringWidthInt(motion::style::caption(), chip->getButtonText()) + 18;
            chip->setBounds(row.removeFromLeft(std::min(width, row.getWidth())));
            row.removeFromLeft(motion::style::gap);
        }
    }
}

void MotionEffectStack::paint(juce::Graphics& g) {
    if (!cards.empty() || dragActive) {
        g.setColour(motion::style::muted());
        g.setFont(motion::style::caption());
        g.drawText("Effects", getLocalBounds().removeFromTop(headingHeight), juce::Justification::centredLeft, false);
    }
    if (!dropZone.isEmpty()) {
        g.setColour(motion::style::accent().withAlpha(dropHover ? .18f : .07f));
        g.fillRoundedRectangle(dropZone.toFloat(), motion::style::radius);
        const float dashes[] {4.0f, 3.0f};
        juce::Path outline, dashed;
        outline.addRoundedRectangle(dropZone.toFloat().reduced(.5f), motion::style::radius);
        juce::PathStrokeType(1.0f).createDashedStroke(dashed, outline, dashes, 2);
        g.setColour(motion::style::accent().withAlpha(dropHover ? .9f : .45f));
        g.fillPath(dashed);
    }
}

bool MotionEffectStack::isInterestedInDragSource(const SourceDetails& details) {
    const auto value = details.description.toString();
    return owner.has_value() && (value.startsWith("motion-effect:") || value.startsWith("motion-effect-instance:"));
}

void MotionEffectStack::itemDragMove(const SourceDetails& details) {
    dropHover = true;
    const auto value = details.description.toString();
    const auto moving = value.startsWith("motion-effect-instance:") ? static_cast<motion::Id>(value.fromFirstOccurrenceOf(":", false, false).getLargeIntValue()) : motion::Id();
    const auto found = std::find(listed.begin(), listed.end(), moving);
    dragged = found != listed.end() ? static_cast<int>(found - listed.begin()) : -1;
    gapIndex = cards.empty() ? -1 : insertionIndex(details.localPosition.y);
    gapHeight = dragged >= 0 ? cards[static_cast<std::size_t>(dragged)]->getHeight() + motion::style::gap : 40;
    // Restarting a running timer on every move would postpone its ticks.
    if (!cardAnimation.isTimerRunning()) { cardAnimation.startTimerHz(60); }
    repaint();
}

void MotionEffectStack::itemDragExit(const SourceDetails&) {
    dropHover = false;
    dragged = gapIndex = -1;
    if (!cardAnimation.isTimerRunning()) { cardAnimation.startTimerHz(60); }
    repaint();
}

void MotionEffectStack::itemDropped(const SourceDetails& details) {
    dropHover = false;
    dragged = gapIndex = -1;
    settleCards();
    const auto index = insertionIndex(details.localPosition.y);
    const auto value = details.description.toString();
    if (value.startsWith("motion-effect:")) {
        addEffect(value.fromFirstOccurrenceOf(":", false, false).toStdString(), index);
    } else {
        move(static_cast<motion::Id>(value.fromFirstOccurrenceOf(":", false, false).getLargeIntValue()), index);
    }
    repaint();
}

void MotionEffectStack::addEffect(const std::string& type, int index) {
    if (!owner.has_value()) { return; }
    const auto target = *owner;
    const auto* existing = motion::findEffectOwner(processor.document.project(), target);
    const auto* definition = motion::effectDefinition(type);
    if (existing == nullptr || definition == nullptr || existing->size() >= motion::maximumEffectsPerOwner) { return; }
    const auto effect = motion::makeEffect(processor.document.newId(), *definition);
    processor.document.edit("Add " + juce::String(definition->name), [target, effect, index](motion::Project& project) {
        auto* effects = motion::findEffectOwner(project, target);
        if (effects == nullptr) { return; }
        const auto at = index < 0 ? effects->size() : std::min(static_cast<std::size_t>(index), effects->size());
        effects->insert(effects->begin() + static_cast<std::ptrdiff_t>(at), effect);
    });
    refresh();
}

MotionEffectStack::Card::Card(MotionEffectStack& owner, motion::Id effectId) : stack(owner), id(effectId) {
    const auto* effect = motion::findEffect(stack.processor.document.project(), id);
    const auto* definition = effect == nullptr ? nullptr : motion::effectDefinition(effect->type);
    name = effect == nullptr ? juce::String() : juce::String(effect->name);
    setName(name + " effect");
    close.setPaintsBackground(true);
    close.setIconPadding(6);
    close.onClick = [this] { juce::MessageManager::callAsync([safe = juce::Component::SafePointer<Card>(this)] { if (safe != nullptr) { safe->stack.remove(safe->id); } }); };
    addAndMakeVisible(close);
    if (definition == nullptr) { return; }
    for (const auto& parameter : definition->parameters) {
        auto row = std::make_unique<Row>();
        row->id = parameter.id;
        row->label = juce::String(parameter.name);
        row->field.setSpec(motion::effectPropertySpec(parameter));
        row->field.setName("Effect " + juce::String(parameter.id));
        row->field.setTitle(juce::String(parameter.name));
        row->field.getProperties().set("routeTarget", juce::String(static_cast<juce::int64>(id)));
        row->field.getProperties().set("routeProperties", juce::String(parameter.id));
        row->key.setName("Key effect " + juce::String(parameter.id));
        row->key.setTitle(row->key.getName());
        const auto property = parameter.id;
        row->field.onBegin = [this, property] { stack.beginGesture(id, property); };
        row->field.onChange = [this, property](double value) { stack.setValue(id, property, value); };
        row->field.onEnd = [this] { stack.finishGesture(); };
        row->field.onCancel = [this] { stack.cancelGesture(); update(); };
        row->field.onCommit = [this, property](double value) { stack.setValue(id, property, value); };
        row->key.onClick = [this, property] { stack.toggleKey(id, property); };
        addAndMakeVisible(row->field);
        addAndMakeVisible(row->key);
        rows.push_back(std::move(row));
    }
}

void MotionEffectStack::Card::update() {
    const auto& project = stack.processor.document.project();
    const auto* effect = motion::findEffect(project, id);
    // Fields and keys repaint themselves; the card only shows its switch.
    const auto wasEnabled = std::exchange(enabled, effect != nullptr && effect->enabled);
    if (enabled != wasEnabled) { repaint(); }
    const auto target = motion::findPropertyTarget(project, id);
    if (!target.has_value()) { return; }
    const auto local = target->localTime(stack.frameTime());
    for (auto& row : rows) {
        const auto* curve = target->curve(row->id);
        if (curve == nullptr) { continue; }
        if (!row->field.isEditing()) { row->field.setValue(curve->evaluateBase(local)); }
        const auto& keys = curve->keyframes();
        const bool keyed = std::any_of(keys.begin(), keys.end(), [local](const auto& key) { return std::abs(key.time - local) < 1.0e-6; });
        row->key.setState(keyed ? osci::KeyframeButton::State::keyed : (curve->animated() ? osci::KeyframeButton::State::animated : osci::KeyframeButton::State::unanimated));
    }
}

void MotionEffectStack::Card::resized() {
    auto area = getLocalBounds().reduced(motion::style::gap, 0);
    auto header = area.removeFromTop(headerHeight);
    close.setBounds(header.removeFromRight(headerHeight).reduced(3));
    for (auto& row : rows) {
        auto line = area.removeFromTop(motion::style::controlHeight);
        area.removeFromTop(2);
        line.removeFromLeft(labelWidth);
        row->key.setBounds(line.removeFromRight(18));
        line.removeFromRight(motion::style::gap);
        row->field.setBounds(line);
    }
}

void MotionEffectStack::Card::paint(juce::Graphics& g) {
    g.setColour(motion::style::raised().withAlpha(.45f));
    g.fillRoundedRectangle(getLocalBounds().toFloat(), motion::style::radius + 1);
    auto header = getLocalBounds().reduced(motion::style::gap, 0).removeFromTop(headerHeight);
    // The on/off box, then the name (dimmed when bypassed).
    box = header.removeFromLeft(22).toFloat().withSizeKeepingCentre(12, 12);
    g.setColour(motion::style::text().withAlpha(.6f));
    g.drawRoundedRectangle(box, 2, 1.2f);
    if (enabled) {
        g.setColour(motion::style::accent());
        g.fillRoundedRectangle(box.reduced(2.5f), 1.5f);
    }
    g.setColour(motion::style::text().withAlpha(enabled ? 1.0f : .4f));
    g.setFont(motion::style::title());
    g.drawText(name, header.withTrimmedRight(headerHeight), juce::Justification::centredLeft, true);
    g.setFont(motion::style::caption());
    g.setColour(motion::style::muted().withAlpha(enabled ? 1.0f : .5f));
    auto area = getLocalBounds().reduced(motion::style::gap, 0).withTrimmedTop(headerHeight);
    for (const auto& row : rows) {
        g.drawText(row->label, area.removeFromTop(motion::style::controlHeight).removeFromLeft(labelWidth).withTrimmedLeft(4), juce::Justification::centredLeft, true);
        area.removeFromTop(2);
    }
}

void MotionEffectStack::Card::mouseDown(const juce::MouseEvent& event) {
    if (event.y >= headerHeight) { return; }
    if (event.mods.isPopupMenu()) {
        stack.showMenu(id);
        return;
    }
    if (box.expanded(6).contains(event.position)) { stack.setEnabled(id, !enabled); }
}

void MotionEffectStack::Card::mouseDrag(const juce::MouseEvent& event) {
    if (event.getMouseDownY() >= headerHeight || event.getDistanceFromDragStart() < 4) { return; }
    auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this);
    if (container != nullptr && !container->isDragAndDropActive()) { container->startDragging("motion-effect-instance:" + juce::String(id), this); }
}

void MotionEffectStack::refreshStages() {
    const auto& project = processor.document.project();
    std::vector<std::pair<juce::String, motion::Id>> found;
    const auto add = [&](const juce::String& label, motion::Id id) {
        const auto* effects = motion::findEffectOwner(project, id);
        if (effects != nullptr && !effects->empty() && (!owner.has_value() || *owner != id)) {
            found.emplace_back(label + juce::String::fromUTF8(" \xc2\xb7 ") + juce::String(static_cast<int>(effects->size())), id);
        }
    };
    motion::Id group = 0;
    for (const auto& track : project.tracks) {
        const auto holds = std::any_of(track.clips.begin(), track.clips.end(), [this](const auto& clip) { return clip.id == context; });
        if ((!holds && track.id != context) || track.kind != motion::TrackKind::visual) { continue; }
        if (holds) { add(juce::String("Clip"), context); }
        add(juce::String(track.name), track.id);
        group = track.group;
    }
    const auto* selectedGroup = motion::findGroup(project, context);
    if (selectedGroup != nullptr) {
        add(juce::String(selectedGroup->name), selectedGroup->id);
        group = selectedGroup->parent;
    }
    for (std::size_t depth = 0; group != 0 && depth < motion::maximumGroupDepth; ++depth) {
        const auto* value = motion::findGroup(project, group);
        if (value == nullptr) { break; }
        add(juce::String(value->name), value->id);
        group = value->parent;
    }
    add("Composition", 0);
    if (found == listedStages) { return; }
    listedStages = found;
    stages.clear();
    for (const auto& [label, id] : found) {
        auto chip = std::make_unique<juce::TextButton>(label);
        chip->setName("Effects of " + label.upToFirstOccurrenceOf(juce::String::fromUTF8(" \xc2\xb7"), false, false));
        chip->setTooltip("Also applied");
        chip->setColour(juce::TextButton::buttonColourId, motion::style::field());
        // Showing another stage rebuilds these chips, so it waits.
        chip->onClick = [this, id = id] {
            juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MotionEffectStack>(this), id] { if (safe != nullptr && safe->onShowOwner) { safe->onShowOwner(id); } });
        };
        addAndMakeVisible(*chip);
        stages.push_back(std::move(chip));
    }
}

float MotionEffectStack::cardTarget(int index) const {
    if (gapIndex < 0) { return 0.0f; }
    auto boundary = gapIndex;
    if (dragged >= 0 && (boundary == dragged || boundary == dragged + 1)) { boundary = dragged; }
    const auto removed = dragged >= 0 && index > dragged ? -gapHeight : 0;
    return static_cast<float>(removed + (index >= boundary && index != dragged ? gapHeight : 0));
}

void MotionEffectStack::stepCards() {
    cardShift.resize(cards.size(), 0.0f);
    bool moving = false;
    for (std::size_t index = 0; index < cards.size(); ++index) {
        const auto target = cardTarget(static_cast<int>(index));
        auto& shift = cardShift[index];
        shift += (target - shift) * .3f;
        if (std::abs(target - shift) < .5f) { shift = target; } else { moving = true; }
        cards[index]->setTransform(juce::AffineTransform::translation(0.0f, static_cast<float>(juce::roundToInt(shift))));
        cards[index]->setAlpha(static_cast<int>(index) == dragged ? 0.0f : 1.0f);
    }
    if (!moving && gapIndex < 0) { cardAnimation.stopTimer(); }
}

void MotionEffectStack::settleCards() {
    cardAnimation.stopTimer();
    cardShift.clear();
    for (auto& card : cards) {
        card->setTransform({});
        card->setAlpha(1.0f);
    }
}

int MotionEffectStack::insertionIndex(int y) const {
    int index = 0;
    for (const auto& card : cards) {
        if (y < card->getBounds().getCentreY()) { break; }
        ++index;
    }
    return index;
}

void MotionEffectStack::setEnabled(motion::Id id, bool value) {
    processor.document.edit(value ? "Enable effect" : "Bypass effect", [id, value](motion::Project& project) {
        auto* effect = motion::findEffect(project, id);
        if (effect != nullptr) { effect->enabled = value; }
    });
    refresh();
}

void MotionEffectStack::showMenu(motion::Id id) {
    const auto found = std::find(listed.begin(), listed.end(), id);
    if (found == listed.end()) { return; }
    const auto index = static_cast<int>(found - listed.begin());
    juce::PopupMenu menu;
    menu.addItem(2, "Move up", index > 0);
    menu.addItem(3, "Move down", index + 1 < static_cast<int>(listed.size()));
    menu.addSeparator();
    menu.addItem(4, "Remove");
    motion::ui::showDocumentMenu(menu, *this, processor.document, juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(), [this, id, index](int result) {
        if (result == 2) { move(id, index - 1); }
        if (result == 3) { move(id, index + 2); }
        if (result == 4) { remove(id); }
    });
}

void MotionEffectStack::remove(motion::Id id) {
    cancelGesture();
    processor.document.tryEdit("Remove effect", [id](motion::Project& project) {
        for (auto* list : allOwners(project)) {
            const auto before = list->size();
            std::erase_if(*list, [id](const auto& effect) { return effect.id == id; });
            if (list->size() != before) { return true; }
        }
        return false;
    });
    refresh();
}

void MotionEffectStack::move(motion::Id id, int index) {
    if (!owner.has_value()) { return; }
    const auto target = *owner;
    cancelGesture();
    processor.document.tryEdit("Move effect", [id, target, index](motion::Project& project) {
        auto* destination = motion::findEffectOwner(project, target);
        if (destination == nullptr) { return false; }
        for (auto* list : allOwners(project)) {
            const auto found = std::find_if(list->begin(), list->end(), [id](const auto& item) { return item.id == id; });
            if (found == list->end()) { continue; }
            const auto source = static_cast<int>(found - list->begin());
            if (list == destination && (index == source || index == source + 1)) { return false; }
            auto effect = std::move(*found);
            list->erase(found);
            // Removing it first shifts later positions in its own stage.
            const auto at = std::clamp(list == destination && index > source ? index - 1 : index, 0, static_cast<int>(destination->size()));
            destination->insert(destination->begin() + at, std::move(effect));
            return true;
        }
        return false;
    });
    refresh();
}

std::vector<std::vector<motion::EffectInstance>*> MotionEffectStack::allOwners(motion::Project& project) {
    std::vector<std::vector<motion::EffectInstance>*> owners {&project.effects};
    for (auto& group : project.groups) { owners.push_back(&group.effects); }
    for (auto& track : project.tracks) {
        owners.push_back(&track.effects);
        for (auto& clip : track.clips) { owners.push_back(&clip.effects); }
    }
    return owners;
}

void MotionEffectStack::toggleKey(motion::Id id, const std::string& property) {
    cancelGesture();
    const auto target = motion::findPropertyTarget(processor.document.project(), id);
    const auto* curve = target.has_value() ? target->curve(property) : nullptr;
    if (curve == nullptr) { return; }
    const auto local = target->localTime(frameTime());
    const auto& keys = curve->keyframes();
    const bool keyed = std::any_of(keys.begin(), keys.end(), [local](const auto& key) { return std::abs(key.time - local) < 1.0e-6; });
    processor.document.edit(keyed ? "Remove keyframe" : "Key effect parameter", [id, property, local, keyed](motion::Project& project) {
        auto* changed = motion::findPropertyCurve(project, id, property);
        if (changed == nullptr) { return; }
        if (keyed) { changed->removeKey(local); } else { changed->setKeyValue(local, changed->evaluateBase(local)); }
    });
    if (onPropertySelected) { onPropertySelected(id, property); }
}

void MotionEffectStack::beginGesture(motion::Id id, const std::string& property) {
    cancelGesture();
    cancelledGesture = false;
    gesture = Gesture {processor.document.project(), processor.document.revision(), id, property, frameTime()};
    if (onPropertySelected) { onPropertySelected(id, property); }
}

void MotionEffectStack::setValue(motion::Id id, const std::string& property, double value) {
    if (cancelledGesture) { return; }
    if (gesture.has_value() && gesture->revision != processor.document.revision()) { gesture.reset(); cancelledGesture = true; return; }
    const auto time = gesture.has_value() ? gesture->time : frameTime();
    const auto target = motion::findPropertyTarget(processor.document.project(), id);
    const auto* curve = target.has_value() ? target->curve(property) : nullptr;
    if (curve == nullptr) { return; }
    const auto local = target->localTime(time);
    const auto* effect = motion::findEffect(processor.document.project(), id);
    const auto* definition = effect == nullptr ? nullptr : motion::effectDefinition(effect->type);
    if (definition == nullptr || !std::isfinite(value)) { return; }
    for (const auto& parameter : definition->parameters) {
        if (parameter.id == property) { value = std::clamp(value, parameter.min, parameter.max); }
    }
    if (!gesture.has_value() && value == curve->evaluateBase(local)) { return; }
    const auto operation = [id, property, value, local](motion::Project& project) {
        auto* changed = motion::findPropertyCurve(project, id, property);
        if (changed == nullptr) { return; }
        if (changed->animated()) { changed->setKeyValue(local, value); } else { changed->base = value; }
    };
    if (gesture.has_value()) {
        auto project = gesture->before;
        operation(project);
        const auto previous = motion::findPropertyTarget(gesture->before, id);
        const auto* original = previous.has_value() ? previous->curve(property) : nullptr;
        gesture->changed = original != nullptr && original->evaluateBase(local) != value;
        if (!gesture->changed) { project = gesture->before; }
        processor.document.preview(std::move(project));
        gesture->revision = processor.document.revision();
    } else {
        processor.document.editCoalesced("Change effect parameter", "effect:" + juce::String(id) + ":" + juce::String(property), operation);
    }
}

void MotionEffectStack::finishGesture() {
    cancelledGesture = false;
    if (!gesture.has_value()) { return; }
    auto done = std::move(*gesture);
    gesture.reset();
    if (done.changed && done.revision == processor.document.revision()) { processor.document.commit("Change effect parameter", std::move(done.before)); }
}

void MotionEffectStack::cancelGesture() {
    if (!gesture.has_value()) { return; }
    cancelledGesture = true;
    auto done = std::move(*gesture);
    gesture.reset();
    if (done.changed && done.revision == processor.document.revision()) { processor.document.preview(std::move(done.before)); }
}
