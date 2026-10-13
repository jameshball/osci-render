#include <JuceHeader.h>
#include "../Source/motion/model/Document.h"
#include "../Source/motion/import/SourceDecoding.h"
#include "../Source/motion/render/HiddenLineFingerprint.h"
#include "../Source/motion/render/HiddenLines.h"
#include "../Source/motion/model/LightPresets.h"
#include "../Source/motion/render/BeamRenderer.h"

// Lights: clips that light what the Scope draws. Without one, everything is
// as bright as it always was; with one, each line takes the light reaching
// it, and what no light reaches is not drawn.
class MotionLightingTest : public juce::UnitTest {
public:
    MotionLightingTest() : juce::UnitTest("Motion lighting", "Motion") {}

    void runTest() override {
        beginTest("A light is never drawn on the Scope, only pictured in the Scene");
        {
            auto project = scene();
            addLight(project, "pointLight", 30, {{"position.y", 1.5}});
            const motion::PreparedComposition signal(project);
            expectEquals(static_cast<int>(signal.lights.size()), 1, "the light lights the signal");
            expectEquals(static_cast<int>(signal.clips.size()), 2, "and is not one of what it draws");
            expect(signal.hiddenLines != nullptr, "lit frames are baked though no camera hides lines");
            const motion::PreparedComposition editor(project, 48000, nullptr, motion::CompositionPurpose::editorGeometry);
            expectEquals(static_cast<int>(editor.clips.size()), 3, "the Scene draws its picture");
            motion::BeamRenderer beam;
            beam.planCycle(editor, 1, 48000);
            bool drawsLight = false;
            for (const auto& segment : beam.plannedSegments()) { drawsLight = drawsLight || (segment.clip != nullptr && segment.clip->light); }
            expect(!drawsLight, "but the beam's share of time never goes to it");
            const motion::PreparedComposition unlit(scene());
            expect(unlit.hiddenLines == nullptr, "with no light and no camera hiding lines, nothing is baked");
        }

        beginTest("A light lights only while its clip plays");
        {
            auto project = scene();
            addLight(project, "ambientLight", 30, {}, 1.0);
            const motion::PreparedComposition prepared(project);
            motion::hidden::Workspace workspace;
            expect(cull(prepared, .5, workspace)->drawings.empty(), "before it starts, everything draws as it is");
            expect(!cull(prepared, 1.5, workspace)->drawings.empty(), "while it plays, what it lights is redrawn");
        }

        beginTest("An ambient light lights everything evenly, in its colour");
        {
            auto project = scene();
            addLight(project, "ambientLight", 30, {{"light.intensity", .5}, {"green", .5}, {"blue", 0}});
            const motion::PreparedComposition prepared(project);
            motion::hidden::Workspace workspace;
            const auto frame = cull(prepared, 1, workspace);
            for (std::size_t clip = 0; clip < 2; ++clip) {
                const auto colours = coloursOf(frame->drawing(clip));
                expect(!colours.empty(), "every clip is lit");
                for (const auto& colour : colours) {
                    expect(std::abs(colour[0] - .5f) < 1.0e-4f && std::abs(colour[1] - .25f) < 1.0e-4f && colour[2] == 0, "each line at the light's colour times its intensity");
                }
            }
        }

        beginTest("A light brighter than the beam can draw keeps its colour");
        {
            auto project = scene();
            addLight(project, "ambientLight", 30, {{"light.intensity", 3}, {"green", .5}, {"blue", .1}});
            const motion::PreparedComposition prepared(project);
            motion::hidden::Workspace workspace;
            const auto colours = coloursOf(cull(prepared, 1, workspace)->drawing(0));
            expect(!colours.empty(), "the box is lit");
            for (const auto& colour : colours) {
                expect(std::abs(colour[0] - 1) < 1.0e-4f && std::abs(colour[1] - .5f) < 1.0e-4f && std::abs(colour[2] - .1f) < 1.0e-4f, "at full brightness in the light's own orange, not white");
            }
        }

        beginTest("Lit by lights at 0, a clip draws in its own colour, as a flame gives off its own light");
        {
            const auto litBy = [&](double amount) {
                auto project = scene();
                auto box = project.tracks[0].clips[0];
                box.properties["lit"] = motion::Curve(amount);
                motion::Track track;
                track.id = project.tracks[0].id;
                track.insert(box, project.tempo());
                auto rest = project.tracks;
                project.tracks.clear();
                project.tracks.push_back(track);
                for (std::size_t index = 1; index < rest.size(); ++index) { project.tracks.push_back(rest[index]); }
                addLight(project, "ambientLight", 30, {{"light.intensity", .4}, {"green", 0}, {"blue", 0}});
                const motion::PreparedComposition prepared(project);
                motion::hidden::Workspace workspace;
                return coloursOf(cull(prepared, 1, workspace)->drawing(0));
            };
            const auto own = litBy(0), half = litBy(50);
            expect(!own.empty() && !half.empty(), "the box is drawn either way");
            for (const auto& colour : own) { expect(colour[0] == 1 && colour[1] == 1 && colour[2] == 1, "unlit, it keeps its own white"); }
            for (const auto& colour : half) { expect(std::abs(colour[0] - .7f) < 1.0e-4f && std::abs(colour[1] - .5f) < 1.0e-4f, "half lit, it is halfway between its own colour and the light's"); }
        }

        beginTest("A point light fades with distance, and lights nothing past its range");
        {
            auto project = scene();
            addLight(project, "pointLight", 30, {{"position.x", 2}, {"position.y", 0}, {"position.z", 0}, {"light.range", 2.6}, {"light.shadows", 0}});
            const motion::PreparedComposition prepared(project);
            motion::hidden::Workspace workspace;
            const auto frame = cull(prepared, 1, workspace);
            const auto* box = frame->drawing(0);
            expect(box != nullptr && !box->empty(), "the box is lit");
            // Its near side brighter than its far side; nothing past the range.
            float nearest = 0, furthest = 1.0e9f;
            for (std::size_t index = 0; box != nullptr && index < box->shapeCount(); ++index) {
                for (int step = 0; step <= 4; ++step) {
                    const auto point = box->shape(index)->nextVector(step / 4.0f);
                    if (point.x > .45f) { nearest = std::max(nearest, point.r); }
                    if (point.x < -.45f) { furthest = std::min(furthest, point.r); }
                }
            }
            expect(nearest > 0 && (furthest > 1.0e8f || furthest < nearest), "the side nearer the light is brighter");
            // Along each line, not just from one to the next: lines running
            // away from the light dim along their length.
            float steepest = 0;
            for (std::size_t index = 0; box != nullptr && index < box->shapeCount(); ++index) {
                auto* shape = box->shape(index);
                steepest = std::max(steepest, std::abs(shape->nextVector(0).r - shape->nextVector(1).r));
            }
            expect(steepest > .1f, "a line's light changes along it");
            auto* edge = box != nullptr && box->shapeCount() > 0 ? box->shape(0) : nullptr;
            bool smooth = edge != nullptr;
            for (int step = 1; edge != nullptr && step < 10; ++step) {
                const auto before = edge->nextVector((step - 1) / 10.0f).r, here = edge->nextVector(step / 10.0f).r, after = edge->nextVector((step + 1) / 10.0f).r;
                smooth = smooth && std::abs(here - (before + after) / 2) < .05f;
            }
            expect(smooth, "and changes smoothly, blended along each part");
            const auto* floor = frame->drawing(1);
            const auto* floorWhole = prepared.clips[1].source->drawingAt(0);
            expect(floor != nullptr && floor->length() < floorWhole->length() * .8, "what is out of its reach is not drawn");
        }

        beginTest("A spot light lights inside its cone and not outside it");
        {
            auto project = scene();
            // Straight down onto the floor, narrow.
            addLight(project, "spotLight", 30, {{"position.x", 1.6}, {"position.y", 1}, {"position.z", 0}, {"rotation.x", -90}, {"rotation.y", 0}, {"light.cone", 30}, {"light.softness", 0}, {"light.range", 10}});
            const motion::PreparedComposition prepared(project);
            motion::hidden::Workspace workspace;
            const auto frame = cull(prepared, 1, workspace);
            const auto* floor = frame->drawing(1);
            expect(floor != nullptr && !floor->empty(), "the floor under it is lit");
            // On the floor, 1.8 below it, its cone reaches tan(15°) * 1.8 across.
            const auto reach = std::tan(15 * juce::MathConstants<double>::pi / 180) * 1.8;
            int outside = 0;
            for (const auto& point : pointsOf(prepared, 1, floor)) { outside += std::hypot(point.x - 1.6, point.z) > reach + .03 ? 1 : 0; }
            expectEquals(outside, 0, "nothing outside its cone is drawn");
        }

        beginTest("What something blocks a light from is in its shadow");
        {
            // Straight down, over the box on the floor.
            const auto shadowed = [&](double strength) {
                auto project = scene();
                addLight(project, "directionalLight", 30, {{"rotation.x", -90}, {"rotation.y", 0}, {"light.shadows", strength}});
                const motion::PreparedComposition prepared(project);
                motion::hidden::Workspace workspace;
                const auto frame = cull(prepared, 1, workspace);
                int under = 0, beside = 0;
                for (const auto& point : pointsOf(prepared, 1, frame->drawing(1))) {
                    under += std::abs(point.x) < .4 && std::abs(point.z) < .4 ? 1 : 0;
                    beside += std::abs(point.x) > .7 || std::abs(point.z) > .7 ? 1 : 0;
                }
                // The box's top is lit; its bottom, facing away and under its top, is not.
                int top = 0, bottom = 0;
                for (const auto& point : pointsOf(prepared, 0, frame->drawing(0))) {
                    top += point.y > .45 ? 1 : 0;
                    bottom += point.y < -.45 && std::abs(point.x) < .45 && std::abs(point.z) < .45 ? 1 : 0;
                }
                return std::array<int, 4> {under, beside, top, bottom};
            };
            const auto full = shadowed(100);
            expectEquals(full[0], 0, "the floor under the box is in its shadow, and not drawn");
            expect(full[1] > 0, "the floor beside it is lit");
            expect(full[2] > 0, "the box's top is lit");
            const auto none = shadowed(0);
            expect(none[0] > 0, "with no shadows, the floor under it is lit too");
        }

        beginTest("Shadows kept from one frame to the next follow the light and what it shines on");
        {
            // Over a point light, which moves at 1 s; then the box moves, at 2 s.
            const auto steps = [](std::initializer_list<std::pair<double, double>> keys) {
                motion::Curve curve(keys.begin()->second);
                for (const auto& [time, value] : keys) {
                    motion::Keyframe key;
                    key.time = time;
                    key.value = value;
                    curve.setKey(key);
                }
                return curve;
            };
            auto project = scene();
            addLight(project, "pointLight", 30, {{"position.y", 1.5}, {"position.z", 0}, {"light.range", 6}});
            // Its tracks again, with one clip moving along the x axis.
            const auto moving = [&](std::size_t index, motion::Curve x) {
                auto clip = project.tracks[index].clips[0];
                clip.properties["position.x"] = std::move(x);
                motion::Track track;
                track.id = project.tracks[index].id;
                track.insert(clip, project.tempo());
                auto tracks = project.tracks;
                project.tracks.clear();
                for (std::size_t other = 0; other < tracks.size(); ++other) { project.tracks.push_back(other == index ? track : tracks[other]); }
            };
            moving(0, steps({{0, 0}, {2, 0}, {2.2, .6}}));
            moving(project.tracks.size() - 1, steps({{0, -.8}, {1, -.8}, {1.2, .8}}));
            const motion::PreparedComposition prepared(project);
            const auto floorAt = [&](double time, motion::hidden::Workspace& workspace) {
                const auto frame = motion::hidden::cull(prepared, time, workspace);
                return std::pair {pointsOf(prepared, 1, frame->drawing(1)).size(), coloursOf(frame->drawing(1))};
            };
            motion::hidden::Workspace kept;
            const auto floorAfter = [&](double time, const char* what) {
                const auto seen = floorAt(time, kept);
                motion::hidden::Workspace fresh;
                expect(seen == floorAt(time, fresh), what);
                return seen;
            };
            const auto still = floorAfter(.2, "the first frame casts its shadows");
            expect(floorAfter(.8, "a frame where nothing moved keeps them") == still);
            const auto lightMoved = floorAfter(1.5, "a light that moves casts them anew");
            expect(lightMoved != still, "where it now casts them");
            expect(floorAfter(2.5, "a surface that moves casts its shadow anew") != lightMoved, "where it now falls");
        }

        beginTest("Lit with a camera hiding lines, what is hidden stays hidden");
        {
            auto project = scene();
            project.cameras.front().properties["hideLines"] = motion::Curve(100.0);
            addLight(project, "ambientLight", 30, {});
            const motion::PreparedComposition prepared(project);
            motion::hidden::Workspace workspace;
            const auto lit = cull(prepared, 1, workspace);
            auto unlit = scene();
            unlit.cameras.front().properties["hideLines"] = motion::Curve(100.0);
            const motion::PreparedComposition plain(unlit);
            const auto hidden = cull(plain, 1, workspace);
            const auto* litBox = lit->drawing(0);
            const auto* hiddenBox = hidden->drawing(0);
            expect(litBox != nullptr && hiddenBox != nullptr && std::abs(litBox->length() - hiddenBox->length()) < hiddenBox->length() * .02, "the lit box draws what the unlit one does");
        }

        beginTest("A preset makes a light move on its own, and another replaces it");
        {
            auto project = scene();
            addLight(project, "pointLight", 30, {{"position.y", 1.5}});
            motion::Id next = 1000;
            const auto newId = [&] { return ++next; };
            expect(motion::applyLightPreset(project, 30, motion::LightPreset::fire, newId), "fire goes on a light");
            expectEquals(static_cast<int>(project.modulators.size()), 6, "as six ways it moves");
            expectEquals(static_cast<int>(project.routes.size()), 6, "each routed to the light");
            expect(project.modulators.front().name.starts_with("Point light \xc2\xb7 Fire"), "named for the light and the preset");
            {
                const motion::PreparedComposition prepared(project);
                std::vector<double> intensities;
                double lowest = 1.0e9, highest = 0, drift = 0;
                for (int step = 0; step < 60; ++step) {
                    const auto state = prepared.lightAt(prepared.lights.front(), step / 20.0);
                    expect(state.has_value());
                    lowest = std::min(lowest, state->intensity);
                    highest = std::max(highest, state->intensity);
                    drift = std::max(drift, std::abs(state->position.y - 1.5));
                }
                expect(highest - lowest > .3, "a fire's intensity flickers");
                expect(drift > .005 && drift < .2, "and it sways a little where it is");
                const auto state = prepared.lightAt(prepared.lights.front(), 0);
                expect(state->colour[0] > state->colour[2] * 2, "in a fire's orange");
            }
            expect(motion::applyLightPreset(project, 30, motion::LightPreset::candle, newId), "a candle goes on in its place");
            expectEquals(static_cast<int>(project.modulators.size()), 3, "the fire's modulators are gone");
            expectEquals(static_cast<int>(project.routes.size()), 3, "and its routes");
            expect(motion::removeLightPreset(project, 30) && project.modulators.empty() && project.routes.empty(), "and the candle comes off cleanly");
            expect(!motion::removeLightPreset(project, 30), "a light with no preset has none to take off");
        }

        beginTest("A light's preset is the one routed to it, whatever the light is called");
        {
            auto project = scene();
            addLight(project, "pointLight", 30, {{"position.y", 1.5}});
            addLight(project, "pointLight", 31, {{"position.y", 1.5}});
            motion::Id next = 1000;
            const auto newId = [&] { return ++next; };
            expect(motion::applyLightPreset(project, 30, motion::LightPreset::candle, newId));
            expect(motion::currentLightPreset(project, 30) == motion::LightPreset::candle, "the light given a candle has one");
            expect(!motion::currentLightPreset(project, 31).has_value() && !motion::hasLightPreset(project, 31), "another of the same name has none");
            motion::changeClip(project, 30)->name = "Lamp";
            expect(motion::currentLightPreset(project, 30) == motion::LightPreset::candle, "renamed, it keeps its preset");
            expect(motion::applyLightPreset(project, 31, motion::LightPreset::pulse, newId));
            expect(motion::currentLightPreset(project, 30) == motion::LightPreset::candle && motion::currentLightPreset(project, 31) == motion::LightPreset::pulse, "each its own");
            expect(motion::removeLightPreset(project, 31) && motion::currentLightPreset(project, 30) == motion::LightPreset::candle, "taking one off leaves the other's");
        }

        beginTest("A face the light turns from fades out, rather than flicking between lit and dark");
        {
            // An upright ring, a point light beside it turning past where the
            // ring's flat faces stand edge on to it: a hair's move of the
            // light changes how much of the ring is lit only a little.
            const auto litAt = [this](double degrees) {
                motion::Project project;
                project.duration = 1;
                addClip(project, generatorAsset(1, "torus"), 10, {{"position.x", 1}, {"position.y", .72}, {"rotation.x", 90}, {"generator.radius", .3}, {"generator.thickness", .07}});
                const auto angle = degrees * juce::MathConstants<double>::pi / 180;
                addLight(project, "pointLight", 30, {{"position.x", std::cos(angle)}, {"position.y", .72}, {"position.z", std::sin(angle)}, {"light.range", 3.2}, {"light.intensity", 1.6}});
                motion::Camera camera;
                camera.id = 40;
                camera.target = 10;
                camera.properties["hideLines"] = motion::Curve(100.0);
                camera.properties["position.x"] = motion::Curve(2.2);
                camera.properties["position.y"] = motion::Curve(1.3);
                camera.properties["position.z"] = motion::Curve(1.6);
                project.cameras.push_back(camera);
                const motion::PreparedComposition prepared(project);
                motion::hidden::Workspace workspace;
                const auto frame = cull(prepared, .5, workspace);
                double lit = 0;
                const auto* drawing = frame->drawing(0);
                for (std::size_t index = 0; drawing != nullptr && index < drawing->shapeCount(); ++index) {
                    auto* shape = drawing->shape(index);
                    double light = 0;
                    for (int step = 0; step < 4; ++step) {
                        const auto point = shape->nextVector((step + .5f) / 4);
                        light += (point.r + point.g + point.b) / 12.0;
                    }
                    lit += shape->length() * light;
                }
                return lit;
            };
            double largest = 0, previous = litAt(244);
            for (double degrees = 244.25; degrees <= 247.01; degrees += .25) {
                const auto next = litAt(degrees);
                largest = std::max(largest, std::abs(next - previous) / std::max(previous, next));
                previous = next;
            }
            expect(largest < .1, "lit within a tenth of itself a quarter of a degree on, at most " + juce::String(largest, 3));
        }

        beginTest("A light keyed and coloured changes the frames it lights");
        {
            auto project = scene();
            addLight(project, "ambientLight", 30, {});
            auto& red = project.tracks.change(project.tracks.size() - 1).clips[0].properties["red"];
            red = motion::Curve(1);
            red.setKey({0, 1});
            red.setKey({2, 0});
            const motion::PreparedComposition prepared(project);
            motion::hidden::Fingerprint early, late;
            early.take(prepared, .5);
            late.take(prepared, 1.5);
            expect(!(early == late), "a light changing colour changes what is baked");
        }
    }

private:
    static std::unique_ptr<motion::hidden::Frame> cull(const motion::PreparedComposition& prepared, double time, motion::hidden::Workspace& workspace) {
        return motion::hidden::cull(prepared, time, workspace);
    }

    static std::shared_ptr<motion::Asset> generatorAsset(motion::Id id, const char* kind) {
        auto asset = std::make_shared<motion::Asset>();
        asset->id = id;
        asset->name = motion::findGenerator(kind)->name.data();
        asset->extension = ".generator";
        asset->data.append(kind, std::strlen(kind));
        const auto decoded = motion::decodeAsset(*asset);
        jassert(decoded.wasOk());
        return asset;
    }

    static void addClip(motion::Project& project, std::shared_ptr<motion::Asset> asset, motion::Id id, std::initializer_list<std::pair<const char*, double>> properties, double start = 0) {
        project.assets.push_back(asset);
        auto clip = motion::Document::makeClip(id, *asset, start);
        clip.duration = 3 - start;
        for (const auto& [name, value] : properties) { clip.properties[name] = motion::Curve(value); }
        motion::Track track;
        track.id = id + 1;
        track.insert(clip, project.tempo());
        project.tracks.push_back(track);
    }

    static void addLight(motion::Project& project, const char* kind, motion::Id id, std::initializer_list<std::pair<const char*, double>> properties, double start = 0) {
        addClip(project, generatorAsset(id + 2, kind), id, properties, start);
    }

    // A unit box standing over a floor of grid, as a camera looking at it sees.
    static motion::Project scene() {
        motion::Project project;
        project.duration = 3;
        addClip(project, generatorAsset(1, "box"), 10, {{"rotation.x", 0}, {"rotation.y", 0}});
        addClip(project, generatorAsset(2, "grid"), 20, {{"rotation.x", -90}, {"position.y", -.8}, {"scale.x", 3}, {"scale.y", 3}});
        motion::Camera camera;
        camera.id = 40;
        camera.properties["position.y"] = motion::Curve(1.5);
        camera.properties["rotation.x"] = motion::Curve(-20);
        project.cameras.push_back(camera);
        return project;
    }

    static std::vector<std::array<float, 3>> coloursOf(const motion::PreparedDrawing* drawing) {
        std::vector<std::array<float, 3>> colours;
        for (std::size_t index = 0; drawing != nullptr && index < drawing->shapeCount(); ++index) {
            const auto point = drawing->shape(index)->nextVector(.5f);
            colours.push_back({point.r, point.g, point.b});
        }
        return colours;
    }

    // A drawing's points in the scene, every so often along each line.
    static std::vector<motion::Vec3> pointsOf(const motion::PreparedComposition& prepared, std::size_t clip, const motion::PreparedDrawing* drawing) {
        std::vector<motion::Vec3> points;
        for (std::size_t index = 0; drawing != nullptr && index < drawing->shapeCount(); ++index) {
            for (int step = 1; step < 20; ++step) {
                const auto at = prepared.worldPoint(prepared.clips[clip], drawing->shape(index)->nextVector(step / 20.0f), 1);
                if (at.has_value()) { points.push_back(*at); }
            }
        }
        return points;
    }
};

static MotionLightingTest motionLightingTest;
