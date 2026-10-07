#pragma once

#include "../model/Document.h"
#include "../model/ModulationGraph.h"
#include "PreparedEffects.h"
#include <functional>
#include <map>
#include <tuple>

namespace motion {
// Attaches routed modulators and resolved property links to prepared curve
// copies. Preparation-thread only; the attached drivers are immutable.
class PreparedDrivers {
public:
    using Loudness = std::function<std::shared_ptr<const SoundtrackEnvelope>()>;
    explicit PreparedDrivers(Loudness loudness) : loudness(std::move(loudness)) {}

    // `scopeClock` maps project seconds to the scope's own seconds.
    void drive(Curve& curve, const Composition& scope, const ClipTiming& scopeClock, Id owner, std::string_view property) {
        drive(curve, scope, scopeClock, owner, property, 0);
    }
    // The Scope's picture: the main composition's routes and links, on
    // project-time curves the composition scope cannot look up itself.
    void driveBeam(Curve& curve, const Project& project, const ClipTiming& projectClock, std::string_view property) {
        attach(curve, project, projectClock, project.beam.id, ClipTiming(0.0, project.duration), property, 0);
    }
    void driveEffects(std::vector<PreparedEffect>& prepared, const std::vector<EffectInstance>& effects, const Composition& scope, const ClipTiming& scopeClock) {
        for (std::size_t index = 0; index < prepared.size() && index < effects.size(); ++index) {
            const auto* definition = effectDefinition(effects[index].type);
            if (definition == nullptr) { continue; }
            for (std::size_t parameter = 0; parameter < prepared[index].count && parameter < definition->parameters.size(); ++parameter) {
                drive(prepared[index].curves[parameter], scope, scopeClock, effects[index].id, definition->parameters[parameter].id);
            }
        }
    }

private:
    using ScopeKey = std::tuple<const Composition*, double, double, double>;
    struct ScopeState {
        std::map<Id, std::shared_ptr<const PreparedModulator>> modulators;
        std::map<std::pair<Id, std::string>, std::shared_ptr<const Curve>> sources;
    };
    // A scope's routes by the property they drive.
    using RouteIndex = std::map<std::pair<Id, std::string_view>, std::vector<const ModulationRoute*>>;
    static constexpr int maximumLinkDepth = 64;

    ScopeState& state(const Composition& scope, const ClipTiming& clock) {
        return scopes[ScopeKey {&scope, clock.start, clock.offset, clock.rate}];
    }
    const std::vector<const ModulationRoute*>* routesInto(const Composition& scope, Id owner, std::string_view property) {
        auto [index, added] = routeIndexes.try_emplace(&scope);
        if (added) {
            for (const auto& route : scope.routes) { index->second[{route.target, route.property}].push_back(&route); }
        }
        const auto found = index->second.find({owner, property});
        return found != index->second.end() ? &found->second : nullptr;
    }

    // Most curves have no route or link; only driven ones need their owner's clock.
    void drive(Curve& curve, const Composition& scope, const ClipTiming& scopeClock, Id owner, std::string_view property, int depth) {
        if (!curve.link.has_value() && routesInto(scope, owner, property) == nullptr) { return; }
        const auto target = findPropertyTarget(scope, owner);
        if (!target.has_value()) { return; }
        attach(curve, scope, scopeClock, owner, target->clock(), property, depth);
    }
    void attach(Curve& curve, const Composition& scope, const ClipTiming& scopeClock, Id owner, const ClipTiming& ownerClock, std::string_view property, int depth) {
        const auto* routes = routesInto(scope, owner, property);
        if (routes == nullptr && !curve.link.has_value()) { return; }
        auto drivers = std::make_shared<CurveDrivers>();
        drivers->clock = ownerClock;
        drivers->projectClock = scopeClock;
        if (routes != nullptr) {
            for (const auto* route : *routes) {
                auto modulator = prepare(scope, scopeClock, route->modulator);
                if (modulator != nullptr) { drivers->routes.push_back({std::move(modulator), route->amount, route->mode}); }
            }
        }
        if (curve.link.has_value() && depth < maximumLinkDepth) {
            const auto source = findPropertyTarget(scope, curve.link->source);
            auto sourceCurve = source.has_value() ? linkedSource(scope, scopeClock, curve.link->source, curve.link->property, depth + 1) : nullptr;
            if (sourceCurve != nullptr) {
                drivers->link = *curve.link;
                drivers->linkSource = std::move(sourceCurve);
                drivers->linkClock = source->clock();
            }
        }
        if (drivers->routes.empty() && drivers->linkSource == nullptr) { return; }
        curve.drivers = std::move(drivers);
    }

    std::shared_ptr<const Curve> linkedSource(const Composition& scope, const ClipTiming& scopeClock, Id owner, std::string_view property, int depth) {
        auto& cache = state(scope, scopeClock).sources;
        const auto key = std::make_pair(owner, std::string(property));
        const auto found = cache.find(key);
        if (found != cache.end()) { return found->second; }
        const auto* authored = findPropertyCurve(scope, owner, property);
        if (authored == nullptr) { return nullptr; }
        auto copy = std::make_shared<Curve>(*authored);
        copy->drivers.reset();
        drive(*copy, scope, scopeClock, owner, property, depth);
        std::shared_ptr<const Curve> result = std::move(copy);
        cache.insert_or_assign(key, result);
        return result;
    }

    std::shared_ptr<const PreparedModulator> prepare(const Composition& scope, const ClipTiming& scopeClock, Id id) {
        auto& cache = state(scope, scopeClock).modulators;
        const auto found = cache.find(id);
        if (found != cache.end()) { return found->second; }
        const auto modulator = std::find_if(scope.modulators.begin(), scope.modulators.end(), [id](const auto& item) { return item.id == id; });
        if (modulator == scope.modulators.end() || !modulator->valid()) { return nullptr; }
        auto prepared = std::make_shared<PreparedModulator>();
        prepared->shape = modulator->shape;
        prepared->shape.enabled = true;
        prepared->shape.amount = 1;
        prepared->shape.mode = ModulationMode::add;
        prepared->bpm = scope.bpm;
        if (scope.tempoChanges != nullptr) { prepared->tempo = scope.tempo(); }
        if (modulator->shape.waveform == ModulationWaveform::soundtrack && loudness) { prepared->soundtrack = loudness(); }
        std::shared_ptr<const PreparedModulator> result = std::move(prepared);
        cache.emplace(id, result);
        return result;
    }

    Loudness loudness;
    std::map<ScopeKey, ScopeState> scopes;
    std::map<const Composition*, RouteIndex> routeIndexes;
};
}
