#include "ExampleFilesGridComponent.h"
#include "GridItemComponent.h"
#include "../JuceLibraryCode/BinaryData.h"

ExampleFilesGridComponent::ExampleFilesGridComponent(OscirenderAudioProcessor& processor)
    : audioProcessor(processor)
{
    // Top bar
    addAndMakeVisible(title);
    addAndMakeVisible(closeButton);
    styleHeading(title);
    closeButton.onClick = [this]() { if (onClosed) onClosed(); };

    // Add categories to component
    auto addCat = [this](CategoryViews& cat) {
        styleHeading(cat.heading);
        addAndMakeVisible(cat.heading);
        addAndMakeVisible(cat.grid);
    };
    addCat(audioCat);
    addCat(textCat);
    addCat(imagesCat);
    addCat(luaCat);
    addCat(modelsCat);
    addCat(svgsCat);

    populate();
}

void ExampleFilesGridComponent::styleHeading(juce::Label& l)
{
    l.setInterceptsMouseClicks(false, false);
    l.setJustificationType(juce::Justification::left);
    l.setFont(juce::FontOptions(16.0f, juce::Font::bold));
}

void ExampleFilesGridComponent::paint(juce::Graphics& g)
{
    // transparent background
}

void ExampleFilesGridComponent::resized()
{
    auto bounds = getLocalBounds();
    auto top = bounds.removeFromTop(30);
    title.setBounds(top.removeFromLeft(200).reduced(4));
    closeButton.setBounds(top.removeFromRight(80).reduced(4));

    auto layCat = [&](CategoryViews& cat) {
        auto header = bounds.removeFromTop(24);
        cat.heading.setBounds(header.reduced(2));
        auto h = cat.grid.calculateRequiredHeight(bounds.getWidth());
        cat.grid.setBounds(bounds.removeFromTop(h));
        bounds.removeFromTop(8); // gap
    };

    layCat(audioCat);
    layCat(textCat);
    layCat(imagesCat);
    layCat(luaCat);
    layCat(modelsCat);
    layCat(svgsCat);
}

void ExampleFilesGridComponent::addExample(CategoryViews& cat, const juce::String& fileName, const char* data, int size)
{
    // Use placeholder icon for now
    auto* item = new GridItemComponent(fileName, juce::String::createStringFromData(BinaryData::random_svg, BinaryData::random_svgSize), fileName);
    item->onItemSelected = [this, fileName, data, size](const juce::String&) {
        juce::SpinLock::ScopedLockType parsersLock(audioProcessor.parsersLock);
        audioProcessor.addFile(fileName, data, size);
        // Signal to UI layer that a new example was added so it can open editors, etc.
        const bool openEditor = fileName.endsWithIgnoreCase(".lua") || fileName.endsWithIgnoreCase(".txt");
        if (onExampleOpened) onExampleOpened(fileName, openEditor);
    };
    cat.grid.addItem(item);
}

void ExampleFilesGridComponent::populate()
{
    // Audio examples
    addExample(audioCat, "sosci.flac", BinaryData::sosci_flac, BinaryData::sosci_flacSize);

    // Text examples
    addExample(textCat, "helloworld.txt", BinaryData::helloworld_txt, BinaryData::helloworld_txtSize);
    addExample(textCat, "greek.txt", BinaryData::greek_txt, BinaryData::greek_txtSize);

    // Image examples (will open as images via frame settings path)
    addExample(imagesCat, "empty.jpg", BinaryData::empty_jpg, BinaryData::empty_jpgSize);
    addExample(imagesCat, "no_reflection.jpg", BinaryData::no_reflection_jpg, BinaryData::no_reflection_jpgSize);
    addExample(imagesCat, "noise.jpg", BinaryData::noise_jpg, BinaryData::noise_jpgSize);
    addExample(imagesCat, "real.png", BinaryData::real_png, BinaryData::real_pngSize);
    addExample(imagesCat, "real_reflection.png", BinaryData::real_reflection_png, BinaryData::real_reflection_pngSize);
    addExample(imagesCat, "vector_display.png", BinaryData::vector_display_png, BinaryData::vector_display_pngSize);
    addExample(imagesCat, "vector_display_reflection.png", BinaryData::vector_display_reflection_png, BinaryData::vector_display_reflection_pngSize);

    // Lua examples
    addExample(luaCat, "demo.lua", BinaryData::demo_lua, BinaryData::demo_luaSize);

    // 3D model examples
    addExample(modelsCat, "cube.obj", BinaryData::cube_obj, BinaryData::cube_objSize);

    // SVG examples (just a subset for brevity, can add more)
    addExample(svgsCat, "demo.svg", BinaryData::demo_svg, BinaryData::demo_svgSize);
    addExample(svgsCat, "lua.svg", BinaryData::lua_svg, BinaryData::lua_svgSize);
    addExample(svgsCat, "trace.svg", BinaryData::trace_svg, BinaryData::trace_svgSize);
    addExample(svgsCat, "wobble.svg", BinaryData::wobble_svg, BinaryData::wobble_svgSize);
}
