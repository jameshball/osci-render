#include "ComponentList.h"

int ComponentListModel::getNumRows() {
	return components.size();
}

void ComponentListModel::paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) {}

juce::Component* ComponentListModel::refreshComponentForRow(int rowNum, bool isRowSelected, juce::Component *existingComponentToUpdate) {
	std::unique_ptr<ComponentWrapper> item(dynamic_cast<ComponentWrapper*>(existingComponentToUpdate));
	if (juce::isPositiveAndBelow(rowNum, getNumRows())) {
		item = std::make_unique<ComponentWrapper>(components[rowNum]);
	}
	return item.release();
}
