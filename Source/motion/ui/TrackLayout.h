#pragma once

#include "../model/Timeline.h"
#include <set>

namespace motion {
struct TrackRow {
    Id id = 0;
    int track = -1;
    int depth = 0;
    bool group() const { return track < 0; }
};

// Preserve authored track order while placing each folder before its children.
// Empty folders remain reachable; collapsed descendants stay in the document.
template <typename ProjectType>
std::vector<TrackRow> trackRows(const ProjectType& project, const std::set<Id>& collapsed) {
    std::vector<TrackRow> rows;
    std::set<Id> emitted;
    const auto emitChildren = [&](auto&& self, Id parent, int depth) -> void {
        if (depth > 32) { return; }
        const auto emitGroup = [&](Id id) {
            if (!emitted.insert(id).second) { return; }
            rows.push_back({ id, -1, depth });
            if (!collapsed.contains(id)) { self(self, id, depth + 1); }
        };
        for (std::size_t index = 0; index < project.tracks.size(); ++index) {
            const auto& track = project.tracks[index];
            if (track.group == parent) {
                rows.push_back({ track.id, static_cast<int>(index), depth });
                continue;
            }
            auto group = track.group;
            for (int guard = 0; group != 0 && guard < 32; ++guard) {
                const auto found = std::find_if(project.groups.begin(), project.groups.end(), [group](const auto& item) { return item.id == group; });
                if (found == project.groups.end()) { break; }
                if (found->parent == parent) { emitGroup(group); break; }
                group = found->parent;
            }
        }
        for (const auto& group : project.groups) {
            if (group.parent == parent) { emitGroup(group.id); }
        }
    };
    emitChildren(emitChildren, 0, 0);
    return rows;
}
}
