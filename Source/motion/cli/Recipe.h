#pragma once

#include "../import/SourceDecoding.h"
#include "../model/ProjectFile.h"
#include "../model/LightPresets.h"
#include "../model/Vec3.h"
#include <map>

namespace motion::cli {
// A project described in JSON, built as it would be built by hand in the app:
// each object a clip on its own track, from a generator (a box, a sphere, a
// light) or a file, with its properties set and keyed. Kept as text, a scene
// is made again exactly whatever the project format comes to be.
// "description": what it is, shown under its name on the start screen;
// "picture": a time whose frame `make` has the app draw as its picture.
//
//   {
//     "name": "Ball", "duration": 8, "fps": 60, "bpm": 120, "sampleRate": 192000, "beam": {"intensity": 20},
//     "camera": {"lookAt": "Ball",
//                "properties": {"position.z": 4, "hideLines": 100}, "keys": {"position.x": [[0, -1], [8, 1, "linear"]]}},
//     "objects": [
//       {"name": "Ball", "generator": "sphere", "properties": {"generator.radius": 0.3}},
//       {"name": "Teapot", "file": "teapot.obj", "start": 2, "duration": 4},
//       {"name": "Flame", "generator": "pointLight", "preset": "fire", "properties": {"position.y": 1}}
//     ]
//   }
//
// Keys are [time, value] with an optional interpolation: hold, linear,
// smooth (the default) or cubic; or a curve as an expression of t (seconds
// into the clip), keyed every so often with its slope:
//   "keys": {"position.x": {"expression": "1.2 * cos(2 * pi * t / 8)", "every": 0.5}}
// Expressions have + - * /, sin cos tan asin acos atan2 sqrt pow exp abs
// min max floor clamp smoothstep, pi, t, and the recipe's own names:
//   "define": {"turn": "2 * pi * t / 16"}
// A camera can follow a path, looking at a point, both expressions of t:
//   "camera": {"path": {"position": ["4 * cos(turn)", "2", "4 * sin(turn)"], "look": ["0", "0", "0"], "every": 0.05}}
// Steady motion is better a modulator than keys: an LFO (sine, triangle,
// saw, square, smooth noise or held noise) at a rate in Hz, starting
// `phase` of a cycle in, routed to properties of objects or the camera,
// each by its own amount (added, or multiplied with "mode": "multiply"):
//   "modulators": [{"name": "Spin", "waveform": "saw", "rate": 0.125, "phase": 0.5,
//                   "routes": [{"to": "Box", "property": "rotation.y", "amount": 180}]}]
// Files are found beside the recipe.
class Recipe {
public:
    // Recipes are named <scene>.recipe.json.
    static bool isRecipe(const juce::File& file) { return file.getFileName().endsWithIgnoreCase(".recipe.json"); }

    // The project a recipe file makes, read back as a saved project would be:
    // what is drawn from a recipe is what is drawn from the file it writes.
    static juce::Result read(const juce::File& file, Project& project) {
        juce::var recipe;
        const auto parsed = juce::JSON::parse(file.loadFileAsString(), recipe);
        if (parsed.failed()) { return juce::Result::fail("It isn't JSON: " + parsed.getErrorMessage()); }
        Project built;
        const auto made = build(recipe, file.getParentDirectory(), built);
        if (made.failed()) { return made; }
        return Document::prepareLoad(Document::save(built), project);
    }
    static juce::Result build(const juce::var& recipe, const juce::File& folder, Project& project) {
        Recipe builder(folder, project);
        return builder.make(recipe);
    }

private:
    Recipe(const juce::File& folder, Project& project) : folder(folder), project(project) {}

    juce::Result make(const juce::var& recipe) {
        if (!recipe.isObject()) { return juce::Result::fail("A recipe is a JSON object."); }
        if (recipe.hasProperty("name")) { project.name = recipe["name"].toString(); }
        // What it is, under its name on the start screen.
        project.description = recipe["description"].toString().trim();
        if (project.description.length() > Project::longestDescription) { return juce::Result::fail("A description is up to " + juce::String(Project::longestDescription) + " characters."); }
        project.duration = number(recipe, "duration", 8);
        project.frameRate = number(recipe, "fps", 60);
        project.bpm = number(recipe, "bpm", 120);
        // The rate the beam is drawn at (192 kHz unless said).
        project.sampleRate = number(recipe, "sampleRate", Project::defaultSampleRate);
        if (!Project::validSampleRate(project.sampleRate)) { return juce::Result::fail("A sample rate is 22050 to 768000 Hz."); }
        // How it exports: {"width", "height", "codec", "crf", "soundtrack"}.
        const auto& exporting = recipe["export"];
        if (exporting.isObject()) {
            auto& settings = project.exportSettings;
            settings.width = static_cast<int>(number(exporting, "width", settings.width));
            settings.height = static_cast<int>(number(exporting, "height", settings.height));
            settings.codec = static_cast<int>(number(exporting, "codec", settings.codec));
            settings.crf = static_cast<int>(number(exporting, "crf", settings.crf));
            settings.soundtrack = exporting.hasProperty("soundtrack") ? static_cast<bool>(exporting["soundtrack"]) : settings.soundtrack;
            if (!settings.valid()) { return juce::Result::fail("Export settings are a frame of 64 to 8192 px, a codec 0-4 and a CRF of 0-51."); }
        }
        // Names for parts of expressions, used by any expression after.
        const auto* defined = recipe["define"].getDynamicObject();
        if (defined != nullptr) {
            for (const auto& [name, text] : defined->getProperties()) {
                juce::String error;
                const juce::Expression expression(text.toString(), error);
                if (error.isNotEmpty()) { return juce::Result::fail("define " + name.toString() + ": " + error); }
                definitions[name.toString()] = expression;
            }
        }
        // The beam's settings (intensity, persistence...), steady.
        const auto* beamSettings = recipe["beam"].getDynamicObject();
        if (beamSettings != nullptr) {
            for (const auto& [name, value] : beamSettings->getProperties()) { project.beam.properties[name.toString().toStdString()] = Curve(static_cast<double>(value)); }
        }
        const auto* objects = recipe["objects"].getArray();
        if (objects == nullptr || objects->isEmpty()) { return juce::Result::fail("A recipe needs \"objects\" to draw."); }
        for (const auto& object : *objects) {
            const auto added = addObject(object);
            if (added.failed()) { return juce::Result::fail(juce::String(object["name"].toString()) + ": " + added.getErrorMessage()); }
        }
        if (recipe.hasProperty("camera")) {
            const auto added = addCamera(recipe["camera"]);
            if (added.failed()) { return juce::Result::fail("camera: " + added.getErrorMessage()); }
        }
        const auto* modulators = recipe["modulators"].getArray();
        if (modulators != nullptr) {
            for (const auto& modulator : *modulators) {
                const auto added = addModulator(modulator);
                if (added.failed()) { return juce::Result::fail(modulator["name"].toString() + ": " + added.getErrorMessage()); }
            }
        }
        return juce::Result::ok();
    }

    static double number(const juce::var& object, const char* name, double otherwise) {
        return object.hasProperty(name) ? static_cast<double>(object[name]) : otherwise;
    }

    juce::Result addObject(const juce::var& object) {
        auto asset = std::make_shared<Asset>();
        asset->id = next++;
        const auto generator = object["generator"].toString();
        const auto file = object["file"].toString();
        if (generator.isNotEmpty()) {
            const auto* definition = findGenerator(generator.toStdString());
            if (definition == nullptr) { return juce::Result::fail("There is no generator \"" + generator + "\"."); }
            asset->name = juce::String(definition->name.data(), definition->name.size());
            asset->extension = juce::String(generatorExtension.data(), generatorExtension.size());
            asset->data.append(definition->id.data(), definition->id.size());
        } else if (file.isNotEmpty()) {
            const auto source = folder.getChildFile(file);
            if (!source.loadFileAsData(asset->data)) { return juce::Result::fail("Couldn't read " + source.getFullPathName() + "."); }
            asset->name = source.getFileName();
            asset->extension = source.getFileExtension().toLowerCase();
        } else {
            return juce::Result::fail("An object needs a \"generator\" or a \"file\".");
        }
        const auto decoded = decodeAsset(*asset);
        if (decoded.failed()) { return decoded; }
        auto clip = Document::makeClip(next++, *asset, number(object, "start", 0));
        const auto name = object["name"].toString();
        if (name.isNotEmpty()) { clip.name = name.toStdString(); }
        clip.duration = number(object, "duration", project.duration - clip.start);
        const auto set = setProperties(clip.properties, object, clip.duration);
        if (set.failed()) { return set; }
        Track track;
        track.id = next++;
        track.name = clip.name;
        track.insert(clip, project.tempo());
        project.assets.push_back(asset);
        project.tracks.push_back(track);
        named[name] = clip.id;
        const auto preset = object["preset"].toString();
        if (preset.isEmpty()) { return juce::Result::ok(); }
        for (const auto candidate : lightPresets) {
            if (preset.equalsIgnoreCase(juce::String(lightPresetName(candidate).data()))) {
                applyLightPreset(project, clip.id, candidate, [this] { return next++; });
                return juce::Result::ok();
            }
        }
        return juce::Result::fail("There is no light preset \"" + preset + "\".");
    }

    juce::Result addCamera(const juce::var& settings) {
        Camera camera;
        camera.id = next++;
        const auto target = settings["lookAt"].toString();
        if (target.isNotEmpty()) {
            const auto found = named.find(target);
            if (found == named.end()) { return juce::Result::fail("There is no object \"" + target + "\" to look at."); }
            camera.target = found->second;
        }
        const auto set = setProperties(camera.properties, settings, project.duration);
        if (set.failed()) { return set; }
        if (settings.hasProperty("path")) {
            const auto followed = cameraPath(camera, settings["path"]);
            if (followed.failed()) { return juce::Result::fail("path: " + followed.getErrorMessage()); }
        }
        project.cameras.push_back(camera);
        named["camera"] = camera.id;
        return juce::Result::ok();
    }

    juce::Result addModulator(const juce::var& settings) {
        Modulator modulator;
        modulator.id = next++;
        modulator.name = settings["name"].toString().toStdString();
        if (modulator.name.empty()) { return juce::Result::fail("A modulator needs a \"name\"."); }
        const auto waveform = settings["waveform"].toString();
        const std::array<std::pair<const char*, ModulationWaveform>, 6> waveforms {{{"sine", ModulationWaveform::sine}, {"triangle", ModulationWaveform::triangle},
            {"saw", ModulationWaveform::saw}, {"square", ModulationWaveform::square}, {"smooth noise", ModulationWaveform::noiseSmooth}, {"held noise", ModulationWaveform::noiseHold}}};
        const auto found = std::find_if(waveforms.begin(), waveforms.end(), [&](const auto& entry) { return waveform.isEmpty() ? entry.second == ModulationWaveform::sine : waveform.equalsIgnoreCase(entry.first); });
        if (found == waveforms.end()) { return juce::Result::fail("A waveform is sine, triangle, saw, square, smooth noise or held noise, not \"" + waveform + "\"."); }
        modulator.shape.waveform = found->second;
        modulator.shape.rateHz = number(settings, "rate", 1);
        modulator.shape.phase = number(settings, "phase", 0);
        if (!modulator.shape.valid()) { return juce::Result::fail("Its rate is 0.001-1000 Hz and its phase 0-1 of a cycle."); }
        const auto* routes = settings["routes"].getArray();
        if (routes == nullptr || routes->isEmpty()) { return juce::Result::fail("A modulator needs \"routes\" to drive."); }
        for (const auto& item : *routes) {
            const auto to = item["to"].toString();
            const auto target = named.find(to);
            if (target == named.end()) { return juce::Result::fail("There is no object \"" + to + "\" to drive (or \"camera\")."); }
            ModulationRoute route;
            route.id = next++;
            route.modulator = modulator.id;
            route.target = target->second;
            route.property = item["property"].toString().toStdString();
            route.amount = number(item, "amount", 1);
            route.mode = item["mode"].toString().equalsIgnoreCase("multiply") ? ModulationMode::multiply : ModulationMode::add;
            if (!route.valid()) { return juce::Result::fail("A route is {\"to\", \"property\", \"amount\"}."); }
            project.routes.push_back(route);
        }
        project.modulators.push_back(modulator);
        return juce::Result::ok();
    }

    // "properties" as steady values, then "keys" as keyframes.
    juce::Result setProperties(PropertyMap& properties, const juce::var& settings, double duration) const {
        const auto* values = settings["properties"].getDynamicObject();
        if (values != nullptr) {
            for (const auto& [name, value] : values->getProperties()) { properties[name.toString().toStdString()] = Curve(static_cast<double>(value)); }
        }
        const auto* keyed = settings["keys"].getDynamicObject();
        if (keyed == nullptr) { return juce::Result::ok(); }
        for (const auto& [name, keys] : keyed->getProperties()) {
            auto& curve = properties[name.toString().toStdString()];
            if (keys.hasProperty("expression")) {
                const auto made = expressionKeys(curve, keys, duration);
                if (made.failed()) { return juce::Result::fail(name.toString() + ": " + made.getErrorMessage()); }
                continue;
            }
            const auto* list = keys.getArray();
            if (list == nullptr) { return juce::Result::fail("Keys of " + name.toString() + " are a list of [time, value]."); }
            for (const auto& key : *list) {
                const auto* parts = key.getArray();
                if (parts == nullptr || parts->size() < 2) { return juce::Result::fail("A key of " + name.toString() + " is [time, value] or [time, value, interpolation]."); }
                Keyframe keyframe;
                keyframe.time = (*parts)[0];
                keyframe.value = (*parts)[1];
                if (parts->size() > 2) {
                    const auto interpolation = interpolationNamed((*parts)[2].toString());
                    if (!interpolation.has_value()) { return juce::Result::fail("Keys interpolate by hold, linear, smooth or cubic, not \"" + (*parts)[2].toString() + "\"."); }
                    keyframe.interpolation = *interpolation;
                }
                curve.setKey(keyframe);
            }
        }
        return juce::Result::ok();
    }
    // A curve over time from an expression of t, knowing the recipe's
    // definitions; not a number where it can't be worked out.
    using Formula = std::function<double(double)>;
    juce::Result formula(const juce::String& text, Formula& made) const {
        juce::String error;
        const juce::Expression expression(text, error);
        if (error.isNotEmpty()) { return juce::Result::fail(error); }
        made = [expression, this](double t) {
            juce::String failed;
            const auto value = expression.evaluate(Variables(t, definitions), failed);
            return failed.isEmpty() ? value : std::numeric_limits<double>::quiet_NaN();
        };
        if (!std::isfinite(made(0))) { return juce::Result::fail("Can't work out \"" + text + "\" (unknown name, or not a number)."); }
        return juce::Result::ok();
    }
    // An expression of t keyed every so often from 0 to `duration`, each key
    // with the curve's slope there.
    juce::Result expressionKeys(Curve& curve, const juce::var& keys, double duration) const {
        Formula at;
        const auto parsed = formula(keys["expression"].toString(), at);
        if (parsed.failed()) { return parsed; }
        const auto every = keys.hasProperty("every") ? static_cast<double>(keys["every"]) : .25;
        if (!(every > 0)) { return juce::Result::fail("\"every\" is how many seconds apart its keys are."); }
        constexpr double nudge = 1.0e-4;
        const auto count = static_cast<int>(std::ceil(duration / every - 1.0e-9));
        for (int index = 0; index <= count; ++index) {
            Keyframe key;
            key.time = std::min(duration, index * every);
            key.value = at(key.time);
            key.incomingSlope = key.outgoingSlope = (at(key.time + nudge) - at(key.time - nudge)) / (2 * nudge);
            if (!std::isfinite(key.value) || !std::isfinite(key.incomingSlope)) { return juce::Result::fail("It isn't a number at " + juce::String(key.time) + " s."); }
            curve.setKey(key);
        }
        return juce::Result::ok();
    }
    // A camera's path: where it is and the point it looks at, each three
    // expressions of t, keyed every so often as its position and the turn
    // that faces it there (its yaw unwound, so a whole turn goes round).
    juce::Result cameraPath(Camera& camera, const juce::var& path) const {
        const auto* position = path["position"].getArray();
        const auto* look = path["look"].getArray();
        if (position == nullptr || look == nullptr || position->size() != 3 || look->size() != 3) { return juce::Result::fail("A path has \"position\" and \"look\", each three expressions (x, y, z)."); }
        std::array<Formula, 6> axes;
        for (std::size_t axis = 0; axis < axes.size(); ++axis) {
            const auto& text = axis < 3 ? (*position)[static_cast<int>(axis)] : (*look)[static_cast<int>(axis - 3)];
            const auto parsed = formula(text.toString(), axes[axis]);
            if (parsed.failed()) { return parsed; }
        }
        const auto every = path.hasProperty("every") ? static_cast<double>(path["every"]) : .05;
        if (!(every > 0)) { return juce::Result::fail("\"every\" is how many seconds apart its keys are."); }
        const auto duration = project.duration;
        constexpr double degrees = 180 / juce::MathConstants<double>::pi;
        // Where it is, and its pitch and yaw facing the look point.
        const auto pose = [&](double t) {
            const motion::Vec3 at {axes[0](t), axes[1](t), axes[2](t)};
            const motion::Vec3 toward {axes[3](t), axes[4](t), axes[5](t)};
            const auto d = (toward - at).normalized().value_or(motion::Vec3 {0, 0, -1});
            return std::array<double, 5> {at.x, at.y, at.z, std::asin(d.y) * degrees, std::atan2(-d.x, -d.z) * degrees};
        };
        std::array<Curve, 5> curves;
        constexpr double nudge = 1.0e-3;
        double yaw = 0;
        const auto count = static_cast<int>(std::ceil(duration / every - 1.0e-9));
        for (int index = 0; index <= count; ++index) {
            const auto t = std::min(duration, index * every);
            const auto now = pose(t), before = pose(std::max(0.0, t - nudge)), after = pose(std::min(duration, t + nudge));
            const auto span = std::min(duration, t + nudge) - std::max(0.0, t - nudge);
            yaw = index == 0 ? now[4] : yaw + std::remainder(now[4] - yaw, 360.0);
            for (std::size_t axis = 0; axis < curves.size(); ++axis) {
                Keyframe key;
                key.time = t;
                key.value = axis == 4 ? yaw : now[axis];
                const auto change = axis == 4 ? std::remainder(after[axis] - before[axis], 360.0) : after[axis] - before[axis];
                key.incomingSlope = key.outgoingSlope = change / span;
                if (!std::isfinite(key.value) || !std::isfinite(key.incomingSlope)) { return juce::Result::fail("The path isn't a number at " + juce::String(t) + " s."); }
                curves[axis].setKey(key);
            }
        }
        const std::array<const char*, 5> names {"position.x", "position.y", "position.z", "rotation.x", "rotation.y"};
        for (std::size_t axis = 0; axis < curves.size(); ++axis) { camera.properties[names[axis]] = curves[axis]; }
        return juce::Result::ok();
    }
    // What an expression knows: t, pi, the recipe's definitions, and
    // functions past JUCE's own.
    struct Variables final : juce::Expression::Scope {
        Variables(double time, const std::map<juce::String, juce::Expression>& definitions) : time(time), definitions(definitions) {}
        juce::Expression getSymbolValue(const juce::String& symbol) const override {
            if (symbol == "t") { return juce::Expression(time); }
            if (symbol == "pi") { return juce::Expression(juce::MathConstants<double>::pi); }
            const auto defined = definitions.find(symbol);
            if (defined != definitions.end()) { return defined->second; }
            return juce::Expression::Scope::getSymbolValue(symbol);
        }
        double evaluateFunction(const juce::String& name, const double* parameters, int count) const override {
            if (count == 1 && name == "sqrt") { return std::sqrt(parameters[0]); }
            if (count == 1 && name == "asin") { return std::asin(parameters[0]); }
            if (count == 1 && name == "acos") { return std::acos(parameters[0]); }
            if (count == 1 && name == "exp") { return std::exp(parameters[0]); }
            if (count == 1 && name == "floor") { return std::floor(parameters[0]); }
            if (count == 1 && name == "smoothstep") {
                const auto x = std::clamp(parameters[0], 0.0, 1.0);
                return x * x * (3 - 2 * x);
            }
            if (count == 2 && name == "pow") { return std::pow(parameters[0], parameters[1]); }
            if (count == 2 && name == "atan2") { return std::atan2(parameters[0], parameters[1]); }
            if (count == 3 && name == "clamp") { return std::clamp(parameters[0], std::min(parameters[1], parameters[2]), std::max(parameters[1], parameters[2])); }
            return juce::Expression::Scope::evaluateFunction(name, parameters, count);
        }
        double time;
        const std::map<juce::String, juce::Expression>& definitions;
    };
    static std::optional<Interpolation> interpolationNamed(const juce::String& name) {
        const std::map<juce::String, Interpolation> names {{"hold", Interpolation::hold}, {"linear", Interpolation::linear}, {"smooth", Interpolation::smooth}, {"cubic", Interpolation::cubic}};
        const auto found = names.find(name.toLowerCase());
        return found == names.end() ? std::nullopt : std::optional(found->second);
    }

    const juce::File folder;
    Project& project;
    Id next = 100;
    // Objects by name, for the camera to look at.
    std::map<juce::String, Id> named;
    std::map<juce::String, juce::Expression> definitions;
};
}
