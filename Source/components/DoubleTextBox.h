#pragma once
#include <JuceHeader.h>

class DoubleTextBox : public juce::TextEditor {
public:
	DoubleTextBox(double minValue, double maxValue) : minValue(minValue), maxValue(maxValue) {
		setText(juce::String(minValue, 2), false);
		setMultiLine(false);
		setJustification(juce::Justification::centred);
		setFont(juce::Font(15.0f, juce::Font::plain));
		onTextChange = [this]() {
			setText(getText(), false);
		};
		juce::Desktop::getInstance().addGlobalMouseListener(this);
    }

	double getValue() {
		return getText().getDoubleValue();
	}

	void setValue(double value, bool sendChangeMessage = true, int digits = 2) {
		setText(juce::String(value, (size_t)(digits)), sendChangeMessage);
	}

	void setText(const juce::String& newText, bool sendChangeMessage = true) {
		// remove all non-digits
		juce::String text = newText.retainCharacters("0123456789.-");

		// only keep first decimal point
		int firstDecimal = text.indexOfChar('.');
		if (firstDecimal != -1) {
			juce::String remainder = text.substring(firstDecimal + 1);
			remainder = remainder.retainCharacters("0123456789");
			text = text.substring(0, firstDecimal + 1) + remainder;
		}

		// only keep negative sign at beginning
		if (text.contains("-")) {
			juce::String remainder = text.substring(1);
			remainder = remainder.retainCharacters("0123456789.");
			text = text.substring(0, 1) + remainder;
		}

		double value = text.getDoubleValue();
		if (value < minValue || value > maxValue) {
			text = juce::String(prevValue);
		} else {
			prevValue = value;
		}
		
		juce::TextEditor::setText(text, sendChangeMessage);
	}

	void mouseDown(const juce::MouseEvent& e) override {
		if (getScreenBounds().contains(e.getScreenPosition())) {
			// Delegate mouse clicks inside the editor to the TextEditor
			// class so as to not break its functionality.
			juce::TextEditor::mouseDown(e);
		} else {
			// Lose focus when mouse clicks occur outside the editor.
			giveAwayKeyboardFocus();
		}
	}

    ~DoubleTextBox() override {
		juce::Desktop::getInstance().removeGlobalMouseListener(this);
	}

private:
	double minValue;
	double maxValue;
	double prevValue = minValue;
};
