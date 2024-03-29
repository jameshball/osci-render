#pragma once
#include <JuceHeader.h>
#include "../lua/LuaParser.h"
#include "../PluginProcessor.h"

class ErrorCodeEditorComponent : public juce::CodeEditorComponent, public ErrorListener, public juce::AsyncUpdater {
 public:
    ErrorCodeEditorComponent(juce::CodeDocument& document, juce::CodeTokeniser* codeTokeniser, juce::String id) : juce::CodeEditorComponent(document, codeTokeniser), id(id) {}

    void paint(juce::Graphics& g) override {
        juce::CodeEditorComponent::paint(g);
        if (errorLine != -1) {
            int firstVisibleLine = getFirstLineOnScreen();
            int lastVisibleLine = firstVisibleLine + getNumLinesOnScreen();
            if (errorLine < firstVisibleLine || errorLine > lastVisibleLine) {
                return;
            }
            
            auto lineBounds = getErrorLineBounds();

            // draw error line
            g.setColour(juce::Colours::red);
            // draw red squiggly line
            juce::Path path;

            double xOffset = 0;
            double yOffset = 2;
            
            double lineIncrement = 2.5;
            double squiggleHeight = 3;

            juce::String line = getDocument().getLine(errorLine - 1);
            // get number of leading whitespace characters
            int leadingWhitespace = line.length() - line.trimStart().length();
            double start = getCharWidth() * leadingWhitespace;
            double width = getCharWidth() * line.length() - getCharWidth();

            double squiggleMax = lineBounds.getY() + lineBounds.getHeight() - squiggleHeight + yOffset;
            double squiggleMin = lineBounds.getY() + lineBounds.getHeight() + yOffset;

            path.startNewSubPath(lineBounds.getX() + start + xOffset, squiggleMax);

            for (double i = start; i < width - xOffset; i += 2 * lineIncrement) {
                path.lineTo(lineBounds.getX() + i + xOffset, squiggleMax);
                path.lineTo(lineBounds.getX() + i + lineIncrement + xOffset, squiggleMin);
            }
            g.strokePath(path, juce::PathStrokeType(1.0f));


            if (errorLineHovered) {
                // draw error text on line below the one being hovered
                auto bounds = juce::Rectangle<int>(lineBounds.getX(), lineBounds.getY() + lineBounds.getHeight(), lineBounds.getWidth(), lineBounds.getHeight());

                // draw over existing line
                g.setColour(juce::Colours::red);
                g.fillRect(bounds);

                // draw text
                g.setColour(juce::Colours::white);
                g.setFont(12);
                g.drawText(errorText, bounds, juce::Justification::centred);
            }
        }
    }

    void mouseMove(const juce::MouseEvent& event) override {
        juce::CodeEditorComponent::mouseMove(event);
        if (errorLine != -1) {
            errorLineHovered = getErrorLineBounds().contains(event.getPosition());
            repaint();
        }
    }

    juce::Rectangle<int> getErrorLineBounds() {
        int firstVisibleLine = getFirstLineOnScreen();
        double gutterWidth = 35;

        return juce::Rectangle<int>(gutterWidth, getLineHeight() * (errorLine - 1 - firstVisibleLine), getWidth() - gutterWidth, getLineHeight());
    }

    void handleAsyncUpdate() override {
        repaint();
    }

    void onError(int lineNumber, juce::String error) override {
        int oldErrorLine = errorLine;
        errorLine = lineNumber;
        errorText = error;
        if (errorLine != -1 || errorLine != oldErrorLine) {
            triggerAsyncUpdate();
        }
    }

private:

    juce::String getId() override {
        return id;
    }

    int errorLine = -1;
    juce::String id;
    juce::String errorText;
    bool errorLineHovered = false;
};

class OscirenderCodeEditorComponent : public juce::GroupComponent {
public:
    OscirenderCodeEditorComponent(juce::CodeDocument& document, juce::CodeTokeniser* codeTokeniser, OscirenderAudioProcessor& p, juce::String id, juce::String fileName) : editor(document, codeTokeniser, id), audioProcessor(p) {
        setText(fileName);
        setTextLabelPosition(juce::Justification::centred);
        setColour(groupComponentBackgroundColourId, Colours::veryDark);
        
        addAndMakeVisible(editor);
        
        audioProcessor.addErrorListener(&editor);
    }

    ~OscirenderCodeEditorComponent() override {
        audioProcessor.removeErrorListener(&editor);
    }

    void resized() override {
        auto bounds = getLocalBounds();
        bounds.removeFromTop(30);
        bounds.removeFromBottom(5);
        editor.setBounds(bounds);
    }

    ErrorCodeEditorComponent& getEditor() {
        return editor;
    }

private:

    ErrorCodeEditorComponent editor;
    OscirenderAudioProcessor& audioProcessor;
};
