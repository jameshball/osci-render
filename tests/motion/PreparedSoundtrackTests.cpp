#include "../../Source/motion/render/PreparedSoundtrack.h"
#include <cstdlib>
#include <iostream>

namespace {
void check(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
bool near(float value, double expected) { return std::abs(value - expected) < 1.0e-6; }
struct Asset {
    motion::Id id = 1;
    std::shared_ptr<const motion::PreparedAudio> audio;
};
struct Scope {
    double bpm = 120, duration = 20;
    motion::Tempo tempo() const { return motion::Tempo(bpm); }
    std::vector<motion::Track> tracks;
    std::vector<motion::Group> groups;
    std::vector<motion::EffectInstance> effects;
};
struct Definition : Scope { motion::Id id = 0; };
struct Project : Scope {
    std::vector<std::shared_ptr<const Asset>> assets;
    std::vector<std::shared_ptr<const Definition>> definitions;
};
Project fixture() {
    const std::vector<float> pcm { 0, 1, 0, -1 };
    const std::array<std::span<const float>, 1> channels { pcm };
    auto source = motion::PreparedAudio::fromPlanar(4, channels);
    check(static_cast<bool>(source), "source prepares");
    Project project;
    project.assets.push_back(std::make_shared<Asset>(Asset { 1, source.audio }));
    motion::Track track;
    track.id = 2;
    track.kind = motion::TrackKind::audio;
    motion::Clip clip;
    clip.id = 3;
    clip.asset = 1;
    clip.start = 2;
    clip.duration = 1;
    track.clips.push_back(clip);
    project.tracks.push_back(track);
    return project;
}
}

int main() {
    auto project = fixture();
    motion::PreparedSoundtrack prepared(project);
    check(near(prepared.sample(2.125).left, 0.5), "project clock maps to interpolated source time");
    check(near(prepared.sample(2.25).left, 1) && near(prepared.sample(2.25).right, 1), "default gain and pan preserve both channels");
    check(prepared.sample(1.99).left == 0 && prepared.sample(3).left == 0, "clip boundaries are half open");
    check(prepared.sample(-1).left == 0 && prepared.sample(std::numeric_limits<double>::infinity()).left == 0,
          "invalid or out of range time is silent");
    for (int repetition = 0; repetition < 5; ++repetition) {
        prepared.sample(2.75);
        check(prepared.sample(2.25).left == 1, "seek and repeat are stateless");
    }
    auto& clip = project.tracks[0].clips[0];
    clip.offset = 0.25;
    clip.rate = 2;
    motion::PreparedSoundtrack shifted(project);
    check(shifted.sample(2).left == 1 && shifted.sample(2.25).left == -1, "offset and playback rate use content seconds");
    check(shifted.sample(2.5).left == 0, "content beyond source duration is silent");
    clip.offset = 0;
    clip.rate = 1;
    clip.properties["gain"] = motion::Curve(2);
    clip.properties["pan"] = motion::Curve(0.5);
    motion::PreparedSoundtrack balanced(project);
    check(balanced.sample(2.25).left == 1 && balanced.sample(2.25).right == 2, "positive balance attenuates left with unity centre");
    clip.properties["gain"] = motion::Curve(10);
    clip.properties["pan"] = motion::Curve(-5);
    motion::PreparedSoundtrack bounded(project);
    check(bounded.sample(2.25).left == 4 && bounded.sample(2.25).right == 0, "gain and pan evaluated values are bounded");
    clip.properties["gain"] = motion::Curve(1);
    clip.properties["pan"] = motion::Curve(0);
    clip.properties["gain"].setKeyValue(0, 0);
    clip.properties["gain"].setKeyValue(0.5, 2);
    motion::PreparedSoundtrack animated(project);
    check(near(animated.sample(2.25).left, 1), "gain automation evaluates in content-local time");
    clip.properties.clear();
    auto second = project.tracks[0];
    second.id = 4;
    second.clips[0].id = 5;
    project.tracks.push_back(second);
    motion::PreparedSoundtrack mix(project);
    check(mix.sample(2.25).left == 2, "simultaneous tracks sum without normalising or threshold clipping");
    project.tracks[1].muted = true;
    check(motion::PreparedSoundtrack(project).sample(2.25).left == 1, "muted audio tracks are excluded");
    project.tracks[1].muted = false;
    project.tracks[1].kind = motion::TrackKind::visual;
    check(motion::PreparedSoundtrack(project).sample(2.25).left == 1, "visual tracks never enter soundtrack");
    project.tracks[1].solo = true;
    check(motion::PreparedSoundtrack(project).sample(2.25).left == 0, "solo filtering is shared with visual tracks");
    project.tracks[1].solo = false;
    motion::Group group;
    group.id = 6;
    group.muted = true;
    project.groups.push_back(group);
    project.tracks[0].group = 6;
    check(motion::PreparedSoundtrack(project).sample(2.25).left == 0, "muted ancestor excludes soundtrack");
    project.groups[0].muted = false;
    project.groups[0].solo = true;
    check(motion::PreparedSoundtrack(project).sample(2.25).left == 1, "solo ancestor includes soundtrack");
    project.tracks[0].group = 999;
    check(motion::PreparedSoundtrack(project).sample(2.25).left == 0, "missing ancestor cannot produce audio");
    project = fixture();
    auto definition = std::make_shared<Definition>();
    definition->id = 10; definition->tracks = project.tracks;
    project.definitions = {definition};
    motion::Clip instance; instance.id = 11; instance.composition = 10;
    instance.start = 5; instance.duration = 2; instance.offset = 2; instance.rate = 2;
    instance.properties["gain"] = motion::Curve(0.5);
    motion::Track container; container.clips = {instance}; project.tracks = {container};
    motion::PreparedSoundtrack nested(project);
    check(nested.preparationError.empty(), "nested soundtrack prepares");
    check(near(nested.sample(5.125).left, 0.5), "nested rate and inherited gain reach source audio");
    check(nested.sample(4.99).left == 0 && nested.sample(5.5).left == 0, "nested boundaries remain silent");
    auto repeated = instance; repeated.id = 12; repeated.start = 10; repeated.rate = 1;
    project.tracks[0].clips.push_back(repeated);
    motion::PreparedSoundtrack shared(project);
    check(near(shared.sample(10.25).left, 0.5) && near(shared.sample(5.125).left, 0.5), "shared source instances seek independently");
    project.tracks[0].muted = true;
    check(motion::PreparedSoundtrack(project).sample(5.125).left == 0, "muting visual instance track also mutes nested audio");
    std::atomic<bool> cancelled {true};
    motion::PreparedSoundtrack interrupted(project, &cancelled);
    check(!interrupted.preparationError.empty() && interrupted.sample(5.125).left == 0, "cancelled soundtrack is explicitly invalid and silent");
    std::cout << "Prepared soundtrack tests passed\n";
}
