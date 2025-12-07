#include "OpenFileComponent.h"
#include "GridItemComponent.h"
#include "../JuceLibraryCode/BinaryData.h"

OpenFileComponent::OpenFileComponent(OscirenderAudioProcessor& processor)
    : audioProcessor(processor)
{
    addAndMakeVisible(group);
    group.setText("Open Files");

    addAndMakeVisible(startImportButton);
    startImportButton.onClick = [this]() { openFileChooser(); };
    startImportButton.setColour(juce::TextButton::buttonColourId, Colours::accentColor);
    startImportButton.setColour(juce::TextButton::textColourOffId, juce::Colours::black);

    addAndMakeVisible(chooseExampleLabel);
    chooseExampleLabel.setJustificationType(juce::Justification::centredTop);

    addAndMakeVisible(viewport);
    addAndMakeVisible(closeButton);

    viewport.setViewedComponent(&content, false);
    viewport.setScrollBarsShown(true, false);

    closeButton.onClick = [this]() { if (onClosed) onClosed(); };

    auto addCat = [this](CategoryViews& cat) {
        cat.group.setColour(groupComponentBackgroundColourId, Colours::darker.darker(0.2));
        content.addAndMakeVisible(cat.group);
        content.addAndMakeVisible(cat.grid);
        cat.grid.setUseViewport(false);
        cat.grid.setUseCenteringPlaceholders(false);
    };
    addCat(textCat);
    addCat(luaCat);
    addCat(modelsCat);
    addCat(svgsCat);

    populate();
}

void OpenFileComponent::resized()
{
    auto bounds = getLocalBounds();
    group.setBounds(bounds);

    // examples mode
    const int headerH = 30;
    auto groupBounds = bounds;
    auto headerBounds = groupBounds.removeFromTop(headerH).reduced(4);

    const int closeSize = 20;
    auto rightArea = headerBounds.removeFromRight(closeSize + 4);
    closeButton.setBounds(rightArea.withSizeKeepingCentre(closeSize, closeSize));

    auto inner = groupBounds; // remaining after header

    const int buttonWidth = juce::jmin(220, inner.getWidth() - 20);
    const int buttonHeight = 40;

    startImportButton.setBounds(inner.removeFromTop(60).withSizeKeepingCentre(buttonWidth, buttonHeight));
    chooseExampleLabel.setBounds(inner.removeFromTop(30));

    viewport.setBounds(inner);
    viewport.setFadeVisible(true);

    auto contentArea = viewport.getLocalBounds();
    int y = 0;

    auto layCat = [&](CategoryViews& cat) {
        const int gridHeight = cat.grid.calculateRequiredHeight(contentArea.getWidth());
        const int headerHeight = 20;
        const int padding = 10;

        auto categoryBounds = juce::Rectangle<int>(contentArea.getX(), contentArea.getY() + y, contentArea.getWidth(), gridHeight + headerHeight + 3 * padding);
        y += categoryBounds.getHeight();
        categoryBounds.reduce(10, padding);

        cat.group.setBounds(categoryBounds);
        cat.grid.setBounds(categoryBounds.removeFromBottom(gridHeight));    
    };
    layCat(modelsCat);
    layCat(luaCat);
    layCat(svgsCat);
    layCat(textCat);

    content.setSize(contentArea.getWidth(), y);
}

// Helper to set initial Lua slider values for shape_generator.lua
static void initialiseShapeGeneratorLuaSliders(OscirenderAudioProcessor& processor)
{
    if (processor.luaEffects.size() >= 3)
    {
        auto setParam = [&](int idx, float value) {
            if (processor.luaEffects[idx]->parameters.size() > 0)
                processor.luaEffects[idx]->parameters[0]->setValueNotifyingHost(value);
            processor.luaValues[idx].store(value);
        };
        setParam(0, 0.0f);
        setParam(1, 1.0f);
        setParam(2, 0.0f);
    }
}

void OpenFileComponent::addExample(CategoryViews& cat, const juce::String& fileName, const juce::String& displayName, const char* data, int size, const char* iconData, int iconSize)
{
    if (iconData == nullptr || iconSize <= 0)
    {
        iconData = BinaryData::random_svg;
        iconSize = BinaryData::random_svgSize;
    }
    auto* item = new GridItemComponent(displayName, juce::String::createStringFromData(iconData, iconSize), fileName);
    item->onItemSelected = [this, fileName, data, size](const juce::String&) {
        juce::SpinLock::ScopedLockType parsersLock(audioProcessor.parsersLock);
        audioProcessor.addFile(fileName, data, size);
        int fileIndex = audioProcessor.numFiles() - 1;
        if (fileName.equalsIgnoreCase("shape_generator.lua"))
            initialiseShapeGeneratorLuaSliders(audioProcessor);
        const bool openEditor = fileName.endsWithIgnoreCase(".lua") || fileName.endsWithIgnoreCase(".txt");
        if (onClosed) onClosed();
        if (onFileOpened) onFileOpened(fileName, openEditor, fileIndex);
    };
    cat.grid.addItem(item);
}

void OpenFileComponent::populate()
{
    addExample(textCat, "helloworld.txt", "Hello World", BinaryData::helloworld_txt, BinaryData::helloworld_txtSize, BinaryData::hello_svg, BinaryData::hello_svgSize);
    addExample(textCat, "greek.txt", "Greek", BinaryData::greek_txt, BinaryData::greek_txtSize, BinaryData::greek_svg, BinaryData::greek_svgSize);
    addExample(textCat, "osci-render.txt", "osci-render", BinaryData::oscirender_txt, BinaryData::oscirender_txtSize, BinaryData::osci_svg, BinaryData::osci_svgSize);
    addExample(textCat, "paperclip.txt", "Paperclip", BinaryData::paperclip_txt, BinaryData::paperclip_txtSize, BinaryData::greek_svg, BinaryData::greek_svgSize);
    addExample(textCat, "sosci.txt", "sosci", BinaryData::sosci_txt, BinaryData::sosci_txtSize, BinaryData::greek_svg, BinaryData::greek_svgSize);
    

    addExample(luaCat, "spiral.lua", "Spiral", BinaryData::spiral_lua, BinaryData::spiral_luaSize, BinaryData::swirl_svg, BinaryData::swirl_svgSize);
    addExample(luaCat, "shape_generator.lua", "Shape Generator", BinaryData::shape_generator_lua, BinaryData::shape_generator_luaSize, BinaryData::shapes_svg, BinaryData::shapes_svgSize);
    addExample(luaCat, "squiggles.lua", "Squiggles", BinaryData::squiggles_lua, BinaryData::squiggles_luaSize, BinaryData::waves_svg, BinaryData::waves_svgSize);
    addExample(luaCat, "donut.lua", "Donut", BinaryData::donut_lua, BinaryData::donut_luaSize, BinaryData::donut_svg, BinaryData::donut_svgSize);
    addExample(luaCat, "graph.lua", "Graph", BinaryData::graph_lua, BinaryData::graph_luaSize, BinaryData::graph_svg, BinaryData::graph_svgSize);
    addExample(luaCat, "gravity.lua", "Gravity Well", BinaryData::gravity_lua, BinaryData::gravity_luaSize, BinaryData::gravity_svg, BinaryData::gravity_svgSize);
    addExample(luaCat, "helix.lua", "Helix", BinaryData::helix_lua, BinaryData::helix_luaSize, BinaryData::helix_svg, BinaryData::helix_svgSize);
    addExample(luaCat, "human.lua", "Human", BinaryData::human_lua, BinaryData::human_luaSize, BinaryData::human_svg, BinaryData::human_svgSize);
    addExample(luaCat, "hypercube.lua", "Hypercube", BinaryData::hypercube_lua, BinaryData::hypercube_luaSize, BinaryData::cube_svg, BinaryData::cube_svgSize);
    addExample(luaCat, "mushroom.lua", "Mushroom", BinaryData::mushroom_lua, BinaryData::mushroom_luaSize, BinaryData::mushroom_svg, BinaryData::mushroom_svgSize);
    addExample(luaCat, "planet.lua", "Planet", BinaryData::planet_lua, BinaryData::planet_luaSize, BinaryData::planet_svg, BinaryData::planet_svgSize);

    addExample(modelsCat, "cube.obj", "Cube", BinaryData::cube_obj, BinaryData::cube_objSize, BinaryData::cube_svg, BinaryData::cube_svgSize);
    addExample(modelsCat, "diamond.obj", "Diamond", BinaryData::diamond_obj, BinaryData::diamond_objSize, BinaryData::diamond_svg, BinaryData::diamond_svgSize);
    addExample(modelsCat, "dodecahedron.obj", "Dodecahedron", BinaryData::dodecahedron_obj, BinaryData::dodecahedron_objSize, BinaryData::dodecahedron_svg, BinaryData::dodecahedron_svgSize);
    addExample(modelsCat, "humanoid_quad.obj", "Humanoid Quad", BinaryData::humanoid_quad_obj, BinaryData::humanoid_quad_objSize, BinaryData::humanoid_quad_objSize > 0 ? BinaryData::human_svg : BinaryData::cube_svg, BinaryData::humanoid_quad_objSize > 0 ? BinaryData::human_svgSize : BinaryData::cube_svgSize);
    addExample(modelsCat, "icosahedron.obj", "Icosahedron", BinaryData::icosahedron_obj, BinaryData::icosahedron_objSize, BinaryData::icosahedron_svg, BinaryData::icosahedron_svgSize);
    addExample(modelsCat, "lamp.obj", "Lamp", BinaryData::lamp_obj, BinaryData::lamp_objSize, BinaryData::lamp_svg, BinaryData::lamp_svgSize);
    addExample(modelsCat, "shuttle.obj", "Shuttle", BinaryData::shuttle_obj, BinaryData::shuttle_objSize, BinaryData::shuttle_svg, BinaryData::shuttle_svgSize);
    addExample(modelsCat, "suzanne.obj", "Suzanne", BinaryData::suzanne_obj, BinaryData::suzanne_objSize, BinaryData::blender_svg, BinaryData::blender_svgSize);
    addExample(modelsCat, "teapot.obj", "Teapot", BinaryData::teapot_obj, BinaryData::teapot_objSize, BinaryData::kettle_svg, BinaryData::kettle_svgSize);
    addExample(modelsCat, "tetrahedron.obj", "Tetrahedron", BinaryData::tetrahedron_obj, BinaryData::tetrahedron_objSize, BinaryData::pyramid_svg, BinaryData::pyramid_svgSize);

    addExample(svgsCat, "air-horn.svg", "Air Horn", BinaryData::airhorn_svg, BinaryData::airhorn_svgSize, BinaryData::airhorn_svg, BinaryData::airhorn_svgSize);
    addExample(svgsCat, "alien.svg", "Alien", BinaryData::alien_svg, BinaryData::alien_svgSize, BinaryData::alien_svg, BinaryData::alien_svgSize);
    addExample(svgsCat, "bicycle.svg", "Bicycle", BinaryData::bicycle_svg, BinaryData::bicycle_svgSize, BinaryData::bicycle_svg, BinaryData::bicycle_svgSize);
    addExample(svgsCat, "card.svg", "Card", BinaryData::card_svg, BinaryData::card_svgSize, BinaryData::card_svg, BinaryData::card_svgSize);
    addExample(svgsCat, "cash.svg", "Cash", BinaryData::cash_svg, BinaryData::cash_svgSize, BinaryData::cash_svg, BinaryData::cash_svgSize);
    addExample(svgsCat, "cat.svg", "Cat", BinaryData::cat_svg, BinaryData::cat_svgSize, BinaryData::cat_svg, BinaryData::cat_svgSize);
    addExample(svgsCat, "clippy.svg", "Clippy", BinaryData::clippy_svg, BinaryData::clippy_svgSize, BinaryData::clippy_svg, BinaryData::clippy_svgSize);
    addExample(svgsCat, "desktop.svg", "Desktop", BinaryData::desktop_svg, BinaryData::desktop_svgSize, BinaryData::desktop_svg, BinaryData::desktop_svgSize);
    addExample(svgsCat, "puzzle.svg", "Puzzle", BinaryData::puzzle_svg, BinaryData::puzzle_svgSize, BinaryData::puzzle_svg, BinaryData::puzzle_svgSize);
    addExample(svgsCat, "skull.svg", "Skull", BinaryData::skull_svg, BinaryData::skull_svgSize, BinaryData::skull_svg, BinaryData::skull_svgSize);
    addExample(svgsCat, "snowflake.svg", "Snowflake", BinaryData::snowflake_svg, BinaryData::snowflake_svgSize, BinaryData::snowflake_svg, BinaryData::snowflake_svgSize);
    addExample(svgsCat, "yinyang.svg", "Yin Yang", BinaryData::yinyang_svg, BinaryData::yinyang_svgSize, BinaryData::yinyang_svg, BinaryData::yinyang_svgSize);
}

void OpenFileComponent::openFileChooser()
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
        
        auto results = chooserRef.getResults();
        if (results.isEmpty()) return;
        
        if (onClosed) onClosed();
        
        for (auto& file : results) {
            if (file != juce::File()) {
                audioProcessor.setLastOpenedDirectory(file.getParentDirectory());
                audioProcessor.addFile(file);
                int fileIndex = audioProcessor.numFiles() - 1;
                juce::String fileName = file.getFileName();
                if (fileName.equalsIgnoreCase("shape_generator.lua"))
                    initialiseShapeGeneratorLuaSliders(audioProcessor);
                if (onFileOpened) onFileOpened(fileName, shouldOpenEditorFor(fileName), fileIndex);
            }
        }
    });
}
