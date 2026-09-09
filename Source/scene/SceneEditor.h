#pragma once

#include "../PluginProcessor.h"
#include "NavigationMotion.h"
#include "../parser/FileFormatRegistry.h"
#include "../components/OverlayDialogHelpers.h"
#include <osci_gui/osci_gui.h>

class SceneEditor final : public juce::Component, private juce::ListBoxModel, private juce::Timer, public juce::FileDragAndDropTarget {
public:
    SceneEditor(OscirenderAudioProcessor& p, int index, std::function<void(osci::texture::SourceInfo)> connect = {}) : connectTexture(std::move(connect)), processor(p), fileIndex(index), model(p.getFileController().ensureScene(index)), canvas(*this), list(*this) {
        setName("Scene editor");
        setWantsKeyboardFocus(true);
        addAndMakeVisible(objectHeader);
        addAndMakeVisible(previewHeader);
        addAndMakeVisible(propertiesHeader);
        addAndMakeVisible(canvas);
        addAndMakeVisible(list);
        list.setModel(this);
        list.setRowHeight(34);
        list.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setName("Scene objects");
        list.setOutlineThickness(0);
        configure(addObjectButton, "+ Add object", [this] {
            if (onAddObject) {
                onAddObject();
            }
        });
        configure(move, "Move", [this] { setTool(0); });
        configure(rotate, "Rotate", [this] { setTool(1); });
        configure(scale, "Scale", [this] { setTool(2); });
        configure(navigate, "Navigate", [this] { canvas.setNavigating(!canvas.isNavigating()); });
        navigate.setWantsKeyboardFocus(false);
        navigate.setTooltip("Navigate the camera: WASD or arrows, Q/E down/up, mouse to look, Shift faster, Escape to finish.");
        configure(frame, "Frame", [this] { frameSelection(); });
        configure(reset, "Reset", [this] {
            beginEdit();
            for (int i = 0; i < scene::controlCount; ++i) {
                transform().set(i, scene::defaultValue(i));
            }
            endEdit();
        });

        configure(expose, "Expose to DAW", [this] { exposureMenu(); });
        expose.setVisible(!juce::JUCEApplicationBase::isStandaloneApp());
        move.setTooltip("Move selected object (W). Drag an axis, or drag the centre freely.");
        rotate.setTooltip("Rotate selected object (E). Drag a coloured ring.");
        scale.setTooltip("Scale selected object (R). Drag an axis, or the centre for uniform scale.");
        frame.setTooltip("Frame the selection (F). This changes the scene's output camera.");
        reset.setTooltip("Reset the selected object's transform or camera framing.");
        addAndMakeVisible(title);
        title.setFont(juce::Font(juce::FontOptions(15.0f)));
        title.setEditable(true, true, false);
        title.onTextChange = [this] {
            if (selected != nullptr) {
                juce::SpinLock::ScopedLockType guard(model->lock);
                const auto extension = juce::File::getCurrentWorkingDirectory().getChildFile(selected->name).getFileExtension();
                auto name = title.getText().trim();
                if (name.isNotEmpty()) {
                    selected->name = name.endsWithIgnoreCase(extension) ? name : name + extension;
                    list.updateContent();
                    processor.getFileController().sceneChanged();
                }
            }
        };
        for (int i = 0; i < scene::controlCount; ++i) {
            auto& slider = controls[i];
            addAndMakeVisible(slider);
            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            slider.setRange(scene::minimum(i), scene::maximum(i), 0.001);
            slider.setNumDecimalPlacesToDisplay(i >= 3 && i < 6 ? 1 : 2);
            slider.setName(scene::controlNames[i]);
            slider.setTooltip("Drag to adjust; hold Shift for fine control. Double-click to type a value.");
            slider.setTextValueSuffix(i >= 3 && i < 6 ? juce::String::charToString(0x00b0) : "");
            slider.setDoubleClickReturnValue(true, scene::defaultValue(i));
            slider.onDragStart = [this] { beginEdit(); };
            slider.onDragEnd = [this] { endEdit(); };
            slider.onValueChange = [this, i] {
                const bool standalone = !editing;
                if (standalone) {
                    beginEdit();
                }
                transform().set(i, (float)controls[i].getValue());
                if (standalone) {
                    endEdit();
                }
                canvas.repaint();
            };
        }
        if (!model->objects.empty()) {
            selected = model->objects.front();
        }
        list.selectRow(selected != nullptr ? 1 : 0);
        setTool(0);
        refresh();
        startTimerHz(60);
    }

    ~SceneEditor() override {
        canvas.setNavigating(false);
    }

    void importSource(const juce::String& name, const juce::MemoryBlock& data) {
        addObject(name, std::make_shared<juce::MemoryBlock>(data));
    }

    void importLive(int kind) {
        if (kind == 2) {
            const juce::String code("return { ext_x, ext_y, 0 }");
            importSource("Audio input.lua", juce::MemoryBlock(code.toRawUTF8(), code.getNumBytesAsUTF8()));
        } else {
            auto object = processor.getFileController().addLiveSceneObject(fileIndex, kind == 0);
            if (object != nullptr) { select(object); }
        }
    }

    bool isCurrentScene() const {
        auto& files = processor.getFileController();
        return files.getCurrentFileIndex() == std::optional<int>(fileIndex) && files.getScene(fileIndex) == model;
    }

    std::shared_ptr<scene::Object> getSelectedObject() const { return selected; }
    std::function<void(std::shared_ptr<scene::Object>)> onEditSource;
    std::function<void()> onAddObject;

    void resized() override {
        auto area = getLocalBounds();
        objectPanelBounds = area.removeFromLeft(juce::jlimit(126, 158, getWidth() / 5));
        area.removeFromLeft(osci::PanelHeader::panelGap);
        propertiesPanelBounds = area.removeFromRight(194);
        area.removeFromRight(osci::PanelHeader::panelGap);
        previewHeader.setBounds(area.removeFromTop(osci::PanelHeader::height));
        area.removeFromTop(osci::PanelHeader::panelGap);
        previewPanel = area;

        listPanel = objectPanelBounds.withTrimmedTop(osci::PanelHeader::height);
        inspectorBounds = propertiesPanelBounds.withTrimmedTop(osci::PanelHeader::height);
        objectHeader.setBounds(objectPanelBounds.withHeight(osci::PanelHeader::height));
        propertiesHeader.setBounds(propertiesPanelBounds.withHeight(osci::PanelHeader::height));
        auto objectBody = listPanel.reduced(5);
        addObjectButton.setBounds(objectBody.removeFromBottom(28));
        objectBody.removeFromBottom(osci::PanelHeader::panelGap);
        list.setBounds(objectBody);
        title.setBounds(getLocalArea(&propertiesHeader, propertiesHeader.contentBounds()));
        auto inspector = inspectorBounds.reduced(10);
        for (int row = 0; row < 3; ++row) {
            propertyLabels[row] = inspector.removeFromTop(22);
            auto fields = inspector.removeFromTop(28);
            const int cell = fields.getWidth() / 3;
            for (int axis = 0; axis < 3; ++axis) {
                auto box = fields.removeFromLeft(cell);
                axisLabels[row * 3 + axis] = box.removeFromLeft(12);
                controls[row * 3 + axis].setBounds(box.withTrimmedRight(3));
            }
            inspector.removeFromTop(12);
        }
        reset.setBounds(inspector.removeFromTop(28));
        inspector.removeFromTop(16);
        expose.setBounds(inspector.removeFromTop(30));
        auto toolbar = getLocalArea(&previewHeader, previewHeader.contentBounds());
        for (auto* button : { &move, &rotate, &scale }) {
            button->setBounds(toolbar.removeFromLeft(60).reduced(1, 0));
        }
        frame.setBounds(toolbar.removeFromRight(44));
        navigate.setBounds(toolbar.removeFromRight(76).reduced(2, 0));
        auto preview = previewPanel.reduced(5);
        footer = preview.removeFromBottom(24);
        canvas.setBounds(preview);
    }

    juce::Colour panelColour() const {
        return findColour(osci::groupComponentBackgroundColourId);
    }
    void paint(juce::Graphics& g) override {
        g.setColour(panelColour());
        g.fillRoundedRectangle(objectPanelBounds.toFloat(), osci::LookAndFeel::RECT_RADIUS);
        g.fillRoundedRectangle(propertiesPanelBounds.toFloat(), osci::LookAndFeel::RECT_RADIUS);
        g.setColour(osci::Colours::veryDark());
        g.fillRoundedRectangle(previewPanel.toFloat(), osci::LookAndFeel::RECT_RADIUS);
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        const char* rows[] = { selected != nullptr ? "Position" : "Target", "Rotation", selected != nullptr ? "Scale" : "View size" };
        for (int row = 0; row < 3; ++row) {
            g.setColour(osci::Colours::textMuted());
            g.drawText(rows[row], propertyLabels[row], juce::Justification::centredLeft);
            for (int axis = 0; axis < 3; ++axis) {
                if (selected != nullptr || row < 2 || axis == 0) {
                    g.setColour(axisColour(axis));
                    g.drawText(juce::String::charToString("XYZ"[axis]), axisLabels[row * 3 + axis], juce::Justification::centredLeft);
                }
            }
        }
        g.setColour(osci::Colours::textMuted());
        g.setFont(juce::Font(juce::FontOptions(11.0f)));
        g.drawFittedText(canvas.isNavigating() ? "WASD move / Mouse look / Esc finish" : "Drag orbit / Shift pan / Scroll zoom", footer, juce::Justification::centred, 2);
    }

    bool stopNavigation() {
        if (!canvas.isNavigating()) { return false; }
        canvas.setNavigating(false);
        return true;
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (key == juce::KeyPress::escapeKey && stopNavigation()) { return true; }
        if (canvas.isNavigating()) { return canvas.navigationKey(key); }
        if (key.getModifiers().isCommandDown() && key.getKeyCode() == 'Z') {
            if (key.getModifiers().isShiftDown()) {
                processor.getUndoManager().redo();
            } else {
                processor.getUndoManager().undo();
            }
            refresh();
            return true;
        }
        if (key.getModifiers().isAnyModifierKeyDown()) {
            return false;
        }
        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
            removeSelected();
            return true;
        }
        switch (key.getKeyCode()) {
            case 'W': setTool(0); return true;
            case 'E': setTool(1); return true;
            case 'R': setTool(2); return true;
            case 'F': frameSelection(); return true;
            default: return false;
        }
    }

    bool isInterestedInFileDrag(const juce::StringArray& paths) override {
        return std::any_of(paths.begin(), paths.end(), [](const auto& path) { return osci::files::isSupportedSource(juce::File(path)); });
    }
    void filesDropped(const juce::StringArray& paths, int, int) override {
        for (const auto& path : paths) {
            auto file = juce::File(path);
            if (osci::files::isSupportedSource(file)) {
                auto data = std::make_shared<juce::MemoryBlock>();
                if (file.loadFileAsData(*data)) {
                    addObject(file.getFileName(), data);
                }
            }
        }
    }

private:
    static juce::Colour axisColour(int axis) {
        return axis == 0 ? juce::Colour(0xffef8e91) : (axis == 1 ? juce::Colour(0xff8ed4aa) : juce::Colour(0xff8ab7f2));
    }
    void configure(juce::TextButton& button, const juce::String& name, std::function<void()> click) {
        addAndMakeVisible(button);
        button.setButtonText(name);
        button.setColour(juce::TextButton::textColourOnId, osci::Colours::text());
        button.setName(name);
        button.onClick = std::move(click);
    }
    scene::Transform& transform() { return selected != nullptr ? selected->transform : model->camera; }
    std::array<float, scene::controlCount> values() {
        std::array<float, scene::controlCount> result;
        for (int i = 0; i < scene::controlCount; ++i) {
            result[i] = transform().get(i);
        }
        return result;
    }
    struct TransformEdit : juce::UndoableAction {
        TransformEdit(std::shared_ptr<scene::Scene> s, std::shared_ptr<scene::Object> o, std::array<float, 9> before, std::array<float, 9> after)
            : scene(std::move(s)), object(std::move(o)), before(before), after(after) {}
        bool apply(const std::array<float, 9>& values) {
            auto& t = object != nullptr ? object->transform : scene->camera;
            for (int i = 0; i < scene::controlCount; ++i) {
                t.set(i, values[i]);
            }
            return true;
        }
        bool perform() override { return apply(after); }
        bool undo() override { return apply(before); }
        std::shared_ptr<scene::Scene> scene;
        std::shared_ptr<scene::Object> object;
        std::array<float, 9> before, after;
    };
    void beginEdit() {
        if (editing) {
            return;
        }
        editing = true;
        before = values();
        for (int i = 0; i < scene::controlCount; ++i) {
            transform().parameter(i).beginChangeGesture();
        }
    }
    void endEdit() {
        if (!editing) {
            return;
        }
        const auto after = values();
        for (int i = 0; i < scene::controlCount; ++i) {
            transform().parameter(i).endChangeGesture();
        }
        editing = false;
        if (before != after) {
            processor.getUndoManager().beginNewTransaction(selected != nullptr ? "Transform object" : "Move scene camera");
            processor.getUndoManager().perform(new TransformEdit(model, selected, before, after));
        }
        refresh();
    }
    void setTool(int value) {
        stopNavigation();
        tool = value;
        move.setToggleState(value == 0, juce::dontSendNotification);
        rotate.setToggleState(value == 1, juce::dontSendNotification);
        scale.setToggleState(value == 2, juce::dontSendNotification);
        canvas.repaint();
    }
    void refresh() {
        if (selected != nullptr && std::find(model->objects.begin(), model->objects.end(), selected) == model->objects.end()) {
            selected = model->objects.empty() ? nullptr : model->objects.front();
        }
        reset.setVisible(true);
        if (!title.isBeingEdited()) {
            title.setText(selected != nullptr ? selected->name : "Scene camera", juce::dontSendNotification);
        }
        const int slot = transform().getSlot();
        expose.setTooltip("Assign this object or camera to a project-wide DAW automation slot. Each slot exposes its transform controls and stays bound across scene changes.");
        expose.setButtonText(slot < 0 ? "Expose to DAW" : "Automation: slot " + juce::String(slot + 1));
        for (int i = 0; i < scene::controlCount; ++i) {
            controls[i].setVisible(selected != nullptr || i < 7);
            if (!controls[i].isMouseButtonDown()) {
                controls[i].setValue(transform().get(i), juce::dontSendNotification);
            }
        }
        list.updateContent();
        repaint();
        canvas.repaint();
    }
    void timerCallback() override {
        if (processor.getFileController().getScene(fileIndex) != model) {
            stopTimer();
            canvas.setNavigating(false);
            return;
        }
        canvas.updateNavigation();
        if (canvas.isNavigating()) {
            for (int i = 0; i < scene::controlCount; ++i) { controls[i].setValue(model->camera.get(i), juce::dontSendNotification); }
            return;
        }
        if (!editing) { refresh(); } else { canvas.repaint(); }
    }
    int getNumRows() override { return (int)model->objects.size() + 1; }
    juce::String getNameForRow(int row) override {
        return row > 0 && row <= (int)model->objects.size() ? model->objects[(size_t)row - 1]->name : "Camera";
    }
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool highlighted) override {
        if (highlighted) {
            g.setColour(osci::Colours::accentColor().withAlpha(0.18f));
            g.fillRoundedRectangle(juce::Rectangle<float>(0, 2, (float)width, (float)height - 4), 5);
        }
        g.setColour(highlighted ? osci::Colours::text() : osci::Colours::textMuted());
        g.setFont(juce::Font(juce::FontOptions(12.0f)));
        const auto object = row > 0 && row <= (int)model->objects.size() ? model->objects[(size_t)row - 1] : nullptr;
        const auto name = object != nullptr ? object->name : "Camera";
        g.drawText(name, 10, 0, width - 62, height, juce::Justification::centredLeft, true);
    }
    class ObjectRow final : public juce::Component {
    public:
        ObjectRow(SceneEditor& owner)
            : owner(owner),
              pencil("Edit source", BinaryData::pencil_svg, osci::Colours::textMuted(), osci::Colours::text()),
              remove("Delete object", BinaryData::delete_svg, osci::Colours::textMuted(), juce::Colours::red) {
            setWantsKeyboardFocus(true);
            addChildComponent(pencil);
            pencil.setTooltip("Edit source in the Editor tab");
            pencil.onClick = [this] { this->owner.list.selectRow(row); this->owner.sourceEditor(); };
            addChildComponent(remove);
            remove.setTooltip("Delete object");
            remove.onClick = [this] {
                this->owner.list.selectRow(row);
                this->owner.removeSelected();
            };
        }
        void update(int index) {
            row = index;
            auto object = row > 0 && row <= (int)owner.model->objects.size() ? owner.model->objects[row - 1] : nullptr;
            pencil.setVisible(object != nullptr && (osci::files::isCodeEditable(object->name) || object->liveKind == "texture"));
            pencil.setButtonText(object != nullptr ? "Edit " + object->name : "Edit source");
            pencil.setName(pencil.getButtonText());
            remove.setVisible(object != nullptr);
            repaint();
        }
        void resized() override {
            remove.setBounds(getWidth() - 26, 7, 20, 20);
            pencil.setBounds(getWidth() - 50, 7, 20, 20);
        }
        void paint(juce::Graphics&) override {}
        void mouseDown(const juce::MouseEvent& e) override {
            grabKeyboardFocus();
            owner.listBoxItemClicked(row, e);
        }
        void mouseDoubleClick(const juce::MouseEvent& e) override { owner.listBoxItemDoubleClicked(row, e); }
        bool keyPressed(const juce::KeyPress& key) override { return owner.keyPressed(key); }
    private:
        SceneEditor& owner;
        osci::SvgButton pencil, remove;
        int row = 0;
    };
    juce::Component* refreshComponentForRow(int row, bool, juce::Component* existing) override {
        if (row < 0 || row >= getNumRows()) { delete existing; return nullptr; }
        auto* component = static_cast<ObjectRow*>(existing);
        if (component == nullptr) { component = new ObjectRow(*this); }
        component->update(row);
        return component;
    }
    void selectedRowsChanged(int row) override {
        endEdit();
        auto next = row > 0 && row <= (int)model->objects.size() ? model->objects[(size_t)row - 1] : nullptr;
        if (next != selected) {
            selected = next;
            resized();
        }
        refresh();
    }
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override { list.selectRow(row); sourceEditor(); }
    void listBoxItemClicked(int row, const juce::MouseEvent& event) override {
        list.selectRow(row);
        if (event.mods.isPopupMenu()) {
            objectMenu();
        }
    }
    void select(const std::shared_ptr<scene::Object>& object) {
        endEdit();
        list.updateContent();
        selected = object;
        const auto it = std::find(model->objects.begin(), model->objects.end(), object);
        list.selectRow(object == nullptr ? 0 : 1 + (int)std::distance(model->objects.begin(), it));
        refresh();
    }
    void showMenu(juce::PopupMenu menu, juce::Component& anchor, std::function<void(int)> callback) {
        auto screenPosition = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().roundToInt();
        if (screenPosition == juce::Point<int>()) {
            screenPosition = anchor.localPointToGlobal(juce::Point<int>(4, anchor.getHeight()));
        }
        osci::showContextMenuAsync(std::move(menu), screenPosition, this, std::move(callback));
    }
    void exposureMenu() {
        juce::PopupMenu menu;
        const int slot = transform().getSlot();
        if (slot < 0) {
            const int free = (int)std::count(processor.getFileController().sceneAutomation.targets.begin(), processor.getFileController().sceneAutomation.targets.end(), juce::String());
            menu.addItem(1, "Expose to DAW (" + juce::String(free) + " slots available)", free > 0);
        } else {
            menu.addSectionHeader("Project-wide slot " + juce::String(slot + 1));
            menu.addItem(2, "Stop exposing to DAW");
        }
        for (int s = 0; s < scene::slotCount; ++s) {
            const auto target = processor.getFileController().sceneAutomation.targets[s];
            if (target.isEmpty()) { continue; }
            bool found = false;
            for (int i = 0; i < processor.getFileController().size(); ++i) {
                const auto candidate = processor.getFileController().getScene(i);
                if (candidate == nullptr) { continue; }
                if (candidate->camera.id == target) { found = true; }
                for (const auto& object : candidate->objects) {
                    if (object->transform.id == target) { found = true; }
                }
            }
            if (!found) { menu.addItem(100 + s, "Release unused slot " + juce::String(s + 1)); }
        }
        auto safe = juce::Component::SafePointer<SceneEditor>(this);
        showMenu(std::move(menu), expose, [safe](int result) {
            if (safe == nullptr) { return; }
            if (result == 1) { safe->transform().expose(); }
            if (result == 2) { safe->transform().unexpose(); }
            if (result >= 100 && result < 100 + scene::slotCount) {
                auto& bank = safe->processor.getFileController().sceneAutomation;
                bank.generations[result - 100].fetch_add(1);
                bank.targets[result - 100].clear();
            }
            safe->refresh();
        });
    }
    void objectMenu() {
        if (selected == nullptr && juce::JUCEApplicationBase::isStandaloneApp()) { return; }
        juce::PopupMenu menu;
        if (!juce::JUCEApplicationBase::isStandaloneApp()) {
            menu.addItem(1, transform().getSlot() < 0 ? "Expose to DAW" : "Manage DAW exposure...");
        }
        menu.addItem(4, "Edit source", selected != nullptr && (osci::files::isCodeEditable(selected->name) || selected->liveKind == "texture"));
        menu.addItem(2, "Duplicate object", selected != nullptr);
        menu.addItem(3, "Delete object", selected != nullptr);
        auto safe = juce::Component::SafePointer<SceneEditor>(this);
        showMenu(std::move(menu), list, [safe](int result) {
            if (safe == nullptr) { return; }
            if (result == 1) { safe->exposureMenu(); }
            if (result == 4) { safe->sourceEditor(); }
            if (result == 2 && safe->selected != nullptr) {
                auto original = safe->selected;
                if (original->liveKind.isNotEmpty()) {
                    auto copy = safe->processor.getFileController().addLiveSceneObject(safe->fileIndex, original->liveKind == "blender");
                    safe->select(copy);
                } else {
                    auto data = std::make_shared<juce::MemoryBlock>(*original->data);
                    safe->addObject(original->name, data);
                }
                juce::XmlElement xml("transform");
                original->transform.save(xml);
                safe->selected->transform.load(xml, true);
                safe->selected->transform.set(0, original->transform.get(0) + 0.2f);
            }
            if (result == 3) { safe->removeSelected(); }
        });
    }
    struct ObjectEdit : juce::UndoableAction {
        ObjectEdit(std::shared_ptr<scene::Scene> s, std::shared_ptr<scene::Object> o, size_t i, bool adding)
            : scene(std::move(s)), object(std::move(o)), index(i), adding(adding) {}
        bool apply(bool add) {
            juce::SpinLock::ScopedLockType guard(scene->lock);
            auto& objects = scene->objects;
            const auto found = std::find(objects.begin(), objects.end(), object);
            if (add && found == objects.end()) {
                objects.insert(objects.begin() + juce::jmin(index, objects.size()), object);
            } else if (!add && found != objects.end()) {
                objects.erase(found);
            }
            return true;
        }
        bool perform() override { return apply(adding); }
        bool undo() override { return apply(!adding); }
        std::shared_ptr<scene::Scene> scene;
        std::shared_ptr<scene::Object> object;
        size_t index;
        bool adding;
    };
    void removeSelected() {
        if (selected == nullptr) { return; }
        const auto position = std::find(model->objects.begin(), model->objects.end(), selected);
        processor.getUndoManager().beginNewTransaction("Remove scene object");
        processor.getUndoManager().perform(new ObjectEdit(model, selected, (size_t)std::distance(model->objects.begin(), position), false));
        select(model->objects.empty() ? nullptr : model->objects.front());
        processor.getFileController().sceneChanged();
    }
    void addObject(const juce::String& name, std::shared_ptr<juce::MemoryBlock> data) {
        auto object = processor.getFileController().addSceneObject(fileIndex, name, std::move(data));
        if (object != nullptr) {
            processor.getUndoManager().beginNewTransaction("Add scene object");
            processor.getUndoManager().perform(new ObjectEdit(model, object, model->objects.size() - 1, true));
            select(object);
        }
    }
    void sourceEditor() {
        if (selected != nullptr && selected->liveKind == "texture") {
            auto sources = osci::texture::listOpenGLSources();
            juce::PopupMenu menu;
            for (int i = 0; i < (int)sources.size(); ++i) {
                menu.addItem(i + 1, sources[i].displayName, sources[i].connectable);
            }
            if (sources.empty()) { menu.addItem(1000, "No texture sources available", false); }
            auto safe = juce::Component::SafePointer<SceneEditor>(this);
            showMenu(std::move(menu), list, [safe, sources](int result) {
                if (safe != nullptr && result > 0 && result <= (int)sources.size() && safe->connectTexture) {
                    safe->connectTexture(sources[result - 1]);
                }
            });
            return;
        }
        if (selected != nullptr && osci::files::isCodeEditable(selected->name) && onEditSource) {
            onEditSource(selected);
        }
    }
    void frameSelection() {
        auto object = selected;
        osci::Point low(20, 20, 20), high(-20, -20, -20);
        bool found = false;
        auto include = [&](osci::Point point) {
            for (int i = 0; i < 3; ++i) { low[i] = juce::jmin(low[i], point[i]); high[i] = juce::jmax(high[i], point[i]); }
            found = true;
        };
        for (const auto& candidate : model->objects) {
            if (object != nullptr && candidate != object) { continue; }
            std::vector<scene::Object::PreviewPoint> preview;
            {
                juce::SpinLock::ScopedLockType guard(candidate->geometryLock);
                preview = candidate->preview;
            }
            for (const auto& vertex : preview) { include(candidate->transform.apply(vertex.point)); }
            if (preview.empty()) {
                include(candidate->transform.apply({ -1, -1, 0 }));
                include(candidate->transform.apply({ 1, 1, 0 }));
            }
        }
        select(nullptr);
        beginEdit();
        const auto centre = found ? (low + high) * 0.5f : osci::Point();
        for (int i = 0; i < 3; ++i) { model->camera.set(i, centre[i]); }
        auto diagonal = high - low;
        model->camera.set(6, juce::jlimit(0.05f, 20.0f, found ? diagonal.magnitude() * 0.6f : 1.0f));
        endEdit();
        select(object);
    }

    class Canvas final : public juce::Component {
    public:
        explicit Canvas(SceneEditor& editor) : owner(editor) {
            setName("Scene 3D preview");
            setWantsKeyboardFocus(true);
            setMouseCursor(juce::MouseCursor::CrosshairCursor);
            const char* names[] = { "Front", "Side", "Top" };
            for (int i = 0; i < 3; ++i) {
                addAndMakeVisible(views[i]);
                views[i].setButtonText(names[i]);
                views[i].setName(juce::String(names[i]) + " camera view");
                views[i].setTooltip("Align the scene camera. This changes the output view.");
                views[i].onClick = [this, i] {
                    auto object = owner.selected;
                    owner.select(nullptr);
                    owner.beginEdit();
                    owner.model->camera.set(3, i == 2 ? 90 : 0);
                    owner.model->camera.set(4, i == 1 ? 90 : 0);
                    owner.model->camera.set(5, 0);
                    owner.endEdit();
                    owner.select(object);
                };
            }
        }
        bool isNavigating() const { return navigating; }
        void setNavigating(bool enabled) {
            if (navigating == enabled) { return; }
            navigating = enabled;
            owner.navigate.setToggleState(enabled, juce::dontSendNotification);
            owner.navigate.setButtonText(enabled ? "Done" : "Navigate");
            if (enabled) {
                navigationSelection = owner.selected;
                owner.select(nullptr);
                owner.beginEdit();
                navigationTick = juce::Time::getMillisecondCounterHiRes();
                navigationMotion = {};
                savedCursor = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition();
                setMouseCursor(juce::MouseCursor::NoCursor);
                grabKeyboardFocus();
                centreCursor();
                auto safeThis = juce::Component::SafePointer<Canvas>(this);
                juce::MessageManager::callAsync([safeThis] {
                    if (safeThis != nullptr && safeThis->navigating) {
                        safeThis->grabKeyboardFocus();
                        safeThis->centreCursor();
                    }
                });
            } else {
                setMouseCursor(juce::MouseCursor::CrosshairCursor);
                juce::Desktop::getInstance().getMainMouseSource().setScreenPosition(savedCursor);
                owner.endEdit();
                owner.select(navigationSelection);
                navigationSelection.reset();
            }
            owner.repaint();
            repaint();
        }
        void centreCursor() {
            juce::Desktop::getInstance().getMainMouseSource().setScreenPosition(localPointToGlobal(getLocalBounds().getCentre().toFloat()));
        }
        void mouseMove(const juce::MouseEvent& event) override {
            if (!navigating) { return; }
            const auto delta = event.position - getLocalBounds().getCentre().toFloat();
            if (delta.getDistanceFromOrigin() < 0.5f) { return; }
            auto& camera = owner.model->camera;
            // The projection eye is behind the camera target. Keep that eye
            // fixed while looking, rather than orbiting around the target.
            auto offset = eyeOffset();
            const auto eye = osci::Point(camera.get(0) + offset.x, camera.get(1) + offset.y, camera.get(2) + offset.z);
            camera.set(3, juce::jlimit(-89.0f, 89.0f, camera.get(3) + delta.y * 0.2f));
            camera.set(4, std::remainder(camera.get(4) + delta.x * 0.2f, 360.0f));
            offset = eyeOffset();
            for (int i = 0; i < 3; ++i) { camera.set(i, juce::jlimit(-20.0f, 20.0f, eye[i] - offset[i])); }
            centreCursor();
            repaint();
        }
        bool navigationKey(const juce::KeyPress& key) {
            const int code = key.getKeyCode();
            if (code == juce::KeyPress::escapeKey) { setNavigating(false); return true; }
            if (code == 'W' || code == 'S' || code == 'A' || code == 'D' || code == 'Q' || code == 'E'
                || code == juce::KeyPress::upKey || code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) {

                return true;
            }
            return true;
        }
        osci::Point eyeOffset() const {
            const auto& camera = owner.model->camera;
            const float fov = juce::degreesToRadians(juce::jlimit(1.5f, 179.0f, owner.processor.perspective->getActualValue(1)));
            osci::Point offset(0, 0, -camera.get(6) / std::sin(fov * 0.5f));
            offset.rotate(juce::degreesToRadians(camera.get(3)), juce::degreesToRadians(camera.get(4)), juce::degreesToRadians(camera.get(5)));
            return offset;
        }
        void moveCamera(osci::Point movement) {
            auto& camera = owner.model->camera;
            movement.scale(camera.get(6), camera.get(6), camera.get(6));
            movement.rotate(juce::degreesToRadians(camera.get(3)), juce::degreesToRadians(camera.get(4)), juce::degreesToRadians(camera.get(5)));
            for (int i = 0; i < 3; ++i) { camera.set(i, juce::jlimit(-20.0f, 20.0f, camera.get(i) + movement[i])); }
            repaint();
        }
        void updateNavigation() {
            if (!navigating) { return; }
            const double now = juce::Time::getMillisecondCounterHiRes();
            const float dt = (float)juce::jlimit(0.0, 0.05, (now - navigationTick) / 1000.0);
            navigationTick = now;
            const auto down = [](int letter, int arrow = 0) {
                return juce::KeyPress::isKeyCurrentlyDown(letter) || (arrow != 0 && juce::KeyPress::isKeyCurrentlyDown(arrow));
            };
            osci::Point direction((float)(down('D', juce::KeyPress::rightKey) - down('A', juce::KeyPress::leftKey)),
                (float)(down('E') - down('Q')), (float)(down('W', juce::KeyPress::upKey) - down('S', juce::KeyPress::downKey)));
            const float speed = juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown() ? 3.0f : 1.0f;
            moveCamera(navigationMotion.advance(direction, dt, speed));
        }
        void resized() override {
            auto buttons = getLocalBounds().reduced(10).removeFromTop(22).removeFromRight(144);
            for (auto& button : views) { button.setBounds(buttons.removeFromLeft(48).reduced(2, 0)); }
        }
        float unit() const { return juce::jmax(1.0f, juce::jmin(getWidth(), getHeight()) * 0.39f); }
        juce::Point<float> screen(osci::Point p) const {
            p = owner.model->camera.view(p);
            const auto projected = projector.project({ p.x, p.y, p.z });
            p.x += projectionStrength * (projected.x - p.x);
            p.y += projectionStrength * (projected.y - p.y);
            return { getWidth() * 0.5f + p.x * unit(), getHeight() * 0.5f - p.y * unit() };
        }
        osci::Point centre() const {
            return owner.selected != nullptr ? osci::Point(owner.selected->transform.get(0), owner.selected->transform.get(1), owner.selected->transform.get(2)) : osci::Point();
        }
        void paint(juce::Graphics& g) override {
            projectionStrength = owner.processor.perspective->getActualValue(0);
            const float fov = juce::degreesToRadians(juce::jlimit(1.5f, 179.0f, owner.processor.perspective->getActualValue(1)));
            projector.setFieldOfViewRadians(fov);
            projector.setCameraPosition({ 0, 0, -1.0f / std::sin(fov * 0.5f) });
            g.fillAll(osci::Colours::veryDark());
            const auto outputFrame = juce::Rectangle<float>(getWidth() * 0.5f - unit(), getHeight() * 0.5f - unit(), unit() * 2.0f, unit() * 2.0f);
            g.setColour(osci::Colours::text().withAlpha(0.18f));
            g.drawRoundedRectangle(outputFrame, 2.0f, 1.0f);
            g.setColour(osci::Colours::text().withAlpha(0.09f));
            const float grid = 0.5f;
            for (int i = -8; i <= 8; ++i) {
                g.drawLine(juce::Line<float>(screen({ i * grid, -4, 0 }), screen({ i * grid, 4, 0 })), 0.6f);
                g.drawLine(juce::Line<float>(screen({ -4, i * grid, 0 }), screen({ 4, i * grid, 0 })), 0.6f);
            }
            paths.clear();
            for (const auto& object : owner.model->objects) {
                juce::Path path;
                std::vector<juce::Line<float>> segments;
                juce::Point<float> previous;
                bool hasPrevious = false;
                auto appendPoint = [&](juce::Point<float> point, bool start) {
                    if (start || !hasPrevious) {
                        path.startNewSubPath(point);
                    } else {
                        path.lineTo(point);
                        segments.emplace_back(previous, point);
                    }
                    previous = point;
                    hasPrevious = true;
                };
                if (object->parser != nullptr && object->parser->isSample()) {
                    std::array<osci::Point, 1024> trace;
                    int count = 0;
                    {
                        juce::SpinLock::ScopedLockType guard(owner.model->lock);
                        count = object->traceSize;
                        for (int i = 0; i < count; ++i) {
                            trace[i] = object->trace[(object->traceWrite - count + i + 1024) % 1024];
                        }
                    }
                    for (int i = 0; i < count; ++i) {
                        appendPoint(screen(object->transform.apply(trace[i])), i == 0);
                    }
                } else {
                    std::vector<scene::Object::PreviewPoint> preview;
                    auto source = object->liveSource != nullptr ? object->liveSource : object;
                    {
                        juce::SpinLock::ScopedLockType guard(source->geometryLock);
                        preview = source->preview;
                    }
                    for (const auto& vertex : preview) {
                        appendPoint(screen(object->transform.apply(vertex.point)), vertex.start);
                    }
                }
                const bool selected = object == owner.selected;
                const auto colour = selected ? osci::Colours::accentColor() : osci::Colours::text();
                for (const auto& segment : segments) {
                    const float brightness = juce::jlimit(0.025f, 1.0f, 14.0f / juce::jmax(14.0f, segment.getLength()));
                    g.setColour(colour.withAlpha(brightness * (selected ? 0.12f : 0.07f)));
                    g.drawLine(segment, selected ? 5.0f : 3.0f);
                    g.setColour(colour.withAlpha(brightness * (selected ? 1.0f : 0.72f)));
                    g.drawLine(segment, selected ? 1.6f : 1.1f);
                }
                paths.emplace_back(object, std::move(path));
            }
            drawGizmo(g);
            g.setColour(osci::Colours::textMuted());
            g.setFont(juce::Font(juce::FontOptions(10.5f)));
            if (getWidth() > 360) { g.drawText("SCENE PREVIEW", getLocalBounds().reduced(12).removeFromTop(16), juce::Justification::centredLeft); }
            if (navigating) {
                const auto centre = getLocalBounds().getCentre().toFloat();
                g.setColour(osci::Colours::text().withAlpha(0.65f));
                g.drawLine(centre.x - 5, centre.y, centre.x + 5, centre.y, 1.0f);
                g.drawLine(centre.x, centre.y - 5, centre.x, centre.y + 5, 1.0f);
            }
            g.drawText(navigating ? "Mouse captured / Esc to exit" : "Camera moves change output", getLocalBounds().reduced(12).removeFromBottom(16), juce::Justification::centredRight);
            if (owner.model->objects.empty()) {
                g.setFont(juce::Font(juce::FontOptions(15.0f)));
                g.drawText("Drop files here, or add an object", getLocalBounds(), juce::Justification::centred);
            }
        }
        void drawGizmo(juce::Graphics& g) {
            handles = {};
            if (owner.selected == nullptr) { return; }
            const auto origin = centre();
            const auto c = screen(origin);
            const float worldSize = 66.0f / unit() * owner.model->camera.get(6);
            for (int axis = 0; axis < 3; ++axis) {
                auto end = origin;
                end[axis] += worldSize;
                if (owner.tool == 1) {
                    for (int j = 0; j <= 64; ++j) {
                        auto p = origin;
                        const float angle = juce::MathConstants<float>::twoPi * j / 64.0f;
                        p[(axis + 1) % 3] += worldSize * std::cos(angle);
                        p[(axis + 2) % 3] += worldSize * std::sin(angle);
                        if (j == 0) { handles[axis].startNewSubPath(screen(p)); } else { handles[axis].lineTo(screen(p)); }
                    }
                } else {
                    const auto endpoint = screen(end);
                    handles[axis].startNewSubPath(c);
                    handles[axis].lineTo(endpoint);
                    g.setColour(axisColour(axis));
                    if (owner.tool == 2) {
                        g.fillRect(juce::Rectangle<float>(endpoint.x - 4, endpoint.y - 4, 8, 8));
                    } else {
                        g.drawArrow(juce::Line<float>(c, endpoint), 2.0f, 9.0f, 9.0f);
                    }
                    g.drawText(juce::String::charToString("XYZ"[axis]), juce::Rectangle<float>(endpoint.x - 8, endpoint.y - 22, 16, 15), juce::Justification::centred);
                }
                g.setColour(axisColour(axis));
                g.strokePath(handles[axis], juce::PathStrokeType(2.0f));
            }
            g.setColour(osci::Colours::text());
            g.fillRoundedRectangle(c.x - 4, c.y - 4, 8, 8, 2);
        }
        int hitHandle(juce::Point<float> p) const {
            if (owner.selected == nullptr) { return -2; }
            if (p.getDistanceFrom(screen(centre())) < 10) { return -1; }
            int closest = -2;
            float distance = 9;
            for (int axis = 0; axis < 3; ++axis) {
                juce::Point<float> nearest;
                handles[axis].getNearestPoint(p, nearest);
                const float d = p.getDistanceFrom(nearest);
                if (!handles[axis].isEmpty() && d < distance) { closest = axis; distance = d; }
            }
            return closest;
        }
        void mouseDown(const juce::MouseEvent& event) override {
            grabKeyboardFocus();
            start = event.position;
            if (navigating) {
                initial = owner.values();
                return;
            }
            axis = hitHandle(start);
            cameraDrag = axis == -2 || event.mods.isAltDown() || event.mods.isMiddleButtonDown();
            previousSelection = owner.selected;
            if (cameraDrag && !event.mods.isAltDown() && !event.mods.isMiddleButtonDown() && !event.mods.isShiftDown()) {
                for (auto it = paths.rbegin(); it != paths.rend(); ++it) {
                    juce::Point<float> nearest;
                    it->second.getNearestPoint(start, nearest);
                    if (!it->second.isEmpty() && start.getDistanceFrom(nearest) < 8) {
                        owner.select(it->first);
                        axis = -1;
                        cameraDrag = false;
                        break;
                    }
                }
            }
            if (event.mods.isPopupMenu()) { owner.objectMenu(); return; }
            if (cameraDrag) { owner.select(nullptr); }
            owner.beginEdit();
            initial = owner.values();
        }
        void mouseDrag(const juce::MouseEvent& event) override {
            if (!owner.editing || event.mods.isPopupMenu()) { return; }
            const auto delta = event.position - start;
            if (navigating) { mouseMove(event); return; }
            auto& t = owner.transform();
            if (cameraDrag) {
                if (event.mods.isShiftDown() || event.mods.isMiddleButtonDown()) {
                    auto movement = osci::Point(-delta.x / unit(), delta.y / unit(), 0);
                    movement.scale(initial[6], initial[6], initial[6]);
                    movement.rotate(juce::degreesToRadians(initial[3]), juce::degreesToRadians(initial[4]), juce::degreesToRadians(initial[5]));
                    for (int i = 0; i < 3; ++i) { t.set(i, initial[i] + movement[i]); }
                } else {
                    t.set(3, juce::jlimit(-180.0f, 180.0f, initial[3] + delta.y * 0.45f));
                    t.set(4, juce::jlimit(-180.0f, 180.0f, initial[4] + delta.x * 0.45f));
                }
            } else if (owner.tool == 1) {
                const int control = 3 + (axis >= 0 ? axis : 2);
                const auto pivot = screen(centre());
                const float startAngle = std::atan2(start.y - pivot.y, start.x - pivot.x);
                const float angle = std::atan2(event.position.y - pivot.y, event.position.x - pivot.x);
                const float change = std::remainder(angle - startAngle, juce::MathConstants<float>::twoPi);
                t.set(control, juce::jlimit(-180.0f, 180.0f, initial[control] - juce::radiansToDegrees(change)));
            } else if (owner.tool == 2) {
                const float factor = std::exp((delta.x - delta.y) * 0.01f);
                for (int i = 0; i < 3; ++i) {
                    if (axis < 0 || axis == i) { t.set(6 + i, juce::jlimit(0.01f, 20.0f, initial[6 + i] * factor)); }
                }
            } else {
                auto movement = osci::Point(delta.x / unit(), -delta.y / unit(), 0);
                const auto& camera = owner.model->camera;
                movement.scale(camera.get(6), camera.get(6), camera.get(6));
                movement.rotate(juce::degreesToRadians(camera.get(3)), juce::degreesToRadians(camera.get(4)), juce::degreesToRadians(camera.get(5)));
                if (axis >= 0) {
                    auto endpoint = centre();
                    endpoint[axis] += 1.0f;
                    const auto direction = screen(endpoint) - screen(centre());
                    const float denominator = direction.x * direction.x + direction.y * direction.y;
                    if (denominator > 1.0f) {
                        const float distance = (delta.x * direction.x + delta.y * direction.y) / denominator;
                        t.set(axis, juce::jlimit(-20.0f, 20.0f, initial[axis] + distance));
                    }
                } else {
                    for (int i = 0; i < 3; ++i) { t.set(i, juce::jlimit(-20.0f, 20.0f, initial[i] + movement[i])); }
                }
            }
            repaint();
        }
        void mouseUp(const juce::MouseEvent&) override {
            if (navigating) { return; }
            owner.endEdit();
            if (cameraDrag) { owner.select(previousSelection); }
        }
        void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override {
            if (navigating) {
                moveCamera({ 0, 0, wheel.deltaY * 1.6f });
                return;
            }
            auto object = owner.selected;
            owner.select(nullptr);
            owner.beginEdit();
            auto& camera = owner.model->camera;
            camera.set(6, juce::jlimit(0.05f, 20.0f, camera.get(6) * std::exp(-wheel.deltaY * 1.6f)));
            owner.endEdit();
            owner.select(object);
        }
        bool keyPressed(const juce::KeyPress& key) override { return owner.keyPressed(key); }
    private:
        SceneEditor& owner;
        std::array<juce::TextButton, 3> views;
        juce::Point<float> start;
        std::array<float, 9> initial;
        std::array<juce::Path, 3> handles;
        std::vector<std::pair<std::shared_ptr<scene::Object>, juce::Path>> paths;
        std::shared_ptr<scene::Object> previousSelection;
        int axis = -2;
        bool cameraDrag = false;
        bool navigating = false;
        double navigationTick = 0;
        scene::NavigationMotion navigationMotion;
        osci::PerspectiveProjector projector;
        float projectionStrength = 1.0f;
        juce::Point<float> savedCursor;
        std::shared_ptr<scene::Object> navigationSelection;
    };

    std::function<void(osci::texture::SourceInfo)> connectTexture;
    OscirenderAudioProcessor& processor;
    int fileIndex;
    std::shared_ptr<scene::Scene> model;
    std::shared_ptr<scene::Object> selected;
    osci::PanelHeader objectHeader{"Objects"}, previewHeader, propertiesHeader;
    Canvas canvas;
    class ObjectList final : public juce::ListBox {
    public:
        explicit ObjectList(SceneEditor& owner) : owner(owner) {}
        bool keyPressed(const juce::KeyPress& key) override {
            if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
                return owner.keyPressed(key);
            }
            return juce::ListBox::keyPressed(key);
        }
    private:
        SceneEditor& owner;
    } list;
    juce::TextButton move, rotate, scale, frame, reset, expose, navigate, addObjectButton;
    juce::Label title;
    class NumberField final : public juce::Slider {
    public:
        NumberField() {
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            addChildComponent(input);
            input.setJustification(juce::Justification::centred);
            input.onReturnKey = [this] { commit(); };
            input.onEscapeKey = [this] { input.setVisible(false); };
            input.onFocusLost = [this] { commit(); };
        }
        void paint(juce::Graphics& g) override {
            g.setColour(osci::Colours::veryDark().withAlpha(isMouseOverOrDragging() ? 0.65f : 0.85f));
            g.fillRoundedRectangle(getLocalBounds().toFloat(), 4);
            g.setColour(osci::Colours::text());
            g.setFont(juce::Font(juce::FontOptions(13.0f)));
            g.drawText(getTextFromValue(getValue()), getLocalBounds().reduced(2, 0), juce::Justification::centred);
        }
        void resized() override { input.setBounds(getLocalBounds()); }
        void mouseDown(const juce::MouseEvent&) override {
            origin = getValue();
            if (onDragStart) { onDragStart(); }
        }
        void mouseDrag(const juce::MouseEvent& e) override {
            const double sensitivity = getName().startsWith("Rotation") ? 0.5 : 0.01;
            setValue(origin + (e.getDistanceFromDragStartX() - e.getDistanceFromDragStartY()) * sensitivity * (e.mods.isShiftDown() ? 0.1 : 1.0), juce::sendNotificationSync);
        }
        void mouseUp(const juce::MouseEvent&) override { if (onDragEnd) { onDragEnd(); } }
        void mouseDoubleClick(const juce::MouseEvent&) override {
            input.setText(juce::String(getValue(), 3));
            input.setVisible(true);
            input.grabKeyboardFocus();
            input.selectAll();
        }
        void commit() {
            if (input.isVisible()) {
                const auto text = input.getText().trim();
                input.setVisible(false);
                if (text.isNotEmpty() && text.containsAnyOf("0123456789")) { setValue(text.getDoubleValue(), juce::sendNotificationSync); }
            }
        }
        osci::TextEditor input;
        double origin = 0;
    };
    std::array<NumberField, scene::controlCount> controls;
    std::array<float, scene::controlCount> before;
    juce::Rectangle<int> objectPanelBounds, propertiesPanelBounds, listPanel, inspectorBounds, previewPanel, footer;
    std::array<juce::Rectangle<int>, 3> propertyLabels;
    std::array<juce::Rectangle<int>, 9> axisLabels;
    int tool = 0;
    bool editing = false;
};
