#include "EffectsListComponent.h"
#include "SvgButton.h"
#include "../PluginEditor.h"
#include "../LookAndFeel.h"

EffectsListComponent::EffectsListComponent(DraggableListBox& lb, AudioEffectListBoxItemData& data, int rn, osci::Effect& effect) : DraggableListBoxItem(lb, data, rn),
effect(effect), audioProcessor(data.audioProcessor), editor(data.editor) {
    auto parameters = effect.parameters;
    for (int i = 0; i < parameters.size(); i++) {
        std::shared_ptr<EffectComponent> effectComponent = std::make_shared<EffectComponent>(effect, i);
        enabled.setToggleState(effect.enabled == nullptr || effect.enabled->getValue(), juce::dontSendNotification);
        // using weak_ptr to avoid circular reference and memory leak
        std::weak_ptr<EffectComponent> weakEffectComponent = effectComponent;
        effectComponent->slider.setValue(parameters[i]->getValueUnnormalised(), juce::dontSendNotification);
        
        list.setEnabled(enabled.getToggleState());
        enabled.onClick = [this, weakEffectComponent] {
            if (auto effectComponent = weakEffectComponent.lock()) {
                auto data = (AudioEffectListBoxItemData&)modelData;
                juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
                data.setSelected(rowNum, enabled.getToggleState());
                list.setEnabled(enabled.getToggleState());
            }
            repaint();
        };
        effectComponent->updateToggleState = [this, i, weakEffectComponent] {
            if (auto effectComponent = weakEffectComponent.lock()) {
                enabled.setToggleState(effectComponent->effect.enabled == nullptr || effectComponent->effect.enabled->getValue(), juce::dontSendNotification);
                list.setEnabled(enabled.getToggleState());
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
    list.updateContent();
    addAndMakeVisible(list);
    addAndMakeVisible(enabled);

    closeButton.setEdgeIndent(2);
    closeButton.onClick = [this]() {
        // Flip flags under lock
        {
            juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
            if (this->effect.enabled) this->effect.enabled->setValueNotifyingHost(false);
            if (this->effect.selected) this->effect.selected->setValueNotifyingHost(false);
            // Reset all parameters/flags for this effect back to defaults on removal
            this->effect.resetToDefault();
        }
        // Defer model reset and outer list refresh to avoid re-entrancy on current row
        juce::MessageManager::callAsync([this]() {
            auto& data = static_cast<AudioEffectListBoxItemData&>(modelData);
            data.resetData();
            // Update the outer DraggableListBox, not the inner parameter list
            this->listBox.updateContent();
            // If there are no effects visible, open the grid
            if (data.getNumItems() == 0) {
                if (data.onAddNewEffectRequested)
                    data.onAddNewEffectRequested();
            }
        });
    };
    addAndMakeVisible(closeButton);
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

    auto rightBar = getLocalBounds().removeFromRight(RIGHT_BAR_WIDTH);
    rightBar.removeFromBottom(PADDING);
    g.setColour(findColour(effectComponentHandleColourId));
    juce::Path rightPath;
    rightPath.addRoundedRectangle(rightBar.getX(), rightBar.getY(), rightBar.getWidth(), rightBar.getHeight(), OscirenderLookAndFeel::RECT_RADIUS, OscirenderLookAndFeel::RECT_RADIUS, false, true, false, true);
    g.fillPath(rightPath);

    DraggableListBoxItem::paint(g);
}

void EffectsListComponent::paintOverChildren(juce::Graphics& g) {
    g.setColour(juce::Colours::black.withAlpha(0.3f));
    auto bounds = list.getBounds();
    bounds.removeFromBottom(PADDING);
    
    if (!enabled.getToggleState()) {
        g.fillRect(bounds);
    }
}

void EffectsListComponent::resized() {
    auto area = getLocalBounds();
    auto leftBar = area.removeFromLeft(LEFT_BAR_WIDTH);
    auto buttonBounds = area.removeFromRight(RIGHT_BAR_WIDTH);
    closeButton.setBounds(buttonBounds);
    closeButton.setImageTransform(juce::AffineTransform::translation(0, -2));
    leftBar.removeFromLeft(20);
    enabled.setBounds(leftBar.withSizeKeepingCentre(30, 20));
    // TODO: this is super slow
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
    return data.getEffect(row)->parameters.size() * EffectsListComponent::ROW_HEIGHT + EffectsListComponent::PADDING;
}

bool EffectsListBoxModel::hasVariableHeightRows() const {
    return true;
}

juce::Component* EffectsListBoxModel::refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component *existingComponentToUpdate) {
    auto data = (AudioEffectListBoxItemData&)modelData;
    if (! juce::isPositiveAndBelow(rowNumber, data.getNumItems()))
        return nullptr;
    std::unique_ptr<EffectsListComponent> item(dynamic_cast<EffectsListComponent*>(existingComponentToUpdate));
    item = std::make_unique<EffectsListComponent>(listBox, (AudioEffectListBoxItemData&)modelData, rowNumber, *data.getEffect(rowNumber));
    return item.release();
}
