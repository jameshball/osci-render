// The Document's edits of its sources: assets, live sources and the clips
// made of them.

#include "Document.h"
#include "DocumentEdits.h"
#include "CompositionGraph.h"
#include "LuaClipBake.h"
#include "ModulationGraph.h"
#include "PropertySchema.h"
#include "../import/SourceDecoding.h"
#include <numbers>
#include <set>

namespace motion {

juce::Result Document::makeSourceUnique(Id clipId, const std::shared_ptr<const Asset>& expected, Asset copy, Id& madeId) {
    madeId = 0;
    const auto& state = project();
    if (expected == nullptr || std::find(state.assets.begin(), state.assets.end(), expected) == state.assets.end()) {
        return juce::Result::fail("The source changed while preparing its copy.");
    }
    const auto references = assetUses(expected->id);
    if (references < 2) { return juce::Result::fail("This clip already has its own source."); }
    for (std::size_t trackIndex = 0; trackIndex < state.tracks.size(); ++trackIndex) {
        const auto& track = state.tracks[trackIndex];
        for (std::size_t clipIndex = 0; clipIndex < track.clips.size(); ++clipIndex) {
            const auto& clip = track.clips[clipIndex];
            if (clip.id != clipId) { continue; }
            if (track.locked || clip.asset != expected->id) { return juce::Result::fail("The selected clip is locked or its source has changed."); }
            const auto highest = highestId();
            if (highest >= maximumId) { return noIdentitiesLeft(); }
            if (copy.liveIdentity != nullptr) { copy.liveIdentity = std::make_shared<const LiveSourceIdentity>(); }
            copy.id = highest + 1;
            copy.name = copy.liveIdentity != nullptr ? expected->name + " copy" : expected->name.upToLastOccurrenceOf(".", false, false) + " copy " + juce::String(static_cast<juce::uint64>(copy.id)) + expected->extension;
            lastId = madeId = copy.id;
            edit("Make source unique", [trackIndex, clipIndex, made = std::make_shared<const Asset>(std::move(copy))](Project& project) {
                project.assets.push_back(made);
                project.tracks.change(trackIndex).clips[clipIndex].asset = made->id;
            });
            return juce::Result::ok();
        }
    }
    return juce::Result::fail("The selected clip no longer exists.");
}

std::size_t Document::assetUses(Id assetId) const {
    return sourceReferenceCount(mainProject(), assetId);
}

std::vector<std::shared_ptr<const Asset>> Document::liveSourcesInUse() const {
    std::vector<std::shared_ptr<const Asset>> result;
    for (const auto& asset : mainProject().assets) {
        if (asset != nullptr && asset->liveIdentity != nullptr && assetUses(asset->id) > 0) { result.push_back(asset); }
    }
    return result;
}

juce::Result Document::drawSourceInstead(Id from, Id to) {
    const auto& assets = mainProject().assets;
    const auto original = findAsset(assets, from);
    const auto replacement = findAsset(assets, to);
    if (original == nullptr || replacement == nullptr) { return juce::Result::fail("The source no longer exists."); }
    if ((original->audio != nullptr) != (replacement->audio != nullptr)) { return juce::Result::fail("A visual source can only be drawn in place of another visual source."); }
    if (from == to || assetUses(from) == 0) { return juce::Result::ok(); }
    // Clips still named after the old source take the new one's name.
    const auto oldName = original->name.toStdString();
    const auto newName = replacement->name.toStdString();
    editProject("Use " + replacement->name, [from, to, &oldName, &newName](Project& project) {
        const auto trackUses = [from](const Track& track) {
            return std::any_of(track.clips.begin(), track.clips.end(), [from](const auto& clip) { return clip.asset == from; });
        };
        const auto uses = [&](const Composition& composition) { return std::any_of(composition.tracks.begin(), composition.tracks.end(), trackUses); };
        changeEachComposition(project, uses, [&](Composition& composition) {
            composition.tracks.changeEach(trackUses, [&](Track& track) {
                for (auto& clip : track.clips) {
                    if (clip.asset != from) { continue; }
                    clip.asset = to;
                    if (clip.name == oldName) { clip.name = newName; }
                }
            });
        });
    });
    return juce::Result::ok();
}

Project Document::withoutLiveSources(Project project) {
    std::set<Id> live;
    for (const auto& asset : project.assets) {
        if (asset != nullptr && asset->liveIdentity != nullptr) { live.insert(asset->id); }
    }
    if (live.empty()) { return project; }
    const auto trackUses = [&live](const Track& track) {
        return std::any_of(track.clips.begin(), track.clips.end(), [&live](const auto& clip) { return live.contains(clip.asset); });
    };
    const auto uses = [&](const Composition& composition) { return std::any_of(composition.tracks.begin(), composition.tracks.end(), trackUses); };
    changeEachComposition(project, uses, [&](Composition& composition) {
        composition.tracks.changeEach(trackUses, [&live](Track& track) { std::erase_if(track.clips, [&live](const auto& clip) { return live.contains(clip.asset); }); });
        pruneReferences(composition);
    });
    return project;
}

bool Document::setLuaBake(Id clipId, std::shared_ptr<const LuaClipBake> bake) {
    // A cache, not an edit: no undo step and no revision-guarded gesture is
    // disturbed beyond a normal state refresh.
    bool found = false;
    auto next = state;
    changeEachComposition(next, [clipId](const Composition& composition) { return findClip(composition, clipId) != nullptr; }, [&](Composition& composition) {
        changeClip(composition, clipId)->luaBake = bake;
        found = true;
    });
    if (!found) { return false; }
    // A cache: publish the new frames without an edit's revision, so open
    // gestures, coalescing and change guards are undisturbed.
    state = std::move(next);
    refreshScope();
    if (onChanged) { onChanged(); }
    sendChangeMessage();
    return true;
}

juce::Result Document::replaceAsset(Id assetId, std::shared_ptr<const Asset> replacement) {
    const auto& assets = project().assets;
    const auto found = findAsset(assets, assetId);
    if (found == nullptr) { return juce::Result::fail("The source no longer exists."); }
    if (replacement == nullptr || replacement->id != assetId) { return juce::Result::fail("Invalid replacement source."); }
    const auto audio = [](const Asset& asset) { return asset.audio != nullptr; };
    if (audio(*found) != audio(*replacement)) {
        return juce::Result::fail(audio(*found) ? "Replace audio with another audio file." : "Replace a visual source with another visual file.");
    }
    const bool lua = replacement->extension.equalsIgnoreCase(".lua");
    const bool fractal = isFractal(replacement->extension);
    const auto* generator = generatorOf(*replacement);
    // Every clip of it, in every composition, not only the one being edited.
    editProject("Replace source", [assetId, replacement, lua, fractal, generator](Project& project) {
        for (auto& asset : project.assets) {
            if (asset != nullptr && asset->id == assetId) { asset = replacement; }
        }
        // The old source's own properties (a script's sliders, a
        // generator's parameters) and a script's bake go with it; routes and
        // links to them are pruned.
        const auto ownedByNew = [&](const Track& track, const std::string& name) {
            const auto specs = track.kind == TrackKind::audio ? std::span<const PropertySpec>(audioPropertySpecs) : std::span<const PropertySpec>(objectPropertySpecs);
            return findPropertySpec(specs, name) != nullptr || (lua && name.starts_with("slider.")) || (fractal && findPropertySpec(fractalPropertySpecs, name) != nullptr)
                || (generator != nullptr && findPropertySpec(generator->parameters, name) != nullptr);
        };
        const auto trackUses = [assetId](const Track& track) {
            return std::any_of(track.clips.begin(), track.clips.end(), [assetId](const auto& clip) { return clip.asset == assetId; });
        };
        const auto clear = [&](Composition& composition) {
            composition.tracks.changeEach(trackUses, [&](Track& track) {
                for (auto& clip : track.clips) {
                    if (clip.asset != assetId) { continue; }
                    clip.luaBake.reset();
                    std::erase_if(clip.properties, [&](const auto& item) { return !ownedByNew(track, item.first); });
                }
            });
            pruneReferences(composition);
        };
        const auto uses = [&](const Composition& composition) {
            return std::any_of(composition.tracks.begin(), composition.tracks.end(), trackUses);
        };
        changeEachComposition(project, uses, clear);
    });
    return juce::Result::ok();
}

juce::Result Document::renameAsset(Id assetId, juce::String name) {
    name = name.trim();
    if (name.isEmpty() || name.length() > 200 || name.containsAnyOf("\r\n")) { return juce::Result::fail(juce::String::fromUTF8("Use a source name of 1\xe2\x80\x93" "200 characters on one line.")); }
    const auto& assets = mainProject().assets;
    const auto found = findAsset(assets, assetId);
    if (found == nullptr) { return juce::Result::fail("The source no longer exists."); }
    if (found->name == name) { return juce::Result::ok(); }
    edit("Rename source", [assetId, name](Project& project) {
        for (auto& asset : project.assets) {
            if (asset != nullptr && asset->id == assetId) {
                auto renamed = std::make_shared<Asset>(*asset);
                renamed->name = name;
                asset = std::move(renamed);
            }
        }
    });
    return juce::Result::ok();
}

juce::Result Document::removeUnusedAssets(std::vector<Id> assetIds, int& removed) {
    removed = 0;
    const auto& assets = mainProject().assets;
    if (assetIds.empty()) {
        for (const auto& asset : assets) { if (asset != nullptr) { assetIds.push_back(asset->id); } }
    }
    std::set<Id> unused;
    for (const auto id : assetIds) {
        if (findAsset(assets, id) != nullptr && assetUses(id) == 0) { unused.insert(id); }
    }
    if (unused.empty()) { return juce::Result::fail("Only sources that no clip uses can be removed."); }
    removed = static_cast<int>(unused.size());
    edit(unused.size() > 1 ? "Remove unused sources" : "Remove source", [&unused](Project& project) {
        std::erase_if(project.assets, [&](const auto& asset) { return asset != nullptr && unused.contains(asset->id); });
    });
    return juce::Result::ok();
}

juce::Result Document::addBlenderSource(juce::String name, BlenderSourceSettings settings, Id& id) {
    if (!settings.valid() || name.trim().isEmpty()) { return juce::Result::fail("Enter a source name and a port from 51600 to 51699."); }
    auto asset = std::make_shared<Asset>();
    asset->id = newId();
    asset->name = name.trim();
    asset->extension = ".blender";
    asset->blenderSettings = settings;
    const auto result = decodeAsset(*asset);
    if (result.failed()) { return result; }
    edit("Add Blender source", [asset](Project& project) { project.assets.push_back(asset); });
    id = asset->id;
    return juce::Result::ok();
}

juce::Result Document::addTextureSource(const juce::MemoryBlock& sender, const juce::String& name, RasterSettings settings, Id& id) {
    auto asset = std::make_shared<Asset>();
    asset->id = newId();
    asset->name = uniqueAssetName(mainProject(), name.trim().isEmpty() ? juce::String("Live picture") : name.trim());
    asset->extension = ".texture";
    asset->data = sender;
    asset->rasterSettings = settings;
    const auto result = decodeAsset(*asset);
    if (result.failed()) { return result; }
    edit("Add live picture", [asset](Project& project) { project.assets.push_back(asset); });
    id = asset->id;
    return juce::Result::ok();
}

juce::Result Document::setTextureSettings(Id id, RasterSettings settings) {
    const auto found = findAsset(state.assets, id);
    if (found == nullptr || found->liveIdentity == nullptr || !found->extension.equalsIgnoreCase(".texture")) { return juce::Result::fail("The live picture no longer exists."); }
    const auto error = settings.validate();
    if (!error.empty()) { return juce::Result::fail(juce::String(error)); }
    auto replacement = std::make_shared<Asset>(*found);
    replacement->rasterSettings = settings;
    tryEditCoalesced("Change live picture", "texture:" + juce::String(id), [id, replacement](Project& project) {
        for (auto& asset : project.assets) { if (asset->id == id) { asset = replacement; return true; } }
        return false;
    });
    return juce::Result::ok();
}

juce::Result Document::setBlenderSource(Id id, juce::String name, BlenderSourceSettings settings) {
    const auto found = findAsset(state.assets, id);
    if (found == nullptr || !found->extension.equalsIgnoreCase(".blender")) { return juce::Result::fail("The Blender source no longer exists."); }
    if (!settings.valid() || name.trim().isEmpty()) { return juce::Result::fail("Enter a source name and a port from 51600 to 51699."); }
    if (found->name == name.trim() && found->blenderSettings == settings) { return juce::Result::ok(); }
    auto replacement = std::make_shared<Asset>(*found);
    replacement->name = name.trim();
    replacement->blenderSettings = settings;
    if (found->blenderSettings.port != settings.port) { replacement->liveIdentity = std::make_shared<const LiveSourceIdentity>(); }
    edit("Change Blender source", [id, replacement](Project& project) {
        for (auto& asset : project.assets) { if (asset->id == id) { asset = replacement; break; } }
    });
    return juce::Result::ok();
}

Clip Document::makeClip(Id id, const Asset& asset, double time) {
    Clip clip;
    clip.id = id;
    clip.asset = asset.id;
    clip.name = asset.name.toStdString();
    clip.start = time;
    if (asset.audio != nullptr) {
        clip.duration = asset.audio->duration();
        clip.properties = defaultProperties(audioPropertySpecs);
        return clip;
    }
    if (asset.source != nullptr && (asset.source->frameCount() > 1 || asset.extension.equalsIgnoreCase(".lua") || asset.extension.equalsIgnoreCase(".blender-capture") || osci::files::isVideo(asset.extension))) {
        clip.duration = asset.source->duration();
    }
    clip.properties = defaultProperties(objectPropertySpecs);
    // What has depth (a solid, a grid, a model) starts solid, hiding what is
    // behind it; a flat drawing (a shape, text, an SVG's fill) starts a
    // wireframe, so flat designs laid over each other all show.
    const auto* generator = generatorOf(asset);
    const auto* drawing = asset.source != nullptr && asset.source->frameCount() > 0 ? asset.source->drawingAt(0) : nullptr;
    const auto flat = generator != nullptr ? generator->category == GeneratorCategory::shape : drawing != nullptr && drawing->faces().planar;
    if (flat) { clip.properties["solid"] = Curve(0); }
    // An L-system's clip starts at the depth chosen as it was imported.
    if (isFractal(asset.extension)) { clip.properties["fractal.depth"] = Curve(asset.fractalDepth); }
    // A generator's clip holds its own values of the generator's parameters.
    // A solid starts turned, seen a little from above and the side, so it
    // reads as 3D in a camera looking straight at it.
    if (generator != nullptr) {
        for (const auto& spec : generator->parameters) { clip.properties[std::string(spec.id)] = Curve(spec.defaultValue); }
        if (generator->category == GeneratorCategory::solid) {
            clip.properties["rotation.x"] = Curve(25);
            clip.properties["rotation.y"] = Curve(-35);
        }
        // A light starts white, above and to the left of the middle of the
        // scene, in front (clear of the Scene's axes in its corner); one that
        // shines one way shines at the middle.
        if (generator->category == GeneratorCategory::light) {
            const auto place = [&](double x, double y, double z) {
                clip.properties["position.x"] = Curve(x);
                clip.properties["position.y"] = Curve(y);
                clip.properties["position.z"] = Curve(z);
                // Unturned, a light shines down -Z: turned about X, then Y, to the middle.
                const auto length = std::sqrt(x * x + y * y + z * z);
                if (length > 0) {
                    constexpr auto degrees = 180 / std::numbers::pi;
                    clip.properties["rotation.x"] = Curve(std::asin(-y / length) * degrees);
                    clip.properties["rotation.y"] = Curve(std::atan2(x, z) * degrees);
                }
            };
            // Near enough to be in the Scene's view as it opens.
            switch (generator->kind) {
                case GeneratorKind::ambientLight: place(.8, -.6, 0); break;
                case GeneratorKind::directionalLight: place(-.8, .6, .6); break;
                case GeneratorKind::pointLight: place(-.5, .35, 1.6); break;
                default: place(-.35, .45, 2); break;
            }
            return clip;
        }
    }
    // A source without its own colours starts in the phosphor green.
    if (asset.source == nullptr || !asset.source->hasExplicitColour()) {
        clip.properties["red"] = Curve(0.2);
        clip.properties["blue"] = Curve(0.35);
    }
    return clip;
}
}
