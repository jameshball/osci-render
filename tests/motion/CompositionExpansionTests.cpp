#include "../../Source/motion/model/CompositionExpansion.h"
#include <cstdlib>
#include <iostream>

namespace {
struct Scope {
    double duration = 20, bpm = 120;
    std::vector<motion::Track> tracks;
    std::vector<motion::Group> groups;
};
struct Definition : Scope { motion::Id id = 0; };
struct Project : Scope { std::vector<std::shared_ptr<const Definition>> definitions; };
void check(bool value, const char* message) { if (!value) { std::cerr << message << '\n'; std::exit(1); } }
void near(double a, double b, const char* message) { check(std::abs(a - b) < 1e-10, message); }
}
int main() {
    Project project;
    auto definition = std::make_shared<Definition>();
    definition->id = 1;
    definition->duration = 8;
    motion::Clip leaf; leaf.id = 2; leaf.asset = 3; leaf.start = 1; leaf.duration = 6; leaf.offset = 10; leaf.rate = 0.5;
    motion::Track childTrack; childTrack.clips = {leaf}; definition->tracks = {childTrack};
    project.definitions = {definition};
    motion::Clip instance; instance.id = 4; instance.composition = 1; instance.start = 4; instance.duration = 3; instance.offset = 2; instance.rate = 2;
    motion::Track rootTrack; rootTrack.clips = {instance}; project.tracks = {rootTrack};
    std::vector<std::vector<motion::CompositionStage>> leaves;
    auto expand = [&] { leaves.clear(); return motion::expandComposition(project, [&](const auto&, const auto& stages) { leaves.push_back(stages); }); };
    check(static_cast<bool>(expand()) && leaves.size() == 1, "one nested leaf");
    const auto stages = leaves[0];
    check(stages.size() == 2, "instance ancestry retained");
    near(stages[1].clipClock.start, 4, "parent trim clips child start");
    near(stages[1].clipClock.end(), 6.5, "child end maps into main clock");
    near(stages[1].clipClock.localTime(5), 11.5, "source clock composes offset and rate");
    near(stages[1].scopeClock.localTime(5), 4, "track and group clock remains definition local");
    near(stages[0].scopeClock.localTime(5), 5, "outer track clock remains main time");
    near(stages[0].clipClock.localTime(5), 4, "instance animation follows its own source time");
    auto second = instance; second.id = 5; second.start = 10; second.rate = 1;
    project.tracks[0].clips.push_back(second);
    check(static_cast<bool>(expand()) && leaves.size() == 2, "shared definition expands independent instances");
    near(leaves[1][1].clipClock.localTime(11), 11, "second instance has independent clock");
    project.tracks[0].muted = true;
    check(static_cast<bool>(expand()) && leaves.empty(), "muted parent hides entire subtree");
    project.tracks[0].muted = false;
    definition->tracks[0].muted = true;
    check(static_cast<bool>(expand()) && leaves.empty(), "muted child hides leaf");
    definition->tracks[0].muted = false;
    definition->duration = 3;
    check(static_cast<bool>(expand()), "short definition expands");
    near(leaves[0][1].clipClock.end(), 4.5, "definition duration clips long instance");
    definition->duration = 8;
    auto outer = std::make_shared<Definition>(); outer->id = 20; outer->bpm = 60;
    auto innerInstance = instance; innerInstance.start = 2; innerInstance.duration = 8;
    innerInstance.offset = 0; innerInstance.rate = 1; innerInstance.timeBase = motion::ClipTimeBase::beats;
    innerInstance.contentBpm = 120;
    motion::Track intermediate; intermediate.clips = {innerInstance}; outer->tracks = {intermediate};
    project.definitions.push_back(outer);
    auto mainInstance = instance; mainInstance.composition = 20; mainInstance.start = 1;
    mainInstance.duration = 10; mainInstance.offset = 0; mainInstance.rate = 2;
    project.tracks[0].clips = {mainInstance};
    check(static_cast<bool>(expand()) && leaves.size() == 1 && leaves[0].size() == 3, "two levels with mixed tempos expand");
    near(leaves[0][2].clipClock.start, 3, "beat placement resolves at owning tempo before nesting");
    near(leaves[0][2].clipClock.localTime(4), 10.5, "composed source clock preserves authored time");
    near(leaves[0][1].scopeClock.localTime(4), 6, "intermediate scope clock is independent of beat conversion");
    project.tracks[0].clips[0].rate = std::numeric_limits<double>::max();
    outer->tracks[0].clips[0].rate = 4;
    check(!expand(), "composed speed overflow reports an error rather than silently dropping content");
    project.tracks[0].clips = {instance};
    outer->tracks[0].clips[0].rate = 1;
    std::atomic<bool> cancel {true};
    bool visited = false;
    check(!motion::expandComposition(project, [&](const auto&, const auto&) { visited = true; }, &cancel) && !visited, "cancelled preparation never visits");
    definition->tracks[0].clips[0].asset = 0; definition->tracks[0].clips[0].composition = 1;
    check(!expand() && leaves.empty(), "cycle rejected before any visitor");
    std::cout << "Composition expansion contracts passed\n";
}
