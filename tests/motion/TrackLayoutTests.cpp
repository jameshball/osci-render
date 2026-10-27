#include "../../Source/motion/model/Group.h"
#include "../../Source/motion/ui/TrackLayout.h"
#include <cstdlib>
#include <iostream>

struct Project {
    std::vector<motion::Track> tracks;
    std::vector<motion::Group> groups;
};

namespace {
void check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        std::exit(1);
    }
}
}

int main() {
    Project project;
    motion::Group root, nested, empty;
    root.id = 10;
    nested.id = 11;
    nested.parent = 10;
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
    check(rows.size() == expected.size(), "rows.size() == expected.size()");
    for (std::size_t i = 0; i < expected.size(); ++i) { check(rows[i].id == expected[i], "rows in order"); }
    check(rows[2].track == 0 && rows[2].depth == 2, "rows[2].track == 0 && rows[2].depth == 2");
    check(rows[3].track == 2 && rows[3].depth == 1, "rows[3].track == 2 && rows[3].depth == 1");
    rows = motion::trackRows(project, {10});
    check(rows.size() == 4 && rows[0].id == 10 && rows[1].id == 2 && rows[3].id == 12, "rows.size() == 4 && rows[0].id == 10 && rows[1].id == 2 && rows[3].id == 12");
    rows = motion::trackRows(project, {11});
    check(rows.size() == 6 && rows[1].id == 11 && rows[2].id == 3, "rows.size() == 6 && rows[1].id == 11 && rows[2].id == 3");
    check(project.tracks.size() == 4 && project.tracks[0].group == 11, "project.tracks.size() == 4 && project.tracks[0].group == 11");
    std::swap(project.tracks[0], project.tracks[1]);
    rows = motion::trackRows(project, {});
    check(rows[0].id == 2 && rows[1].id == 10 && rows[3].id == 1 && rows[3].track == 1, "rows[0].id == 2 && rows[1].id == 10 && rows[3].id == 1 && rows[3].track == 1");
    project.tracks.clear();
    rows = motion::trackRows(project, {});
    check(rows.size() == 3 && rows[0].id == 10 && rows[1].id == 11 && rows[2].id == 12, "rows.size() == 3 && rows[0].id == 10 && rows[1].id == 11 && rows[2].id == 12");
    std::cout << "Track layout tests passed\n";
}
