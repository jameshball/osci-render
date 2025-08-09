#include "EffectsListComponent.h"
#include "SvgButton.h"
#include "../PluginEditor.h"
#include "../LookAndFeel.h"

EffectsListComponent::EffectsListComponent(DraggableListBox& lb, AudioEffectListBoxItemData& data, int rn, osci::Effect& effect) : DraggableListBoxItem(lb, data, rn),
effect(effect), audioProcessor(data.audioProcessor), editor(data.editor) {
    auto parameters = effect.parameters;
    for (int i = 0; i < parameters.size(); i++) {
        std::shared_ptr<EffectComponent> effectComponent = std::make_shared<EffectComponent>(effect, i);
        selected.setToggleState(effect.enabled == nullptr || effect.enabled->getValue(), juce::dontSendNotification);
        // using weak_ptr to avoid circular reference and memory leak
        std::weak_ptr<EffectComponent> weakEffectComponent = effectComponent;
        effectComponent->slider.setValue(parameters[i]->getValueUnnormalised(), juce::dontSendNotification);
        
        list.setEnabled(selected.getToggleState());
        selected.onClick = [this, weakEffectComponent] {
            if (auto effectComponent = weakEffectComponent.lock()) {
                auto data = (AudioEffectListBoxItemData&)modelData;
                juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
                data.setSelected(rowNum, selected.getToggleState());
                list.setEnabled(selected.getToggleState());
            }
            repaint();
        };
        effectComponent->updateToggleState = [this, i, weakEffectComponent] {
            if (auto effectComponent = weakEffectComponent.lock()) {
                selected.setToggleState(effectComponent->effect.enabled == nullptr || effectComponent->effect.enabled->getValue(), juce::dontSendNotification);
                list.setEnabled(selected.getToggleState());
            }
            repaint();
        };

        auto component = createComponent(parameters[i]);
        if (component != nullptr) {
            effectComponent->setComponent(component);
        }

        listModel.addComponent(effectComponent);
    }

    list.setColour(effectComponentBackgroundColourId, juce::Colours::transparentBlack.withAlpha(0.2f));
    list.setModel(&listModel);
    list.setRowHeight(ROW_HEIGHT);
    list.updateContent();
    addAndMakeVisible(list);
    addAndMakeVisible(selected);
}

EffectsListComponent::~EffectsListComponent() {
    list.setLookAndFeel(nullptr);
}

void EffectsListComponent::paint(juce::Graphics& g) {
    auto bounds = getLocalBounds().removeFromLeft(LEFT_BAR_WIDTH);
    g.setColour(findColour(effectComponentHandleColourId));
    bounds.removeFromBottom(PADDING);
    juce::Path path;
    path.addRoundedRectangle(bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), OscirenderLookAndFeel::RECT_RADIUS, OscirenderLookAndFeel::RECT_RADIUS, true, false, true, false);
    g.fillPath(path);
    g.setColour(juce::Colours::white);
    // draw drag and drop handle using circles
    double size = 4;
    double leftPad = 4;
    double spacing = 7;
    double topPad = 6;
    double y = bounds.getHeight() / 2 - 15;
    g.fillEllipse(leftPad, y + topPad, size, size);
    g.fillEllipse(leftPad, y + topPad + spacing, size, size);
    g.fillEllipse(leftPad, y + topPad + 2 * spacing, size, size);
    g.fillEllipse(leftPad + spacing, y + topPad, size, size);
    g.fillEllipse(leftPad + spacing, y + topPad + spacing, size, size);
    g.fillEllipse(leftPad + spacing, y + topPad + 2 * spacing, size, size);
    DraggableListBoxItem::paint(g);
}

void EffectsListComponent::paintOverChildren(juce::Graphics& g) {
    g.setColour(juce::Colours::black.withAlpha(0.3f));
    auto bounds = list.getBounds();
    bounds.removeFromBottom(PADDING);
    juce::Path path;
    path.addRoundedRectangle(bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(), OscirenderLookAndFeel::RECT_RADIUS, OscirenderLookAndFeel::RECT_RADIUS, false, true, false, true);
    
    if (!selected.getToggleState()) {
        g.fillPath(path);
    }
}

void EffectsListComponent::resized() {
    auto area = getLocalBounds();
    auto leftBar = area.removeFromLeft(LEFT_BAR_WIDTH);
    leftBar.removeFromLeft(20);
    area.removeFromRight(PADDING);
    selected.setBounds(leftBar.withSizeKeepingCentre(30, 20));
    list.setBounds(area);
}

std::shared_ptr<juce::Component> EffectsListComponent::createComponent(osci::EffectParameter* parameter) {
    if (parameter->paramID == "customEffectStrength") {
        std::shared_ptr<SvgButton> button = std::make_shared<SvgButton>(parameter->name, BinaryData::pencil_svg, juce::Colours::white, juce::Colours::red);
        std::weak_ptr<SvgButton> weakButton = button;
        button->setEdgeIndent(5);
        button->setToggleState(editor.editingCustomFunction, juce::dontSendNotification);
        button->setTooltip("Toggles whether the text editor is editing the currently open file, or the custom Lua effect.");
        button->onClick = [this, weakButton] {
            if (auto button = weakButton.lock()) {
                editor.editCustomFunction(button->getToggleState());
            }
        };
        return button;
    }
    return nullptr;
}

int EffectsListBoxModel::getRowHeight(int row) {
    auto data = (AudioEffectListBoxItemData&)modelData;
    if (row == data.getNumItems() - 1) {
        // Last row is the "Add new effect" button
        return 44; // a tidy button height
    }
    return data.getEffect(row)->parameters.size() * EffectsListComponent::ROW_HEIGHT + EffectsListComponent::PADDING;
}

bool EffectsListBoxModel::hasVariableHeightRows() const {
    return true;
}

juce::Component* EffectsListBoxModel::refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component *existingComponentToUpdate) {
    auto data = (AudioEffectListBoxItemData&)modelData;
    
    if (juce::isPositiveAndBelow(rowNumber, data.getNumItems() - 1)) {
        // Regular effect component
        std::unique_ptr<EffectsListComponent> item(dynamic_cast<EffectsListComponent*>(existingComponentToUpdate));
        item = std::make_unique<EffectsListComponent>(listBox, (AudioEffectListBoxItemData&)modelData, rowNumber, *data.getEffect(rowNumber));
        return item.release();
    } else if (rowNumber == data.getNumItems() - 1) {
        // Last row becomes an "Add new effect" button
        auto* btn = dynamic_cast<juce::TextButton*>(existingComponentToUpdate);
        if (btn == nullptr)
            btn = new juce::TextButton("+ Add new effect");

        btn->setButtonText("+ Add new effect");
        auto onAdd = data.onAddNewEffectRequested; // copy to avoid dangling reference
        btn->onClick = [onAdd]() mutable {
            if (onAdd)
                onAdd();
        };
        return btn;
    }
    
    return nullptr;
}
