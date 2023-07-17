#include "EffectsListComponent.h"

EffectsListComponent::EffectsListComponent(DraggableListBox& lb, AudioEffectListBoxItemData& data, int rn, std::shared_ptr<Effect> effect) : DraggableListBoxItem(lb, data, rn), effect(effect) {
	auto parameters = effect->parameters;
	for (int i = 0; i < parameters.size(); i++) {
		std::shared_ptr<EffectComponent> effectComponent = std::make_shared<EffectComponent>(*effect, i, i == 0);
		// using weak_ptr to avoid circular reference and memory leak
		std::weak_ptr<EffectComponent> weakEffectComponent = effectComponent;
		effectComponent->slider.setValue(parameters[i].value, juce::dontSendNotification);
		effectComponent->slider.onValueChange = [this, i, weakEffectComponent] {
			if (auto effectComponent = weakEffectComponent.lock()) {
				this->effect->setValue(i, effectComponent->slider.getValue());
			}
		};

		if (i == 0) {
			bool isSelected = false;

			{
				juce::SpinLock::ScopedLockType lock(data.audioProcessor.effectsLock);
				// check if effect is in audioProcessor enabled effects
				for (auto processorEffect : data.audioProcessor.enabledEffects) {
					if (processorEffect->getId() == effect->getId()) {
						isSelected = true;
						break;
					}
				}
			}
			effectComponent->selected.setToggleState(isSelected, juce::dontSendNotification);
			effectComponent->selected.onClick = [this, weakEffectComponent] {
				if (auto effectComponent = weakEffectComponent.lock()) {
					auto data = (AudioEffectListBoxItemData&)modelData;
					juce::SpinLock::ScopedLockType lock(data.audioProcessor.effectsLock);
					data.setSelected(rowNum, effectComponent->selected.getToggleState());
				}
			};
		}

		listModel.addComponent(effectComponent);
	}

	list.setModel(&listModel);
	list.setRowHeight(30);
	list.updateContent();
	addAndMakeVisible(list);
}

EffectsListComponent::~EffectsListComponent() {}

void EffectsListComponent::paint(juce::Graphics& g) {
	auto bounds = getLocalBounds();
	g.fillAll(juce::Colours::darkgrey);
	g.setColour(juce::Colours::white);
	bounds.removeFromLeft(20);
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
	auto bounds = getLocalBounds();
	g.setColour(juce::Colours::white);
	g.drawRect(bounds);
}

void EffectsListComponent::resized() {
	auto area = getLocalBounds();
	area.removeFromLeft(20);
	list.setBounds(area);
}

int EffectsListBoxModel::getRowHeight(int row) {
	auto data = (AudioEffectListBoxItemData&)modelData;
	return data.getEffect(row)->parameters.size() * 30;
}

bool EffectsListBoxModel::hasVariableHeightRows() const {
	return true;
}

juce::Component* EffectsListBoxModel::refreshComponentForRow(int rowNumber, bool isRowSelected, juce::Component *existingComponentToUpdate) {
    std::unique_ptr<EffectsListComponent> item(dynamic_cast<EffectsListComponent*>(existingComponentToUpdate));
    if (juce::isPositiveAndBelow(rowNumber, modelData.getNumItems())) {
		auto data = (AudioEffectListBoxItemData&)modelData;
        item = std::make_unique<EffectsListComponent>(listBox, (AudioEffectListBoxItemData&)modelData, rowNumber, data.getEffect(rowNumber));
    }
    return item.release();
}
