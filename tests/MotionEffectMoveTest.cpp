#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"

// An effect dragged from one owner to another keeps its parameters and keys;
// a move keeps its identity (and so its routes), a copy gets its own.
class MotionEffectMoveTest : public juce::UnitTest {
public:
    MotionEffectMoveTest() : juce::UnitTest("Motion effect moves", "Motion") {}

    void runTest() override {
        beginTest("Moving an effect carries its parameters and routes; copying leaves the original");
        motion::Project project;
        project.duration = 10;
        auto asset = std::make_shared<motion::Asset>();
        asset->id = 1;
        asset->name = "Square.svg";
        asset->extension = ".svg";
        project.assets.push_back(asset);
        motion::Track first, second;
        first.id = 10;
        second.id = 11;
        auto clip = motion::Document::makeClip(20, *asset, 0);
        auto bulge = motion::makeEffect(30, *motion::effectDefinition("bulge"));
        bulge.properties["strength"].base = .42;
        clip.effects.push_back(bulge);
        expect(first.insert(clip, project.tempo()));
        auto other = motion::Document::makeClip(21, *asset, 0);
        expect(second.insert(other, project.tempo()));
        project.tracks.push_back(first);
        project.tracks.push_back(second);
        motion::Modulator lfo;
        lfo.id = 40;
        project.modulators.push_back(lfo);
        project.routes.push_back({41, 40, 30, "strength", .5, motion::ModulationMode::add});

        juce::UndoManager undo;
        motion::Document document(undo);
        document.reset(project);
        expect(document.moveEffect(30, 21, false).wasOk());
        const auto* moved = motion::findEffect(document.project(), 30);
        expect(moved != nullptr && motion::findEffectList(document.project(), 30) == &motion::findClip(document.project(), 21)->effects, "the effect now belongs to the other clip");
        expect(moved != nullptr && std::abs(moved->properties.at("strength").base - .42) < 1.0e-9, "its parameters come with it");
        expect(motion::findClip(document.project(), 20)->effects.empty(), "and it leaves the first clip");
        expect(document.project().routes.size() == 1 && document.project().routes.front().target == 30, "routes to it still reach it");

        expect(document.moveEffect(30, 10, true).wasOk());
        const auto& trackEffects = document.project().tracks[0].effects;
        expect(trackEffects.size() == 1 && trackEffects.front().id != 30 && std::abs(trackEffects.front().properties.at("strength").base - .42) < 1.0e-9, "a copy has its own identity and the same parameters");
        expect(motion::findEffect(document.project(), 30) != nullptr, "the original stays");

        expect(document.moveEffect(30, 21, false).failed(), "moving onto its own owner does nothing");
        expect(document.moveEffect(99, 21, false).failed(), "an unknown effect does nothing");
        expect(document.moveEffect(30, 0, false).wasOk() && document.project().effects.size() == 1, "dropped on empty space, it applies to the whole composition");

        expect(undo.undo() && undo.undo() && undo.undo());
        expect(motion::findClip(document.project(), 20)->effects.size() == 1 && document.project().tracks[0].effects.empty() && document.project().effects.empty(), "undo puts each step back");
    }
};

static MotionEffectMoveTest motionEffectMoveTest;
