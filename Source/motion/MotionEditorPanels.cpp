#include "MotionEditor.h"
#include "ui/CanvasSizeEditor.h"
#include "ui/MarkerPanel.h"
#include "ui/GeneratorIcons.h"
#include "ui/MenuIcons.h"
#include "ui/ModelSizePanel.h"
#include "ui/StylePictures.h"

// Each panel set up: the Scene, the Scope, the library of sources, effects and
// modulators, Properties, the timeline and the Graph.

namespace {
// Panel tabs at their natural width, in the panel title style.
void styleTabs(osci::TabBar& tabs) {
    tabs.setTabSpacing(1, 8);
    tabs.setFont(motion::style::title());
    tabs.setColour(osci::TabBar::backgroundColourId, osci::Colours::veryDark());
    tabs.setColour(osci::TabBar::textColourId, osci::Colours::textMuted());
    tabs.setColour(osci::TabBar::selectedTextColourId, osci::Colours::text());
    tabs.setColour(osci::TabBar::indicatorColourId, osci::Colours::accentColor());
}
}

void MotionEditor::setUpScene() {
    sceneTools.move.setToggleState(true, juce::dontSendNotification);
    sceneTools.move.onClick = [this] { composition.setTool(MotionTransformTool::move); };
    sceneTools.rotate.onClick = [this] { composition.setTool(MotionTransformTool::rotate); };
    sceneTools.scale.onClick = [this] { composition.setTool(MotionTransformTool::scale); };
    sceneTools.anchor.onClick = [this] { composition.setTool(MotionTransformTool::anchor); };
    const auto centreAnchor = [this](motion::Id id) {
        const auto problem = composition.centreAnchor(id);
        if (problem.isNotEmpty()) { statusBar.show(problem); }
    };
    propertyInspector.onCentreAnchor = centreAnchor;
    composition.onToolChanged = [this](MotionTransformTool tool) {
        sceneTools.move.setToggleState(tool == MotionTransformTool::move, juce::dontSendNotification);
        sceneTools.rotate.setToggleState(tool == MotionTransformTool::rotate, juce::dontSendNotification);
        sceneTools.scale.setToggleState(tool == MotionTransformTool::scale, juce::dontSendNotification);
        sceneTools.anchor.setToggleState(tool == MotionTransformTool::anchor, juce::dontSendNotification);
    };
    sceneTools.parts.onClick = [this] { composition.setPartMode(true); };
    addChildComponent(partPickTools);
    partPickTools.box.onClick = [this] { composition.setPartPick(MotionCompositionView::PartPick::box); };
    partPickTools.lasso.onClick = [this] { composition.setPartPick(MotionCompositionView::PartPick::lasso); };
    partPickTools.connected.onClick = [this] { composition.setPartPick(MotionCompositionView::PartPick::pieces); };
    composition.onPartModeChanged = [this](bool active) {
        const auto tool = composition.viewState().tool;
        sceneTools.parts.setToggleState(active, juce::dontSendNotification);
        sceneTools.move.setToggleState(!active && tool == MotionTransformTool::move, juce::dontSendNotification);
        sceneTools.rotate.setToggleState(!active && tool == MotionTransformTool::rotate, juce::dontSendNotification);
        sceneTools.scale.setToggleState(!active && tool == MotionTransformTool::scale, juce::dontSendNotification);
        sceneTools.anchor.setToggleState(!active && tool == MotionTransformTool::anchor, juce::dontSendNotification);
        resized();
    };
    // Extract asks for the part's name first, beside the bar it came from.
    composition.onExtractParts = [this](const std::map<motion::Id, MotionCompositionView::Picked>& picks) {
        const auto* clip = picks.empty() ? nullptr : motion::findClip(processor.document.project(), picks.begin()->first);
        const auto asset = clip != nullptr ? motion::findAsset(processor.document.project().assets, clip->asset) : nullptr;
        const auto stem = asset == nullptr ? juce::String("Part") : asset->name.containsChar('.') ? asset->name.upToLastOccurrenceOf(".", false, false) : asset->name;
        auto panel = std::make_unique<MotionExtractPanel>(stem + " part");
        auto* shown = panel.get();
        panel->onApply = [this, picks, shown](juce::String name) {
            dismissPopover(shown);
            extractParts(picks, name);
        };
        showPopover(std::move(panel), getLocalArea(&composition, composition.extractAnchor()));
    };
    composition.onStatus = [this](const juce::String& message) { if (message.isNotEmpty()) { statusBar.show(message); } };
    composition.onSelectClips = [this](const std::vector<motion::Id>& ids, MotionCompositionView::SelectionChange change) {
        using Change = MotionCompositionView::SelectionChange;
        const auto& current = timeline.selectedClipIds();
        std::vector<motion::Id> chosen;
        if (change == Change::toggle) {
            // The clicked object leads the selection, or leaves it.
            for (const auto id : ids) {
                if (!current.contains(id)) { chosen.push_back(id); }
            }
            for (const auto id : current) {
                if (std::find(ids.begin(), ids.end(), id) == ids.end()) { chosen.push_back(id); }
            }
        } else {
            if (change == Change::add) { chosen.assign(current.begin(), current.end()); }
            for (const auto id : ids) {
                if (std::find(chosen.begin(), chosen.end(), id) == chosen.end()) { chosen.push_back(id); }
            }
        }
        timeline.selectClips(chosen);
    };
    sceneTools.path.onClick = [this] { composition.setMotionPathVisible(sceneTools.path.getToggleState()); };
    sceneTools.faces.onClick = [this] { composition.setSolidView(sceneTools.faces.getToggleState()); };
    composition.onMotionPathChanged = [this](bool visible) { sceneTools.path.setToggleState(visible, juce::dontSendNotification); };
    sceneTools.fly.onClick = [this] { composition.setNavigating(!composition.isNavigating()); };
    composition.onNavigationChanged = [this](bool active) { sceneTools.fly.setToggleState(active, juce::dontSendNotification); };
    sceneTools.frame.onClick = [this] { composition.frameSelection(); };
    sceneTools.lookThrough.onClick = [this] {
        composition.setDrivenCamera(sceneTools.lookThrough.getToggleState() ? selection : 0);
        // A camera that cannot be driven leaves the button off.
        sceneTools.lookThrough.setToggleState(composition.drivenCamera() != 0, juce::dontSendNotification);
        resized();
    };
    composition.onDrivenCameraChanged = [this](motion::Id id) {
        sceneTools.lookThrough.setToggleState(id != 0, juce::dontSendNotification);
        refreshCameraTools();
        resized();
    };
    composition.onContextMenu = [this] { showSceneMenu(); };
    composition.onPrepared = [this] { timeline.beamChanged(); propertyInspector.refreshValues(); };
    propertyInspector.beamShare = [this](motion::Id id) -> std::optional<double> {
        const auto* prepared = composition.preparedComposition();
        if (prepared == nullptr) { return std::nullopt; }
        const auto& project = processor.document.project();
        const auto time = processor.position.load();
        const auto live = processor.liveSourcePreview();
        const auto& shares = beamShares.at(*prepared, time, live.get());
        // A group's share is everything it holds.
        if (motion::findGroup(project, id) != nullptr) {
            double total = 0;
            for (const auto& share : shares) {
                const auto* track = motion::findClipTrack(project, share.clip);
                if (track != nullptr && motion::groupHolds(project, id, track->group)) { total += share.fraction; }
            }
            return total;
        }
        const auto* clip = motion::findClip(project, id);
        if (clip == nullptr || !clip->contains(time, project.tempo())) { return std::nullopt; }
        return MotionBeamShares::of(shares, id);
    };
    timeline.beamComposition = [this] { return composition.preparedComposition(); };
    composition.onEffectMoved = [this](motion::Id effect, motion::Id owner, bool copy) { moveEffectTo(effect, owner, copy); };
    composition.onEffectMovePreview = [this](motion::Id effect, std::optional<motion::Id> owner) {
        auto project = processor.document.project();
        const auto moved = owner.has_value() && motion::moveEffect(project, effect, *owner, juce::ModifierKeys::getCurrentModifiers().isAltDown(), std::numeric_limits<motion::Id>::max());
        processor.prepareComposition(moved ? project : processor.document.project());
        if (moved) { composition.preview(project); } else { composition.refresh(); }
    };
    timeline.onEffectMoved = [this](motion::Id effect, motion::Id owner, bool copy) { moveEffectTo(effect, owner, copy); };
    composition.onPropertyEdited = [this](motion::Id id, const std::string& property) { selectCurveTarget(id, property, false, true); };
    composition.isSelected = [this](motion::Id id) { return timeline.selectedClipIds().contains(id); };
    composition.selectedObjects = [this] {
        const auto& ids = timeline.selectedClipIds();
        return std::vector<motion::Id>(ids.begin(), ids.end());
    };
    composition.onOpenSource = [this](motion::Id id) {
        const auto& project = processor.document.project();
        const auto* clip = motion::findClip(project, id);
        if (clip == nullptr) { return; }
        if (clip->composition != 0) { enterComposition(clip->id); return; }
        const auto asset = motion::findAsset(project.assets, clip->asset);
        if (asset == nullptr) { return; }
        if (asset->extension.equalsIgnoreCase(".svg") && motion::drawing::isDrawing(asset->data.toString())) {
            showDrawingEditor(asset->id);
        } else if (asset->extension.equalsIgnoreCase(".txt") || asset->extension.equalsIgnoreCase(".lua")) {
            assetLibrary.onBake(asset->id);
        }
    };
    composition.onEffectPreview = [this](const std::string& type, std::optional<motion::Id> owner) { previewEffect(type, owner); };
    composition.onEffectDropped = [this](const std::string& type, motion::Id owner) { addEffectTo(type, owner); };
    composition.onSelection = [this](motion::Id id) { select(id); };
}

void MotionEditor::setUpScope() {
    outputHeader.setName("Scope");
    visualiser.setControlButtonsHidden(true);
    using Control = VisualiserComponent::Control;
    scopeTools.textureOutput.onClick = [this] { visualiser.clickControl(Control::textureOutput, &scopeTools.textureOutput); };
    scopeTools.settings.onClick = [this] { visualiser.clickControl(Control::settings, &scopeTools.settings); };
    scopeTools.popout.onClick = [this] { visualiser.clickControl(Control::popout, &scopeTools.popout); };
    scopeTools.fullScreen.onClick = [this] { visualiser.clickControl(Control::fullScreen, &scopeTools.fullScreen); };
    visualiser.setFullScreenCallback([this](FullScreenMode mode) {
        const auto next = mode == FullScreenMode::TOGGLE ? !scopeFullScreen : mode == FullScreenMode::FULL_SCREEN;
        setScopeFullScreen(next);
    });
    scopeTools.canvas.onClick = [this] {
        auto panel = std::make_unique<MotionCanvasSettings>(processor.recordingParameters.getCanvasSize());
        auto* controls = panel.get();
        const juce::Component::SafePointer<MotionEditor> owner(this);
        const juce::Component::SafePointer<juce::Component> popover(controls);
        controls->onApply = [owner, popover](VisualiserRenderSize size) {
            juce::MessageManager::callAsync([owner, popover, size] {
                if (owner == nullptr || popover == nullptr) { return; }
                // The frame is the project's: an edit, saved and undone with it.
                if (size.width != owner->processor.document.mainProject().exportSettings.width || size.height != owner->processor.document.mainProject().exportSettings.height) {
                    owner->processor.document.edit("Change the canvas", [size](motion::Project& project) {
                        project.exportSettings.width = size.width;
                        project.exportSettings.height = size.height;
                    });
                }
                dismissPopover(popover.getComponent());
            });
        };
        showPopover(std::move(panel), getLocalArea(&scopeTools, scopeTools.canvas.getBounds()));
    };
}

void MotionEditor::setUpLibrary() {
    modulatorLibrary.onError = [this](const juce::String& text) { statusBar.show(text); };
    libraryTabs.setName("Library tabs");
    styleTabs(libraryTabs);
    libraryTabs.addTab("Sources");
    libraryTabs.addTab("Effects");
    libraryTabs.addTab("Modulators");
    libraryTabs.onSelectionChanged = [this](int index) {
        assetLibrary.setVisible(index == 0);
        importButton.setVisible(index == 0);
        effectLibrary.setVisible(index == 1);
        modulatorLibrary.setVisible(index == 2);
        if (index == 2) { modulatorLibrary.refresh(); }
        resized();
    };
    // Double-clicking an effect adds it to what Properties shows.
    effectLibrary.onInsert = [this](const std::string& type) { effectStack.addEffect(type); };
    importButton.onClick = [this] {
        namespace icons = motion::ui::menu_icons;
        juce::PopupMenu menu;
        const auto item = [](int id, const juce::String& text, juce::Path glyph) {
            juce::PopupMenu::Item result(text);
            result.itemID = id;
            result.image = icons::stroked(std::move(glyph), icons::ink());
            return result;
        };
        // In groups: a file, one drawn or written here, one osci-motion makes,
        // and one from elsewhere (another app, or an example).
        menu.addItem(item(AddMenuId::importFile, "Import source...", icons::glyph::folder()));
        menu.addSeparator();
        menu.addItem(item(AddMenuId::drawing, "Draw a shape...", icons::glyph::pencil()));
        menu.addItem(item(AddMenuId::newScript, "Write a script...", icons::glyph::braces()));
        menu.addSeparator();
        // Sources made from a few numbers, by category, each with its picture.
        juce::PopupMenu generate;
        for (const auto category : {motion::GeneratorCategory::shape, motion::GeneratorCategory::curve, motion::GeneratorCategory::grid, motion::GeneratorCategory::solid}) {
            generate.addSectionHeader(juce::String(motion::generatorCategoryName(category).data()));
            for (std::size_t index = 0; index < motion::generatorDefinitions.size(); ++index) {
                const auto& definition = motion::generatorDefinitions[index];
                if (definition.category != category) { continue; }
                juce::PopupMenu::Item entry(juce::String(definition.name.data(), definition.name.size()));
                entry.itemID = AddMenuId::firstGenerator + static_cast<int>(index);
                entry.image = motion::ui::generatorIcon(definition, icons::ink());
                generate.addItem(std::move(entry));
            }
        }
        menu.addSubMenu("Generate", generate, true, icons::stroked(icons::glyph::shapes(), icons::ink()));
        // Lights: what the Scope draws, lit by them.
        juce::PopupMenu lights;
        for (std::size_t index = 0; index < motion::generatorDefinitions.size(); ++index) {
            const auto& definition = motion::generatorDefinitions[index];
            if (definition.category != motion::GeneratorCategory::light) { continue; }
            juce::PopupMenu::Item entry(juce::String(definition.name.data(), definition.name.size()));
            entry.itemID = AddMenuId::firstGenerator + static_cast<int>(index);
            entry.image = motion::ui::generatorIcon(definition, icons::ink());
            lights.addItem(std::move(entry));
        }
        // A point light already moving on its own, named for how.
        lights.addSectionHeader("Presets");
        for (std::size_t index = 0; index < motion::lightPresets.size(); ++index) {
            const auto& definition = motion::lightPresetDefinition(motion::lightPresets[index]);
            juce::PopupMenu::Item entry(juce::String(definition.name.data(), definition.name.size()));
            entry.itemID = AddMenuId::firstLightPreset + static_cast<int>(index);
            entry.image = icons::unit(motion::ui::pictures::lightPresetPicture(motion::lightPresets[index]), icons::ink());
            lights.addItem(std::move(entry));
        }
        menu.addSubMenu("Light", lights, true, icons::stroked(icons::glyph::bulb(), icons::ink()));
        // Mirrors: surfaces that show the rest of the scene.
        juce::PopupMenu mirrors;
        for (std::size_t index = 0; index < motion::mirrorPresets.size(); ++index) {
            const auto& preset = motion::mirrorPresets[index];
            juce::PopupMenu::Item entry(juce::String(preset.name.data(), preset.name.size()));
            entry.itemID = AddMenuId::firstMirror + static_cast<int>(index);
            // Pictured as the Add a mirror panel pictures them.
            entry.image = icons::unit(motion::ui::pictures::ofMirror(preset).lines, icons::ink());
            mirrors.addItem(std::move(entry));
        }
        menu.addSubMenu("Mirror", mirrors, true, icons::unit(motion::ui::pictures::mirrorBall().lines, icons::ink()));
        menu.addSeparator();
        // Live: what other apps send, drawn as it arrives, never prepared ahead.
        juce::PopupMenu live;
        live.addItem(item(AddMenuId::blenderSource, "Blender...", icons::glyph::live()));
        const auto senders = motion::LiveTextureController::senders();
        if (motion::LiveTextureController::available()) {
            live.addSectionHeader(motion::texture::technology());
            for (std::size_t index = 0; index < senders.size(); ++index) {
                live.addItem(item(AddMenuId::firstTexture + static_cast<int>(index), senders[index].describe(), icons::glyph::screen()));
            }
            if (senders.empty()) {
                juce::PopupMenu::Item none("No app is sharing a picture");
                none.isEnabled = false;
                live.addItem(std::move(none));
            }
        }
        menu.addSubMenu("Live", live, true, icons::stroked(icons::glyph::live(), icons::ink()));
        // The examples that ship with osci-motion, by kind, each pictured as
        // it draws (a blank space until its picture is made).
        juce::PopupMenu examples;
        std::vector<juce::String> resources;
        const std::array<juce::Path, motion::ui::exampleKinds.size()> kindGlyphs {icons::glyph::cube(), icons::glyph::braces(), icons::glyph::play(), icons::glyph::tree(), icons::glyph::letter()};
        for (std::size_t kindIndex = 0; kindIndex < motion::ui::exampleKinds.size(); ++kindIndex) {
            const auto& exampleKind = motion::ui::exampleKinds[kindIndex];
            juce::PopupMenu kind;
            for (int index = 0; index < BinaryData::namedResourceListSize; ++index) {
                const juce::String file = BinaryData::getNamedResourceOriginalFilename(BinaryData::namedResourceList[index]);
                if (!file.endsWithIgnoreCase(exampleKind.extension)) { continue; }
                resources.push_back(BinaryData::namedResourceList[index]);
                juce::PopupMenu::Item entry(file.upToLastOccurrenceOf(".", false, false).replaceCharacter('_', ' '));
                entry.itemID = AddMenuId::firstExample + static_cast<int>(resources.size()) - 1;
                const auto picture = examplePictures.picture(resources.back());
                entry.image = picture.has_value() ? icons::picture(*picture) : icons::blank();
                kind.addItem(std::move(entry));
            }
            if (kind.getNumItems() > 0) { examples.addSubMenu(exampleKind.title, kind, true, icons::stroked(kindGlyphs[kindIndex], icons::ink())); }
        }
        menu.addSubMenu("Examples", examples, true, icons::stroked(icons::glyph::book(), icons::ink()));
        const juce::Component::SafePointer<MotionEditor> owner(this);
        menu.setLookAndFeel(&getLookAndFeel());
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&importButton), [owner, resources, senders](int choice) {
            if (owner == nullptr) { return; }
            // Which of a list's items was chosen, if one was.
            const auto chosen = [choice](int first, std::size_t count) -> std::optional<std::size_t> {
                if (choice < first || choice - first >= static_cast<int>(count)) { return std::nullopt; }
                return static_cast<std::size_t>(choice - first);
            };
            if (choice == AddMenuId::importFile) { owner->chooseSourceFile(); }
            if (choice == AddMenuId::blenderSource) { owner->showBlenderSettings(); }
            if (choice == AddMenuId::drawing) { owner->showDrawingEditor(0); }
            if (choice == AddMenuId::newScript) { owner->newScript(); }
            const auto generator = chosen(AddMenuId::firstGenerator, motion::generatorDefinitions.size());
            const auto lightPreset = chosen(AddMenuId::firstLightPreset, motion::lightPresets.size());
            const auto mirror = chosen(AddMenuId::firstMirror, motion::mirrorPresets.size());
            const auto example = chosen(AddMenuId::firstExample, resources.size());
            const auto sender = chosen(AddMenuId::firstTexture, senders.size());
            if (sender.has_value()) { owner->addTextureSource(senders[*sender]); }
            if (generator.has_value()) { owner->addGenerator(motion::generatorDefinitions[*generator]); }
            if (lightPreset.has_value()) { owner->addGenerator(motion::generatorNamed("pointLight"), motion::lightPresets[*lightPreset]); }
            if (mirror.has_value()) { owner->addMirror(motion::mirrorPresets[*mirror]); }
            if (example.has_value()) { owner->importExample(resources[*example]); }
        });
    };
    assetLibrary.onReplace = [this](motion::Id id) { replaceSourceFile(id); };
    // A model switched to its file's own size, or back to fitted, is
    // prepared again in place.
    // A model's size, in a popover by its row: the Scene shows the change
    // as it is made, so nothing covers it.
    assetLibrary.onModelSettings = [this](motion::Id id, juce::Component& button) {
        const auto found = motion::findAsset(processor.document.project().assets, id);
        if (found == nullptr) { return; }
        auto panel = std::make_unique<MotionModelSizePanel>(found->name, found->originalSize, found->fittedScale);
        panel->onChange = [this, id](bool keep) { setModelOriginalSize(id, keep); };
        showPopover(std::move(panel), getLocalArea(&button, button.getLocalBounds()));
    };
    assetLibrary.onEditDrawing = [this](motion::Id id) { showDrawingEditor(id); };
    assetLibrary.onRemoveComposition = [this](motion::Id id) {
        const auto& definitions = processor.document.mainProject().definitions;
        const auto found = std::find_if(definitions.begin(), definitions.end(), [id](const auto& value) { return value->id == id; });
        if (found == definitions.end()) { return; }
        const auto expected = *found;
        const auto generation = processor.document.generation();
        const juce::Component::SafePointer<MotionEditor> owner(this);
        motion::ui::MessageSheet::show(*this, "Remove unused composition?",
            "Remove \"" + expected->name + "\" from the library? Shared media and child compositions remain available. You can undo this.",
            "Remove", [owner, id, generation, expected] {
                if (owner == nullptr || owner->processor.document.generation() != generation) { return; }
                const auto& current = owner->processor.document.mainProject().definitions;
                if (std::find(current.begin(), current.end(), expected) == current.end()) { return; }
                const auto result = owner->processor.document.removeComposition(id);
                if (result.failed()) { owner->statusBar.show(result.getErrorMessage()); }
            }, {}, true);
    };
    assetLibrary.onOpenComposition = [this](motion::Id id) { enterComposition(id, true); };
    assetLibrary.onMessage = [this](const juce::String& message, bool problem) { statusBar.show(message, problem ? MotionStatusBar::Kind::warning : MotionStatusBar::Kind::notice); };
    assetLibrary.onSelectUses = [this](motion::Id id) {
        std::vector<motion::Id> clips;
        for (const auto& track : processor.document.project().tracks) {
            for (const auto& clip : track.clips) { if (clip.asset == id) { clips.push_back(clip.id); } }
        }
        timelineTabs.setSelectedIndex(0);
        timeline.selectClips(clips);
        if (clips.empty()) { statusBar.show("This source is only used inside other compositions.", MotionStatusBar::Kind::notice); }
    };
    assetLibrary.onInsert = [this](motion::Id id) { timeline.insertAsset(id, -1, -1); };
    assetLibrary.liveStatus = [this](motion::Id id) {
        const auto asset = motion::findAsset(processor.document.mainProject().assets, id);
        return asset != nullptr && motion::texture::isSource(asset->extension) ? processor.textureInputs().statusText(id) : processor.blenderInputs().statusText(id);
    };
    assetLibrary.onBake = [this](motion::Id id) {
        for (const auto& asset : processor.document.mainProject().assets) {
            if (asset->id == id && asset->liveIdentity != nullptr) { showLiveSettings(id); return; }
        }
        const auto& assets = processor.document.project().assets;
        const auto found = motion::findAsset(assets, id);
        if (found == nullptr || (!found->extension.equalsIgnoreCase(".lua") && !found->extension.equalsIgnoreCase(".txt")
                && !found->extension.equalsIgnoreCase(".lsystem") && !osci::files::isImage(found->extension))) { return; }
        preparationRequests.push_back({{}, processor.position.load(), processor.document.generation(), found});
        showNextPreparationSettings();
    };
    assetLibrary.onCancelImport = [this] { cancelImports(); };
}

void MotionEditor::setUpProperties() {
    effectStack.onPropertySelected = [this](motion::Id id, std::string property) { selectCurveTarget(id, property, false, true); };
    effectStack.onShowOwner = [this](motion::Id id) { select(id); };
    propertyInspector.onRouteModulator = [this](motion::Id modulator, motion::Id target, std::vector<std::string> properties) {
        const auto result = processor.document.routeModulator(modulator, target, properties);
        if (result.failed()) { statusBar.show(result.getErrorMessage()); return; }
        modulatorLibrary.refresh();
        if (!properties.empty()) { selectCurveTarget(target, properties.front(), selectionIsCamera(), true); }
    };
    propertyInspector.onRoutePreview = [this](std::optional<MotionPropertyInspector::RouteRequest> request) {
        const auto preview = request.has_value() ? processor.document.previewRoute(request->modulator, request->target, request->properties) : std::nullopt;
        if (preview.has_value()) {
            processor.prepareComposition(*preview);
            composition.preview(*preview);
        } else {
            processor.prepareComposition(processor.document.project());
            composition.refresh();
        }
    };
    effectStack.onHeightChanged = [this] { propertyInspector.relayout(); };
    effectStack.onReveal = [this](juce::Component& card) {
        osci::RevealOptions options;
        options.margin = motion::style::padding;
        propertyInspector.reveal(card, options);
    };
    // A clip's timing leads its Properties, as layer timing does in other editors.
    inspectorLead.add(compositionSettings, [this] { return compositionSettings.preferredHeight(); });
    inspectorLead.add(clipTimingPanel, [this] { return clipTimingPanel.preferredHeight(); });
    inspectorLead.add(modelSize, [this] { return modelSize.preferredHeight(); });
    modelSize.onChange = [this](motion::Id id, bool original) { setModelOriginalSize(id, original); };
    compositionSettings.onTiming = [this](int command) { applyTiming(command); };
    compositionSettings.onError = [this](const juce::String& message) { statusBar.show(message); };
    // Panels in the lead resize it, and Properties lays out again.
    const auto relayoutLead = [this] { inspectorLead.resized(); propertyInspector.relayout(); };
    compositionSettings.onHeightChanged = relayoutLead;
    inspectorLead.add(textAnimation, [this] { return textAnimation.preferredHeight(); });
    propertyInspector.setLead(&inspectorLead, [this] { return inspectorLead.preferredHeight(); });
    clipTimingPanel.onHeightChanged = relayoutLead;
    textAnimation.onHeightChanged = relayoutLead;
    modelSize.onHeightChanged = relayoutLead;
    // A change to the characters prepares the text source again in place.
    textAnimation.onApply = [this](motion::Id id, motion::TextSettings settings) {
        const auto& assets = processor.document.project().assets;
        const auto found = motion::findAsset(assets, id);
        if (found == nullptr) { return; }
        // One preparation at a time: a later change waits for the current one.
        if (!pendingImports.empty()) {
            queuedTextAnimation = QueuedTextAnimation {id, settings, processor.document.generation()};
            return;
        }
        // Only the animation is this section's; the text and its type stay
        // as the source has them now.
        auto merged = found->textSettings;
        merged.animation = settings.animation;
        merged.characterDelay = settings.characterDelay;
        merged.characterDuration = settings.characterDuration;
        merged.hold = settings.hold;
        merged.amount = settings.amount;
        SourceRequest request {{}, processor.position.load(), processor.document.generation(), found};
        request.textSettings = merged;
        beginSourceImport(request);
    };
    // The owner's effects follow its properties.
    propertyInspector.setTrail(&effectStack, [this] { return effectStack.preferredHeight(); }, "Effects");
    propertyInspector.onPropertySelected = [this](motion::Id id, const std::string& property) { selectCurveTarget(id, property, selectionIsCamera(), true); };
    propertyInspector.onModulate = [this](motion::Id id, const std::string& property) {
        selectCurveTarget(id, property, selectionIsCamera(), true);
        timelineTabs.setSelectedIndex(1);
    };
    propertyInspector.onRename = [this](motion::Id id, const juce::String& text) {
        processor.document.tryEdit("Rename camera", [id, name = text.toStdString()](motion::Project& project) {
            for (auto& camera : project.cameras) {
                if (camera.id == id && camera.name != name) { camera.name = name; return true; }
            }
            return false;
        });
    };
    propertyInspector.selectedKeyTime = [this](motion::Id id) { return composition.selectedKeyContentTime(id); };
    propertyInspector.onKeyTimeEdited = [this] { composition.retainSelectedKeyAfterEdit(); };
    propertyInspector.onShowPopover = [this](std::unique_ptr<juce::Component> content, juce::Component& anchor) { showPopover(std::move(content), getLocalArea(&anchor, anchor.getLocalBounds()), Placement::left); };
}

void MotionEditor::setUpTimeline() {
    timelineDivider.setName("Resize timeline");
    previewDivider.setName("Resize preview panels");
    // Each overlaps the panels' edges either side of the 3 px gutter, so it
    // is above them and easier to grab than the gutter alone.
    for (auto* divider : {&timelineDivider, &previewDivider}) {
        divider->setQuiet(true);
        divider->toFront(false);
    }
    styleTabs(timelineTabs);
    timeline.onEffectAdded = [this](motion::Id owner, motion::Id) { select(owner); };
    timelineTabs.setName("Timeline tabs");
    timelineTabs.addTab("Timeline");
    timelineTabs.addTab("Graph");
    timelineTabs.onSelectionChanged = [this](int index) {
        // Both share the panel height the user chose: switching never moves
        // the Scene or Scope.
        timeline.setVisible(index == 0);
        curveEditor.setVisible(index == 1);
        if (index == 1) { refreshCurveList(); }
        resized();
    };
    timelineDivider.onStart = [this] { dividerStart = timelineFraction; };
    timelineDivider.onDrag = [this](int delta) {
        timelineFraction = std::clamp(dividerStart - static_cast<double>(delta) / workspaceHeight, 0.25, 0.65);
        resized();
    };
    timelineDivider.onReset = [this] { timelineFraction = 0.34; resized(); };
    previewDivider.onStart = [this] { dividerStart = previewFraction; };
    previewDivider.onDrag = [this](int delta) {
        previewFraction = std::clamp(dividerStart + static_cast<double>(delta) / previewWidth, 0.25, 0.75);
        resized();
    };
    previewDivider.onReset = [this] { previewFraction = 0.5; resized(); };
    timeline.onOpenSource = [this](motion::Id id) { composition.onOpenSource(id); };
    timeline.onEditTempo = [this](double beat, double bpm, std::optional<double> replacing) {
        const auto& changes = processor.document.project().tempoChanges;
        const auto existing = changes != nullptr && replacing.has_value() ? std::find_if(changes->begin(), changes->end(), [&](const auto& change) { return change.beat == *replacing; }) : std::vector<motion::TempoChange>::const_iterator();
        const auto ramped = changes != nullptr && replacing.has_value() && existing != changes->end() && existing->ramp;
        auto panel = std::make_unique<MotionTempoPanel>(beat, bpm, processor.document.project().beatsPerBar, ramped);
        const juce::Component::SafePointer<MotionTempoPanel> tempoPanel(panel.get());
        const auto apply = popoverEdit(panel.get(), [tempoPanel](const juce::String& error) { if (tempoPanel != nullptr) { tempoPanel->setError(error); } });
        panel->onApply = [apply, beat, replacing](double value, bool glide) {
            apply([=](motion::Document& document) { return document.setTempoChange(beat, value, replacing, glide); });
        };
        showPopover(std::move(panel), getLocalArea(&timeline, timeline.rulerAnchor(processor.document.project().tempo().seconds(beat))));
    };
    timeline.onEditMarker = [this](motion::Id id, double time) {
        const auto& project = processor.document.project();
        juce::String name = "Marker";
        if (id == 0) {
            const auto found = std::find_if(project.markers.begin(), project.markers.end(), [time](const auto& marker) { return motion::sameTime(marker.time, time); });
            if (found != project.markers.end()) { id = found->id; }
        }
        if (id != 0) {
            const auto found = std::find_if(project.markers.begin(), project.markers.end(), [id](const auto& marker) { return marker.id == id; });
            if (found == project.markers.end()) { return; }
            name = found->name;
            time = found->time;
        }
        auto panel = std::make_unique<MotionMarkerPanel>(name, time, project.timeGrid(), project.duration);
        const juce::Component::SafePointer<MotionMarkerPanel> markerPanel(panel.get());
        const auto apply = popoverEdit(panel.get(), [markerPanel](const juce::String& error) { if (markerPanel != nullptr) { markerPanel->setError(error); } });
        panel->onApply = [apply, id](juce::String name, double time) {
            apply([=](motion::Document& document) { return document.setMarker(id, time, name); });
        };
        showPopover(std::move(panel), getLocalArea(&timeline, timeline.rulerAnchor(time)));
    };
    timeline.onEnterComposition = [this](motion::Id id) { enterComposition(id); };
    timeline.onSelection = [this](motion::Id id) { select(id); };
    timeline.onAddCamera = [this](double time) { addCamera(time); };
    timeline.onMakeUnique = [this](motion::Id id) {
        libraryTabs.setSelectedIndex(0);
        const auto& project = processor.document.project();
        const auto* clip = motion::findClip(project, id);
        const auto asset = clip != nullptr ? motion::findAsset(project.assets, clip->asset) : nullptr;
        if (asset == nullptr) { return; }
        SourceRequest request {{}, processor.position.load(), processor.document.generation(), asset};
        request.uniqueClip = id;
        beginSourceImport(std::move(request));
    };
    // Its timing in Properties, gone to as a browser goes to an anchor, ready to type.
    timeline.onTimingRequested = [this](motion::Id id) {
        select(id);
        const juce::Component::SafePointer<MotionEditor> owner(this);
        juce::MessageManager::callAsync([owner] {
            if (owner == nullptr) { return; }
            osci::RevealOptions options;
            options.focus = true;
            // Near the top, with its Timing heading still in view.
            options.align = osci::RevealOptions::Align::start;
            options.margin = 40;
            owner->propertyInspector.reveal(owner->clipTimingPanel.durationField(), options);
        });
    };
    timeline.onLoopSelection = [this] { loopSelection(); };
    // The composition's effects row: Properties shows the composition, its
    // effects scrolled to and marked.
    timeline.onShowCompositionEffects = [this] { showInComposition([this]() -> juce::Component& { return effectStack; }); };
    timeline.canPaste = [this] { return !std::holds_alternative<std::monostate>(clipboard); };
    timeline.onCommand = [this](const juce::String& name) {
        for (const auto& command : commands) {
            if (command.name == name) { command.action(); return; }
        }
    };
    timeline.onRevealSource = [this](motion::Id asset) {
        libraryTabs.setSelectedIndex(0);
        assetLibrary.refresh();
        assetLibrary.revealAsset(asset);
    };
    timeline.onError = [this](const juce::String& message) { statusBar.show(message); };
    timeline.chosenSource = [this] { return assetLibrary.chosen(); };
    timeline.onHint = [this](const juce::String& message) { statusBar.show(message, MotionStatusBar::Kind::notice); };
    timeline.onPreview = [this](const motion::Project* project) {
        processor.prepareComposition(project != nullptr ? *project : processor.document.project());
        if (project != nullptr) { composition.preview(*project); } else { composition.refresh(); }
    };
}

void MotionEditor::setUpGraph() {
    graphSide.addAndMakeVisible(routingPanel);
    graphSide.addChildComponent(keyPanel);
    keyPanel.onTime = [this](double time) { curveEditor.setActiveKeyTime(time); };
    keyPanel.onValue = [this](double value) { curveEditor.setActiveKeyValue(value); };
    keyPanel.onInterpolation = [this](motion::Interpolation interpolation) { curveEditor.setSelectedInterpolation(interpolation); };
    keyPanel.onEase = [this] { curveEditor.easeSelected(true, true); };
    curveEditor.onSelectionChanged = [this] { refreshKeyPanel(); };
    graphSideViewport.setViewedComponent(&graphSide, false);
    motion::style::fadeScrolling(graphSideViewport);
    routingPanel.onLayoutChanged = [this] { layoutGraphSide(); };
    routingPanel.onError = [this](const juce::String& text) { statusBar.show(text); };
    curveEditor.setVisible(false);
    for (const auto& spec : motion::objectPropertySpecs) { curveProperties.emplace_back(spec.id); }
    curveList.onChoose = [this](const std::string& property) { selectCurveTarget(curveTarget, property, cameraCurve, true); };
    curveList.onShow = [this](const std::string& property, bool show) {
        // Siblings of the edited channel are shown by default, so their eye hides them.
        if (show) { shownCurves.insert(property); hiddenCurves.erase(property); } else { shownCurves.erase(property); hiddenCurves.insert(property); }
        refreshCurveList();
        curveEditor.repaint();
    };
    curveList.onRemoveKeys = [this](const std::string& property) {
        const auto removed = processor.document.removeKeyframes(curveTarget, {property}, processor.position.load(), motion::propertyLabel(processor.document.project(), curveTarget, property).toLowerCase());
        if (removed.failed()) { statusBar.show(removed.getErrorMessage()); }
    };
    curveEditor.onPropertyChosen = [this](const std::string& property) { selectCurveTarget(curveTarget, property, cameraCurve, true); };
    curveEditor.onPreview = [this](const motion::PropertyMap* curves) {
        auto preview = processor.document.project();
        if (curves != nullptr) {
            for (const auto& [name, curve] : *curves) {
                auto* target = motion::findPropertyCurve(preview, curveTarget, name);
                if (target != nullptr) { *target = curve; }
            }
        }
        processor.prepareComposition(preview);
        composition.preview(preview);
    };
}

// A preset's generator reflecting wholly (every preset names one: see MirrorPresets.h).
void MotionEditor::addMirror(const motion::MirrorPreset& preset) {
    addGenerator(*motion::findGenerator(preset.generator), std::nullopt, preset.name, {{"reflects", motion::Curve(100)}});
}

void MotionEditor::setModelOriginalSize(motion::Id asset, bool original) {
    const auto current = motion::findAsset(processor.document.project().assets, asset);
    if (current == nullptr || current->originalSize == original) { return; }
    SourceRequest request;
    request.replacement = current;
    request.generation = processor.document.generation();
    request.originalSize = original;
    beginSourceImport(std::move(request));
}
