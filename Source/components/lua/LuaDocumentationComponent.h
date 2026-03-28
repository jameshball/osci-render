#pragma once

#include <JuceHeader.h>
#include "LuaCodeSnippet.h"
#include "../OverlayComponent.h"
#include "../SvgButton.h"

// Full-screen overlay displaying Lua scripting documentation.
// Uses a Viewport over a content component with headings, body
// text, and syntax-highlighted read-only code snippets.
class LuaDocumentationComponent : public OverlayComponent {
public:
    LuaDocumentationComponent();
    ~LuaDocumentationComponent() override = default;

protected:
    void resizeContent(juce::Rectangle<int> contentArea) override;

private:
    // Helpers to add elements to the content component.
    void addHeading(const juce::String& text);
    void addSubHeading(const juce::String& text);
    void addBody(const juce::String& text);
    void addCode(const juce::String& code);
    void addLink(const juce::String& text, const juce::URL& url);
    void addSpacer(int height = 10);

    void buildDocumentation();
    void layoutContent(int width);

    juce::Label titleLabel;
    std::unique_ptr<SvgButton> closeButton;

    juce::Viewport viewport;
    juce::Component contentComponent;

    // Owned elements inside contentComponent
    juce::OwnedArray<juce::Component> elements;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LuaDocumentationComponent)
};
