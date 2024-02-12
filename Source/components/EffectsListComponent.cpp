#include "EffectsListComponent.h"
#include "SvgButton.h"
#include "../PluginEditor.h"

EffectsListComponent::EffectsListComponent(DraggableListBox& lb, AudioEffectListBoxItemData& data, int rn, Effect& effect) : DraggableListBoxItem(lb, data, rn), effect(effect), audioProcessor(data.audioProcessor), editor(data.editor) {
	auto parameters = effect.parameters;
	for (int i = 0; i < parameters.size(); i++) {
		std::shared_ptr<EffectComponent> effectComponent = std::make_shared<EffectComponent>(audioProcessor, effect, i);
		selected.setToggleState(effect.enabled == nullptr || effect.enabled->getValue(), juce::dontSendNotification);
		// using weak_ptr to avoid circular reference and memory leak
		std::weak_ptr<EffectComponent> weakEffectComponent = effectComponent;
		effectComponent->slider.setValue(parameters[i]->getValueUnnormalised(), juce::dontSendNotification);
		effectComponent->slider.onValueChange = [this, i, weakEffectComponent] {
			if (auto effectComponent = weakEffectComponent.lock()) {
				this->effect.setValue(i, effectComponent->slider.getValue());
			}
		};
		
		list.setEnabled(selected.getToggleState());
		selected.onClick = [this, weakEffectComponent] {
			if (auto effectComponent = weakEffectComponent.lock()) {
				auto data = (AudioEffectListBoxItemData&)modelData;
				juce::SpinLock::ScopedLockType lock(audioProcessor.effectsLock);
				data.setSelected(rowNum, selected.getToggleState());
                list.setEnabled(selected.getToggleState());
			}
		};

		auto component = createComponent(parameters[i]);
		if (component != nullptr) {
            effectComponent->setComponent(component);
        }

		listModel.addComponent(effectComponent);
	}

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
	auto bounds = getLocalBounds();
    g.setColour(findColour(effectComponentHandleColourId));
    bounds.removeFromBottom(2);
	g.fillRect(bounds);
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
	if (!selected.getToggleState()) {
        g.setColour(juce::Colours::black.withAlpha(0.3f));
		auto bounds = list.getBounds();
		bounds.removeFromBottom(2);
		g.fillRect(bounds);
		list.repaint();
    }
}

void EffectsListComponent::resized() {
	auto area = getLocalBounds();
	area.removeFromLeft(20);
	selected.setBounds(area.removeFromLeft(30));
	list.setBounds(area);
}

std::shared_ptr<juce::Component> EffectsListComponent::createComponent(EffectParameter* parameter) {
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
	return data.getEffect(row)->parameters.size() * EffectsListComponent::ROW_HEIGHT + 2;
}

bool EffectsListBoxModel::hasVariableHeightRows() const {
	return true;
}

juce::Component* EffectsListBoxModel::refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component *existingComponentToUpdate) {
    std::unique_ptr<EffectsListComponent> item(dynamic_cast<EffectsListComponent*>(existingComponentToUpdate));
    if (juce::isPositiveAndBelow(rowNumber, modelData.getNumItems())) {
		auto data = (AudioEffectListBoxItemData&)modelData;
        item = std::make_unique<EffectsListComponent>(listBox, (AudioEffectListBoxItemData&)modelData, rowNumber, *data.getEffect(rowNumber));
    }
    return item.release();
}
