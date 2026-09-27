#include "../../Source/motion/model/CompositionGraph.h"
#include <memory>
#include <iostream>
#include <cstdlib>

namespace {
struct Definition { motion::Id id = 0; std::vector<motion::Track> tracks; };
struct Project { std::vector<motion::Track> tracks; std::vector<std::shared_ptr<const Definition>> definitions; };
void check(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; std::exit(1); } }
motion::Track references(std::initializer_list<motion::Id> ids) {
    motion::Track track;
    for (auto id : ids) { motion::Clip clip; clip.composition = id; track.clips.push_back(clip); }
    return track;
}
}
int main() {
    Project project;
    auto leaf = std::make_shared<Definition>(); leaf->id = 1; leaf->tracks = {references({0})};
    auto left = std::make_shared<Definition>(); left->id = 2; left->tracks = {references({1})};
    auto right = std::make_shared<Definition>(); right->id = 3; right->tracks = {references({1})};
    project.definitions = {leaf, left, right}; project.tracks = {references({2, 3})};
    const auto diamond = motion::validateCompositionGraph(project);
    check(static_cast<bool>(diamond) && diamond.expandedClips == 6 && diamond.depth == 2, "repeated shared definitions are not cycles");
    leaf->tracks = {references({2})};
    check(!motion::validateCompositionGraph(project), "indirect cycle rejected");
    project.tracks.clear();
    check(!motion::validateCompositionGraph(project), "unused cycle rejected");
    leaf->tracks = {references({1})};
    check(!motion::validateCompositionGraph(project), "self cycle rejected");
    leaf->tracks = {references({99})};
    check(!motion::validateCompositionGraph(project), "missing definition rejected");
    leaf->tracks = {references({0})};
    project.definitions.push_back(leaf);
    check(!motion::validateCompositionGraph(project), "duplicate definition identity rejected");
    project.definitions.pop_back(); project.tracks = {references({1})};
    project.tracks[0].clips[0].asset = 10;
    check(!motion::validateCompositionGraph(project), "ambiguous source reference rejected");
    project = {};
    for (motion::Id id = 1; id <= 33; ++id) {
        auto definition = std::make_shared<Definition>(); definition->id = id;
        definition->tracks = {references({id == 33 ? 0 : id + 1})};
        project.definitions.push_back(definition);
    }
    project.tracks = {references({2})};
    check(static_cast<bool>(motion::validateCompositionGraph(project)), "32 instance levels accepted even with shared memoised tail");
    project.tracks = {references({1})};
    check(!motion::validateCompositionGraph(project), "33 levels rejected after memoisation");
    project = {};
    for (motion::Id id = 1; id <= 17; ++id) {
        auto definition = std::make_shared<Definition>(); definition->id = id;
        definition->tracks = {id == 17 ? references({0}) : references({id + 1, id + 1})};
        project.definitions.push_back(definition);
    }
    project.tracks = {references({1})};
    check(!motion::validateCompositionGraph(project), "exponential shared graph rejected without expanding it");
    std::cout << "Composition graph contracts passed\n";
}
