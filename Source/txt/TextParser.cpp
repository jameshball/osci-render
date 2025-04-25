#include "TextParser.h"
#include "../svg/SvgParser.h"
#include "../PluginProcessor.h"


TextParser::TextParser(OscirenderAudioProcessor &p, juce::String text, juce::Font font) : audioProcessor(p), text(text) {
    parse(text, font);
}

TextParser::~TextParser() {
}

void TextParser::parse(juce::String text, juce::Font font) {
    lastFont = font;
    
    juce::Path textPath;

#if OSCI_PREMIUM
    // Apply formatting markers if the font is bold or italic
    juce::String formattedText = text;
    
    if (font.isBold()) {
        formattedText = "*" + formattedText + "*";
    }
    
    if (font.isItalic()) {
        formattedText = "_" + formattedText + "_";
    }
    
    // Parse the text with formatting
    attributedString = parseFormattedText(formattedText, font);
    
    // Create a TextLayout from the AttributedString
    juce::TextLayout layout;
    layout.createLayout(attributedString, 64.0f);

    juce::String displayText = attributedString.getText();
    // remove all whitespace
    displayText = displayText.removeCharacters(" \t\n\r");
    int index = 0;
    
    // Iterate through all lines and all runs in each line
    for (int i = 0; i < layout.getNumLines(); ++i) {
        const juce::TextLayout::Line& line = layout.getLine(i);
        
        for (int j = 0; j < line.runs.size(); ++j) {
            juce::TextLayout::Run* run = line.runs.getUnchecked(j);
            
            // Create a GlyphArrangement for this run
            juce::GlyphArrangement glyphs;
            
            for (int k = 0; k < run->glyphs.size(); ++k) {
                if (index >= displayText.length()) {
                    break;
                }
                juce::juce_wchar character = displayText[index];
                juce::TextLayout::Glyph glyph = run->glyphs.getUnchecked(k);
                juce::PositionedGlyph positionedGlyph = juce::PositionedGlyph(
                    run->font,
                    character,
                    glyph.glyphCode,
                    line.lineOrigin.x + glyph.anchor.x - 1,
                    line.lineOrigin.y + glyph.anchor.y - 1,
                    glyph.width,
                    juce::CharacterFunctions::isWhitespace(character)
                );
                glyphs.addGlyph(positionedGlyph);
                index++;
            }
            
            // Add glyphs to the path
            glyphs.createPath(textPath);
        }
    }
    
    // If the layout has no text, fallback to original method
    if (textPath.isEmpty()) {
#endif
        juce::GlyphArrangement glyphs;
        glyphs.addFittedText(font, text, -2, -2, 4, 4, juce::Justification::centred, 2);
        glyphs.createPath(textPath);
#if OSCI_PREMIUM
    }
#endif

    // Convert path to shapes
    shapes = std::vector<std::unique_ptr<osci::Shape>>();
    SvgParser::pathToShapes(textPath, shapes, true);
}

juce::AttributedString TextParser::parseFormattedText(const juce::String& text, juce::Font font) {
    juce::AttributedString result;
    
    // Handle headings first - they're line-based
    juce::StringArray lines;
    lines.addLines(text);
    
    // Combine lines after handling headings for continuous formatting
    juce::String processedText;
    
    for (auto line : lines) {
        // Handle headings
        if (line.startsWith("##### ")) {
            if (!processedText.isEmpty()) {
                // Process any accumulated text before the heading
                processFormattedTextBody(processedText, result, font);
                processedText = juce::String();
            }
            juce::Font headingFont = font.boldened().withHeight(font.getHeight() * 1.1f);
            result.append(parseFormattedText(line.substring(6), headingFont));
            result.append("\n", font);
        } else if (line.startsWith("#### ")) {
            if (!processedText.isEmpty()) {
                processFormattedTextBody(processedText, result, font);
                processedText = juce::String();
            }
            juce::Font headingFont = font.boldened().withHeight(font.getHeight() * 1.25f);
            result.append(parseFormattedText(line.substring(5), headingFont));
            result.append("\n", font);
        } else if (line.startsWith("### ")) {
            if (!processedText.isEmpty()) {
                processFormattedTextBody(processedText, result, font);
                processedText = juce::String();
            }
            juce::Font headingFont = font.boldened().withHeight(font.getHeight() * 1.42f);
            result.append(parseFormattedText(line.substring(4), headingFont));
            result.append("\n", font);
        } else if (line.startsWith("## ")) {
            if (!processedText.isEmpty()) {
                processFormattedTextBody(processedText, result, font);
                processedText = juce::String();
            }
            juce::Font headingFont = font.boldened().withHeight(font.getHeight() * 1.7f);
            result.append(parseFormattedText(line.substring(3), headingFont));
            result.append("\n", font);
        } else if (line.startsWith("# ")) {
            if (!processedText.isEmpty()) {
                processFormattedTextBody(processedText, result, font);
                processedText = juce::String();
            }
            juce::Font headingFont = font.boldened().withHeight(font.getHeight() * 2.1f);
            result.append(parseFormattedText(line.substring(2), headingFont));
            result.append("\n", font);
        } else {
            // For regular text, add to the accumulated text with a newline
            if (!processedText.isEmpty()) {
                processedText += "\n";
            }
            processedText += line;
        }
    }
    
    // Process any remaining text
    if (!processedText.isEmpty()) {
        processFormattedTextBody(processedText, result, font);
    }
    
    return result;
}

// Helper function to process the body text with continuous formatting
void TextParser::processFormattedTextBody(const juce::String& text, juce::AttributedString& result, juce::Font font) {
    juce::String currentText;
    int i = 0;
    
    // Track formatting positions
    juce::Array<int> boldPositions;
    juce::Array<int> italicPositions;
    
    // First, find all formatting markers and record their positions
    while (i < text.length()) {
        if (text[i] == '*') {
            boldPositions.add(i);
        } else if (text[i] == '_') {
            italicPositions.add(i);
        }
        i++;
    }
    
    // Process the text with the recorded formatting positions
    i = 0;
    while (i < text.length()) {
        // Check if we're at a bold marker
        if (text[i] == '*' && boldPositions.indexOf(i) >= 0) {
            int boldIndex = boldPositions.indexOf(i);
            // If there's a matching pair
            if (boldIndex % 2 == 0 && boldIndex + 1 < boldPositions.size()) {
                // First append accumulated text with normal font
                result.append(currentText, font);
                currentText = juce::String();
                
                // Extract and append the bold text
                int endPos = boldPositions[boldIndex + 1];
                juce::String boldText = text.substring(i + 1, endPos);
                result.append(boldText, font.boldened());
                
                // Move past the closing asterisk
                i = endPos + 1;
            } else {
                // No matching pair, treat as literal
                currentText += '*';
                i++;
            }
            continue;
        }
        
        // Check if we're at an italic marker
        if (text[i] == '_' && italicPositions.indexOf(i) >= 0) {
            int italicIndex = italicPositions.indexOf(i);
            // If there's a matching pair
            if (italicIndex % 2 == 0 && italicIndex + 1 < italicPositions.size()) {
                // First append accumulated text with normal font
                result.append(currentText, font);
                currentText = juce::String();
                
                // Extract and append the italic text
                int endPos = italicPositions[italicIndex + 1];
                juce::String italicText = text.substring(i + 1, endPos);
                result.append(italicText, font.italicised());
                
                // Move past the closing underscore
                i = endPos + 1;
            } else {
                // No matching pair, treat as literal
                currentText += '_';
                i++;
            }
            continue;
        }
        
        // Add current character to text buffer
        currentText += text[i++];
    }
    
    // Add any remaining text
    if (currentText.isNotEmpty()) {
        result.append(currentText, font);
    }
}

std::vector<std::unique_ptr<osci::Shape>> TextParser::draw() {
    // reparse text if font changes
    if (audioProcessor.font != lastFont) {
        parse(text, audioProcessor.font);
    }
    
    // clone with deep copy
    std::vector<std::unique_ptr<osci::Shape>> tempShapes;
    
    for (auto& shape : shapes) {
        tempShapes.push_back(shape->clone());
    }
    return tempShapes;
}
