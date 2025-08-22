#include "ExampleFilesGridComponent.h"
#include "GridItemComponent.h"
#include "../JuceLibraryCode/BinaryData.h"

ExampleFilesGridComponent::ExampleFilesGridComponent(OscirenderAudioProcessor& processor)
    : audioProcessor(processor)
{
    // Group styling
    addAndMakeVisible(group);
    group.setText("Open Files");

    // Outer viewport for all content
    addAndMakeVisible(viewport);
    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);

    // Close button in header
    addAndMakeVisible(closeButton);
    closeButton.onClick = [this]() { if (onClosed) onClosed(); };

    // Choose files button in header
    addAndMakeVisible(chooseFilesButton);
    chooseFilesButton.onClick = [this]() { openFileChooser(); };

    // Add categories to content; configure grids (no internal viewport, no centering)
    auto addCat = [this](CategoryViews& cat) {
        styleHeading(cat.heading);
        content.addAndMakeVisible(cat.heading);
        content.addAndMakeVisible(cat.grid);
        cat.grid.setUseViewport(false);
        cat.grid.setUseCenteringPlaceholders(false);
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
    // transparent background; group draws its own background
}

void ExampleFilesGridComponent::resized()
{
    // Fill entire area without margin
    auto bounds = getLocalBounds();

    // Layout group to fill
    group.setBounds(bounds);

    // Compute header height based on group font + padding. GroupComponent typically draws a label at top ~20px.
    const int headerH = 32; // reserve space for group heading text strip (avoid overlap)

    // Position header controls within group header area
    {
        auto headerBounds = group.getBounds().removeFromTop(headerH);
        auto closeSize = 18;
        auto closeArea = headerBounds.removeFromRight(closeSize + 8).withSizeKeepingCentre(closeSize, closeSize);
        closeButton.setBounds(closeArea);

        auto buttonW = 140;
        auto buttonH = 24;
        auto chooseArea = headerBounds.removeFromLeft(buttonW).withSizeKeepingCentre(buttonW, buttonH);
        chooseFilesButton.setBounds(chooseArea);
    }

    // Inside group, leave room for the group text by padding the viewport area
    auto inner = group.getLocalBounds();
    inner.removeFromTop(headerH); // ensure viewport starts below header
    // Translate to this component's coordinate space
    inner = inner.translated(group.getX(), group.getY());

    // Layout outer viewport inside group
    viewport.setBounds(inner);
    viewport.setFadeVisible(true);
    // Ensure overlay and layout are up-to-date when shown
    viewport.resized();

    // Lay out content height based on all categories
    auto contentArea = viewport.getLocalBounds();
    int y = 0;

    auto layCat = [&](CategoryViews& cat) {
        auto header = juce::Rectangle<int>(contentArea.getX(), contentArea.getY() + y, contentArea.getWidth(), 24);
        cat.heading.setBounds(header.reduced(2));
        y += 24;
        const int gridHeight = cat.grid.calculateRequiredHeight(contentArea.getWidth());
        cat.grid.setBounds(contentArea.getX(), contentArea.getY() + y, contentArea.getWidth(), gridHeight);
        y += gridHeight + 8; // gap
    };

    layCat(audioCat);
    layCat(textCat);
    layCat(imagesCat);
    layCat(luaCat);
    layCat(modelsCat);
    layCat(svgsCat);

    content.setSize(contentArea.getWidth(), y);
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
        // Auto-close after selection
        if (onClosed) onClosed();
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

void ExampleFilesGridComponent::openFileChooser()
{
    juce::String fileFormats;
    for (auto& ext : audioProcessor.FILE_EXTENSIONS) {
        fileFormats += "*." + ext + ";";
    }
    chooser = std::make_unique<juce::FileChooser>("Open", audioProcessor.getLastOpenedDirectory(), fileFormats);
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectMultipleItems |
                 juce::FileBrowserComponent::canSelectFiles;

    chooser->launchAsync(flags, [this](const juce::FileChooser& chooserRef) {
        juce::SpinLock::ScopedLockType parsersLock(audioProcessor.parsersLock);
        bool anyAdded = false;
        juce::String lastName;
        for (auto& file : chooserRef.getResults()) {
            if (file != juce::File()) {
                audioProcessor.setLastOpenedDirectory(file.getParentDirectory());
                audioProcessor.addFile(file);
                anyAdded = true;
                lastName = file.getFileName();
            }
        }

        if (anyAdded) {
            if (onExampleOpened) onExampleOpened(audioProcessor.getCurrentFileName(), shouldOpenEditorFor(lastName));
            if (onClosed) onClosed();
        }
    });
}
