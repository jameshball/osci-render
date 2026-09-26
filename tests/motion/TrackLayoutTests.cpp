#include "../../Source/motion/model/Group.h"
#include "../../Source/motion/ui/TrackLayout.h"
#include <cassert>

struct Project {
    std::vector<motion::Track> tracks;
    std::vector<motion::Group> groups;
};

int main() {
    Project project;
    motion::Group root, nested, empty;
    root.id = 10;
    nested.id = 11; nested.parent = 10;
    empty.id = 12;
    project.groups = { root, nested, empty };
    for (int i = 0; i < 4; ++i) {
        motion::Track track;
        track.id = i + 1;
        track.group = i == 0 ? 11 : (i == 2 ? 10 : 0);
        project.tracks.push_back(track);
    }
    auto rows = motion::trackRows(project, {});
    const std::vector<motion::Id> expected {10, 11, 1, 3, 2, 4, 12};
    assert(rows.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) { assert(rows[i].id == expected[i]); }
    assert(rows[2].track == 0 && rows[2].depth == 2);
    assert(rows[3].track == 2 && rows[3].depth == 1);
    rows = motion::trackRows(project, {10});
    assert(rows.size() == 4 && rows[0].id == 10 && rows[1].id == 2 && rows[3].id == 12);
    rows = motion::trackRows(project, {11});
    assert(rows.size() == 6 && rows[1].id == 11 && rows[2].id == 3);
    assert(project.tracks.size() == 4 && project.tracks[0].group == 11);
    std::swap(project.tracks[0], project.tracks[1]);
    rows = motion::trackRows(project, {});
    assert(rows[0].id == 2 && rows[1].id == 10 && rows[3].id == 1 && rows[3].track == 1);
    project.tracks.clear();
    rows = motion::trackRows(project, {});
    assert(rows.size() == 3 && rows[0].id == 10 && rows[1].id == 11 && rows[2].id == 12);
}
