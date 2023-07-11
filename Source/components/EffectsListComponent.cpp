#include "EffectsListComponent.h"

EffectsListComponent::EffectsListComponent(DraggableListBox& lb, AudioEffectListBoxItemData& data, int rn, std::shared_ptr<Effect> effect) : DraggableListBoxItem(lb, data, rn), effect(effect) {
	auto details = effect->getDetails();
	for (int i = 0; i < details.size(); i++) {
		std::shared_ptr<EffectComponent> effectComponent = std::make_shared<EffectComponent>(0, 1, 0.01, details[i], i == 0);
		effectComponent->slider.setValue(details[i].value, juce::dontSendNotification);
		effectComponent->slider.onValueChange = [this, i, effectComponent] {
			this->effect->setValue(i, effectComponent->slider.getValue());
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
			effectComponent->selected.onClick = [this, effectComponent] {
				auto data = (AudioEffectListBoxItemData&)modelData;
				juce::SpinLock::ScopedLockType lock(data.audioProcessor.effectsLock);
				data.setSelected(rowNum, effectComponent->selected.getToggleState());
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
	DraggableListBoxItem::paint(g);
	auto bounds = getLocalBounds();
	bounds.removeFromLeft(20);
	// draw drag and drop handle using circles
	g.setColour(juce::Colours::white);
	double size = 4;
	double leftPad = 4;
	double spacing = 7;
	double topPad = 7;
	g.fillEllipse(leftPad, topPad, size, size);
	g.fillEllipse(leftPad, topPad + spacing, size, size);
	g.fillEllipse(leftPad, topPad + 2 * spacing, size, size);
	g.fillEllipse(leftPad + spacing, topPad, size, size);
	g.fillEllipse(leftPad + spacing, topPad + spacing, size, size);
	g.fillEllipse(leftPad + spacing, topPad + 2 * spacing, size, size);
}

void EffectsListComponent::resized() {
	auto area = getLocalBounds();
	area.removeFromLeft(20);
	list.setBounds(area);
}

int EffectsListBoxModel::getRowHeight(int row) {
	auto data = (AudioEffectListBoxItemData&)modelData;
	return data.getEffect(row)->getDetails().size() * 30;
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
