#pragma once

#include "../model/Vec3.h"
#include "SurfaceLines.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <tuple>
#include <vector>

// Hatching laid on a surface as one Sierpinski curve over its own faces: a
// curve that halves triangles, so it lives on the mesh itself, with no seams
// and nothing laid from outside it. Its faces are put in one loop first:
// split about their middles, halving the sides a tree through them crosses,
// they are walked round that tree, each piece sharing a side with the next.
// Each piece is then halved and halved again, across the side the curve runs
// from its way in to its way out of it, its longest where it can be, till its
// pieces are no wider than the finest the curve needs, so a big face carries
// as much of the curve as its size. Pure geometry, so it can be tested alone.
namespace motion::hidden::hatching::sierpinski {
// The curve over a mesh, as a tree of the pieces it passes through: the
// pieces of its faces, halved and halved again (the leaves, in the curve's
// order), and above the faces' pieces, runs of them along the loop taken
// two at a time, so the curve can be as coarse as the light asks.
struct Curve {
    struct Node {
        // Its halves, the first the curve passes through first (none for a leaf).
        std::int32_t first = -1, second = -1;
        // Leaves from `lo` to `hi` (not included) are in it.
        std::uint32_t lo = 0, hi = 0;
        // A piece of a face: the face, and its corners by their weights on it.
        std::uint32_t face = 0;
        std::array<surface::Weights, 3> corners {};
        bool piece = false;
    };
    std::vector<Node> nodes;
    // Each loop's top (one for each part of the mesh not joined to the rest).
    std::vector<std::uint32_t> roots;
    // The leaves in the curve's order, by node.
    std::vector<std::uint32_t> leaves;

    // Where a node is: its middle, on its face (a run's: its middle leaf's).
    std::pair<std::uint32_t, surface::Weights> middleOf(std::uint32_t index) const {
        const auto* node = &nodes[index];
        if (!node->piece) { node = &nodes[leaves[(node->lo + node->hi) / 2]]; }
        surface::Weights middle {};
        for (const auto& corner : node->corners) {
            for (std::size_t axis = 0; axis < 3; ++axis) { middle[axis] += corner[axis] / 3; }
        }
        return {node->face, middle};
    }

    // `finest`: how wide, at most, a leaf is, in the mesh's units.
    static Curve of(const std::vector<Vec3>& corners, const std::vector<surface::Face>& faces, double finest);
};

namespace detail {
using Place = std::tuple<std::int64_t, std::int64_t, std::int64_t>;

// A piece of a face as the loop walks it: its corners by weights, and, for
// a half of a side the tree crosses, the side and which of its corners it
// is by (the side's first or second).
struct Piece {
    std::uint32_t face;
    std::array<surface::Weights, 3> corners;
    std::int32_t side = -1;
    bool second = false;
};

inline surface::Weights mix(const surface::Weights& a, const surface::Weights& b) { return {(a[0] + b[0]) / 2, (a[1] + b[1]) / 2, (a[2] + b[2]) / 2}; }

// How far along a Hilbert curve through the cube a point (each axis 0 to 1)
// is, to `bits` bits an axis (Skilling's transpose, interleaved).
inline std::uint64_t hilbertIndex(std::array<double, 3> point, int bits = 10) {
    const auto top = (1u << bits) - 1;
    std::array<std::uint32_t, 3> x {};
    for (std::size_t axis = 0; axis < 3; ++axis) { x[axis] = static_cast<std::uint32_t>(std::clamp(point[axis], 0.0, 1.0) * top); }
    // Inverse undo.
    for (std::uint32_t q = 1u << (bits - 1); q > 1; q >>= 1) {
        const auto p = q - 1;
        for (std::size_t axis = 0; axis < 3; ++axis) {
            if ((x[axis] & q) != 0) {
                x[0] ^= p;
            } else {
                const auto t = (x[0] ^ x[axis]) & p;
                x[0] ^= t;
                x[axis] ^= t;
            }
        }
    }
    // Gray encode.
    for (std::size_t axis = 1; axis < 3; ++axis) { x[axis] ^= x[axis - 1]; }
    std::uint32_t t = 0;
    for (std::uint32_t q = 1u << (bits - 1); q > 1; q >>= 1) {
        if ((x[2] & q) != 0) { t ^= q - 1; }
    }
    for (auto& value : x) { value ^= t; }
    std::uint64_t index = 0;
    for (int bit = bits - 1; bit >= 0; --bit) {
        for (std::size_t axis = 0; axis < 3; ++axis) { index = (index << 1) | ((x[axis] >> bit) & 1u); }
    }
    return index;
}

// The loop through the faces of each part of a mesh: pieces, each sharing a
// side with the next and the last with the first.
inline std::vector<std::vector<Piece>> loops(const std::vector<Vec3>& corners, const std::vector<surface::Face>& faces) {
    const auto placed = surface::detail::byPlace(corners, faces);
    const auto usable = [&](std::uint32_t face) {
        const auto& p = placed[face];
        return p[0] != p[1] && p[1] != p[2] && p[2] != p[0];
    };
    // Faces either side of each side, by its corners' places.
    std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<std::pair<std::uint32_t, std::uint32_t>>> sides;
    for (std::uint32_t face = 0; face < faces.size(); ++face) {
        if (!usable(face)) { continue; }
        for (std::uint32_t side = 0; side < 3; ++side) {
            auto a = placed[face][side], b = placed[face][(side + 1) % 3];
            if (b < a) { std::swap(a, b); }
            sides[{a, b}].emplace_back(face, side);
        }
    }
    // A tree through the faces of each part: which of each face's sides it
    // crosses, to which face, and that face's side. Faces near each other
    // along a Hilbert curve through the space round the mesh are joined
    // first, so the tree, and the loop round it, keep close at every scale,
    // as the curve does: a stretch of the loop is a patch of the surface,
    // not a strip across it.
    struct Crossing {
        std::int32_t face = -1;
        std::uint32_t side = 0;
    };
    std::vector<std::array<Crossing, 3>> tree(faces.size());
    const auto bounds = surface::detail::boundsOf(corners, faces);
    std::vector<std::uint64_t> along(faces.size(), 0);
    for (std::uint32_t face = 0; face < faces.size(); ++face) {
        const auto middle = (corners[faces[face][0]] + corners[faces[face][1]] + corners[faces[face][2]]) / 3.0;
        along[face] = hilbertIndex({(middle.x - bounds.low.x) / bounds.size(), (middle.y - bounds.low.y) / bounds.size(), (middle.z - bounds.low.z) / bounds.size()});
    }
    struct Join {
        std::uint64_t apart;
        std::uint32_t face, side, other, otherSide;
    };
    std::vector<Join> joins;
    for (const auto& [key, sharing] : sides) {
        // Only where just two faces meet: a seam the loop can cross.
        if (sharing.size() != 2) { continue; }
        const auto [face, side] = sharing[0];
        const auto [other, otherSide] = sharing[1];
        joins.push_back({along[face] > along[other] ? along[face] - along[other] : along[other] - along[face], face, side, other, otherSide});
    }
    std::sort(joins.begin(), joins.end(), [](const Join& one, const Join& two) { return std::tie(one.apart, one.face, one.side) < std::tie(two.apart, two.face, two.side); });
    std::vector<std::uint32_t> group(faces.size());
    for (std::uint32_t face = 0; face < faces.size(); ++face) { group[face] = face; }
    const auto root = [&](std::uint32_t face) {
        while (group[face] != face) { face = group[face] = group[group[face]]; }
        return face;
    };
    for (const auto& join : joins) {
        const auto a = root(join.face), b = root(join.other);
        if (a == b) { continue; }
        group[a] = b;
        tree[join.face][join.side] = {static_cast<std::int32_t>(join.other), join.otherSide};
        tree[join.other][join.otherSide] = {static_cast<std::int32_t>(join.face), join.side};
    }
    std::vector<char> reached(faces.size(), 0);
    std::vector<std::vector<Piece>> found;
    // Each part's loop from its face first along the curve.
    std::vector<std::uint32_t> byAlong;
    for (std::uint32_t face = 0; face < faces.size(); ++face) {
        if (usable(face)) { byAlong.push_back(face); }
    }
    std::sort(byAlong.begin(), byAlong.end(), [&](std::uint32_t one, std::uint32_t two) { return along[one] < along[two]; });
    for (const auto start : byAlong) {
        if (reached[start] != 0) { continue; }
        // Its part, reached through the tree.
        std::vector<std::uint32_t> queue {start};
        reached[start] = 1;
        for (std::size_t next = 0; next < queue.size(); ++next) {
            for (const auto& crossing : tree[queue[next]]) {
                if (crossing.face < 0 || reached[static_cast<std::size_t>(crossing.face)] != 0) { continue; }
                reached[static_cast<std::size_t>(crossing.face)] = 1;
                queue.push_back(static_cast<std::uint32_t>(crossing.face));
            }
        }
        // Each face's pieces round its middle, in its own order: a side the
        // tree crosses in two halves, either side of its middle, so the walk
        // goes out by one half and back by the other.
        const auto ringOf = [&](std::uint32_t face) {
            std::vector<Piece> ring;
            const surface::Weights middle {1.0 / 3, 1.0 / 3, 1.0 / 3};
            for (std::uint32_t side = 0; side < 3; ++side) {
                surface::Weights from {}, to {};
                from[side] = 1;
                to[(side + 1) % 3] = 1;
                if (tree[face][side].face >= 0) {
                    const auto half = mix(from, to);
                    ring.push_back({face, {middle, from, half}, static_cast<std::int32_t>(side), false});
                    ring.push_back({face, {middle, half, to}, static_cast<std::int32_t>(side), true});
                } else {
                    ring.push_back({face, {middle, from, to}});
                }
            }
            return ring;
        };
        // Where in a face's ring a side's half by one of its corners is.
        const auto halfAt = [&](std::uint32_t face, std::uint32_t side, std::uint32_t corner) {
            std::uint32_t index = 0;
            for (std::uint32_t before = 0; before < side; ++before) { index += tree[face][before].face >= 0 ? 2 : 1; }
            return index + (placed[face][side] == corner ? 0u : 1u);
        };
        // Round the tree: each face from where it is entered, round its ring
        // away from the other half of the side it came in by; into each face
        // the tree reaches on the way, and back.
        struct Visit {
            std::uint32_t face;
            std::vector<Piece> ring;
            std::uint32_t at;
            int direction;
            std::uint32_t done = 0;
        };
        std::vector<Piece> loop;
        std::vector<Visit> stack;
        stack.push_back({start, ringOf(start), 0, 1});
        while (!stack.empty()) {
            auto& visit = stack.back();
            if (visit.done == visit.ring.size()) {
                stack.pop_back();
                continue;
            }
            const auto count = static_cast<std::int64_t>(visit.ring.size());
            const auto index = static_cast<std::uint32_t>(((static_cast<std::int64_t>(visit.at) + visit.direction * static_cast<std::int64_t>(visit.done)) % count + count) % count);
            const auto piece = visit.ring[index];
            loop.push_back(piece);
            ++visit.done;
            if (visit.done == visit.ring.size()) { continue; }
            const auto nextIndex = static_cast<std::uint32_t>(((static_cast<std::int64_t>(index) + visit.direction) % count + count) % count);
            // The first of a crossed side's halves met: across, and back by the other.
            if (piece.side < 0 || visit.ring[nextIndex].side != piece.side) { continue; }
            const auto side = static_cast<std::uint32_t>(piece.side);
            const auto across = tree[visit.face][side];
            // The half by the same corner as this one, across.
            const auto corner = placed[visit.face][piece.second ? (side + 1) % 3 : side];
            const auto other = static_cast<std::uint32_t>(across.face);
            const auto entry = halfAt(other, across.side, corner);
            auto ring = ringOf(other);
            const auto ringCount = static_cast<std::uint32_t>(ring.size());
            // Away from its other half.
            const auto otherHalf = placed[other][across.side] == corner ? entry + 1 : entry - 1;
            const auto direction = (otherHalf + ringCount) % ringCount == (entry + 1) % ringCount ? -1 : 1;
            stack.push_back({other, std::move(ring), entry, direction});
        }
        found.push_back(std::move(loop));
    }
    return found;
}
}

inline Curve Curve::of(const std::vector<Vec3>& corners, const std::vector<surface::Face>& faces, double finest) {
    Curve curve;
    const auto placeOf = [&](const detail::Piece& piece, std::size_t corner) { return surface::detail::placeOf(surface::at(corners, faces[piece.face], piece.corners[corner])); };
    const auto pointOf = [&](std::uint32_t face, const surface::Weights& weights) { return surface::at(corners, faces[face], weights); };
    for (const auto& loop : detail::loops(corners, faces)) {
        const auto count = loop.size();
        if (count == 0) { continue; }
        // Where the curve crosses from each piece to the next: one of the two
        // corners of the side they share, so each piece is gone through from
        // a corner on its way in to another on its way out, the side between
        // them its longest where it can be: chosen round the loop at once.
        std::vector<std::array<std::size_t, 2>> shared(count);
        bool joined = true;
        for (std::size_t index = 0; index < count && joined; ++index) {
            const auto& here = loop[index];
            const auto& next = loop[(index + 1) % count];
            std::size_t found = 0;
            for (std::size_t corner = 0; corner < 3; ++corner) {
                for (std::size_t other = 0; other < 3; ++other) {
                    if (found < 2 && placeOf(here, corner) == placeOf(next, other)) { shared[index][found++] = corner; }
                }
            }
            joined = found == 2;
        }
        // Each piece's corners as the curve takes them: in, through, out.
        std::vector<std::array<std::size_t, 3>> order(count);
        if (!joined || count == 1) {
            for (auto& piece : order) { piece = {1, 0, 2}; }
        } else {
            // Its cost, by the corner it is entered by (a corner of the side
            // shared with the last) and left by: how much shorter the side
            // between them is than its longest.
            const auto costOf = [&](std::size_t index, std::size_t in, std::size_t out) {
                if (in == out) { return 1.0e300; }
                const auto& piece = loop[index];
                std::array<Vec3, 3> at;
                for (std::size_t corner = 0; corner < 3; ++corner) { at[corner] = pointOf(piece.face, piece.corners[corner]); }
                const auto longest = std::max({(at[1] - at[0]).length(), (at[2] - at[1]).length(), (at[0] - at[2]).length()});
                return longest - (at[out] - at[in]).length();
            };
            // Which of the next piece's corners a crossing is, from this one's.
            const auto inNext = [&](std::size_t index, std::size_t corner) {
                const auto& next = loop[(index + 1) % count];
                for (std::size_t other = 0; other < 3; ++other) {
                    if (placeOf(next, other) == placeOf(loop[index], corner)) { return other; }
                }
                return std::size_t {0};
            };
            double best = 1.0e300;
            std::vector<std::size_t> bestChoice;
            // The loop's last crossing fixed, each way; the rest chosen in turn.
            for (std::size_t lastChoice = 0; lastChoice < 2; ++lastChoice) {
                std::vector<std::array<double, 2>> cost(count, {1.0e300, 1.0e300});
                std::vector<std::array<std::size_t, 2>> from(count, {0, 0});
                const auto entryOfFirst = inNext(count - 1, shared[count - 1][lastChoice]);
                for (std::size_t choice = 0; choice < 2; ++choice) { cost[0][choice] = costOf(0, entryOfFirst, shared[0][choice]); }
                for (std::size_t index = 1; index < count; ++index) {
                    for (std::size_t choice = 0; choice < 2; ++choice) {
                        if (index == count - 1 && choice != lastChoice) { continue; }
                        for (std::size_t before = 0; before < 2; ++before) {
                            const auto value = cost[index - 1][before] + costOf(index, inNext(index - 1, shared[index - 1][before]), shared[index][choice]);
                            if (value < cost[index][choice]) {
                                cost[index][choice] = value;
                                from[index][choice] = before;
                            }
                        }
                    }
                }
                if (cost[count - 1][lastChoice] < best) {
                    best = cost[count - 1][lastChoice];
                    bestChoice.assign(count, 0);
                    bestChoice[count - 1] = lastChoice;
                    for (auto index = count - 1; index > 0; --index) { bestChoice[index - 1] = from[index][bestChoice[index]]; }
                }
            }
            for (std::size_t index = 0; index < count; ++index) {
                const auto in = inNext((index + count - 1) % count, shared[(index + count - 1) % count][bestChoice[(index + count - 1) % count]]);
                const auto out = shared[index][bestChoice[index]];
                order[index] = {in, 3 - in - out, out};
            }
        }
        // Each piece halved and halved again across its longest side, so its
        // pieces stay well shaped however thin its face: the curve goes in by
        // one corner (a), round the third (b) and out by the other (c), and
        // its halves take it the same way, the first from a to the corner
        // they share, the second from there to c.
        std::vector<std::uint32_t> pieces;
        for (std::size_t index = 0; index < count; ++index) {
            const auto& piece = loop[index];
            const auto [in, through, out] = order[index];
            const auto build = [&](const auto& self, const surface::Weights& a, const surface::Weights& b, const surface::Weights& c, int depth) -> std::uint32_t {
                const auto id = static_cast<std::uint32_t>(curve.nodes.size());
                curve.nodes.push_back({});
                curve.nodes[id].face = piece.face;
                curve.nodes[id].corners = {a, b, c};
                curve.nodes[id].piece = true;
                curve.nodes[id].lo = static_cast<std::uint32_t>(curve.leaves.size());
                const auto pa = pointOf(piece.face, a), pb = pointOf(piece.face, b), pc = pointOf(piece.face, c);
                const auto widest = std::max({(pb - pa).length(), (pc - pb).length(), (pa - pc).length()});
                if (widest > finest && depth < 24) {
                    const auto across = (pc - pa).length(), in = (pb - pa).length(), out = (pc - pb).length();
                    std::uint32_t first = 0, second = 0;
                    if (across >= in && across >= out) {
                        // Across from a to c: through b, by its middle.
                        const auto middle = detail::mix(a, c);
                        first = self(self, a, middle, b, depth + 1);
                        second = self(self, b, middle, c, depth + 1);
                    } else if (out >= in) {
                        // The side from b to c: a to its middle round b, then on round a to c.
                        const auto middle = detail::mix(b, c);
                        first = self(self, a, b, middle, depth + 1);
                        second = self(self, middle, a, c, depth + 1);
                    } else {
                        // The side from a to b: a to its middle round c, then on round b to c.
                        const auto middle = detail::mix(a, b);
                        first = self(self, a, c, middle, depth + 1);
                        second = self(self, middle, b, c, depth + 1);
                    }
                    curve.nodes[id].first = static_cast<std::int32_t>(first);
                    curve.nodes[id].second = static_cast<std::int32_t>(second);
                } else {
                    curve.leaves.push_back(id);
                }
                curve.nodes[id].hi = static_cast<std::uint32_t>(curve.leaves.size());
                return id;
            };
            pieces.push_back(build(build, piece.corners[in], piece.corners[through], piece.corners[out], 0));
        }
        // The pieces along the loop in runs, halved where half their area
        // is, so runs of the same depth are alike in size: small faces'
        // pieces are taken together as one.
        std::vector<double> before {0};
        for (const auto id : pieces) {
            const auto& node = curve.nodes[id];
            const auto a = pointOf(node.face, node.corners[0]), b = pointOf(node.face, node.corners[1]), c = pointOf(node.face, node.corners[2]);
            before.push_back(before.back() + (b - a).cross(c - a).length() / 2);
        }
        const auto run = [&](const auto& self, std::size_t from, std::size_t to) -> std::uint32_t {
            if (to - from == 1) { return pieces[from]; }
            const auto half = (before[from] + before[to]) / 2;
            auto middle = static_cast<std::size_t>(std::lower_bound(before.begin() + static_cast<std::ptrdiff_t>(from) + 1, before.begin() + static_cast<std::ptrdiff_t>(to), half) - before.begin());
            if (middle > from + 1 && half - before[middle - 1] < before[middle] - half) { --middle; }
            middle = std::clamp(middle, from + 1, to - 1);
            const auto first = self(self, from, middle), second = self(self, middle, to);
            Node node;
            node.first = static_cast<std::int32_t>(first);
            node.second = static_cast<std::int32_t>(second);
            node.lo = curve.nodes[first].lo;
            node.hi = curve.nodes[second].hi;
            curve.nodes.push_back(node);
            return static_cast<std::uint32_t>(curve.nodes.size() - 1);
        };
        curve.roots.push_back(run(run, 0, pieces.size()));
    }
    return curve;
}

// A step of the curve: from a point of one face to a point of another (or
// the same), by their weights.
struct Step {
    std::uint32_t fromFace, toFace;
    surface::Weights from, to;
};

// The curve lit for a frame: each node's width and the most light in it,
// from its leaves'. Its width is as wide as its area would be square, or as
// far across as its leaves reach, if more: a long thin run (up one narrow
// face and down the next, by a cone's tip) is as wide as it is long.
// `leafLight(leaf)`: the light at a leaf's middle (negative where it faces
// away or none falls); `leafArea(leaf)`: its area; `leafAt(leaf)`: where its
// middle is.
struct Lit {
    const Curve* curve = nullptr;
    std::vector<float> light;
    std::vector<double> width;

    template <typename LeafLight, typename LeafArea, typename LeafAt>
    static Lit of(const Curve& curve, LeafLight&& leafLight, LeafArea&& leafArea, LeafAt&& leafAt) {
        Lit lit;
        lit.curve = &curve;
        lit.light.assign(curve.nodes.size(), -1.0f);
        std::vector<double> area(curve.nodes.size(), 0.0);
        constexpr auto far = 1.0e300;
        std::vector<std::array<Vec3, 2>> box(curve.nodes.size(), {Vec3 {far, far, far}, Vec3 {-far, -far, -far}});
        // Each node after the nodes in it: pieces' halves after the piece,
        // runs after what they run over. Leaves first, then upward.
        std::vector<std::uint32_t> order;
        order.reserve(curve.nodes.size());
        const auto collect = [&](const auto& self, std::uint32_t index) -> void {
            const auto& node = curve.nodes[index];
            if (node.first >= 0) {
                self(self, static_cast<std::uint32_t>(node.first));
                self(self, static_cast<std::uint32_t>(node.second));
            }
            order.push_back(index);
        };
        for (const auto root : curve.roots) { collect(collect, root); }
        for (const auto index : order) {
            const auto& node = curve.nodes[index];
            if (node.first < 0) {
                lit.light[index] = static_cast<float>(leafLight(static_cast<std::uint32_t>(index)));
                area[index] = leafArea(static_cast<std::uint32_t>(index));
                const Vec3 at = leafAt(static_cast<std::uint32_t>(index));
                box[index] = {at, at};
                continue;
            }
            const auto first = static_cast<std::size_t>(node.first), second = static_cast<std::size_t>(node.second);
            lit.light[index] = std::max(lit.light[first], lit.light[second]);
            area[index] = area[first] + area[second];
            const auto& one = box[first];
            const auto& two = box[second];
            box[index] = {Vec3 {std::min(one[0].x, two[0].x), std::min(one[0].y, two[0].y), std::min(one[0].z, two[0].z)}, Vec3 {std::max(one[1].x, two[1].x), std::max(one[1].y, two[1].y), std::max(one[1].z, two[1].z)}};
        }
        lit.width.resize(area.size());
        for (std::size_t index = 0; index < area.size(); ++index) { lit.width[index] = std::max(std::sqrt(2 * area[index]), (box[index][1] - box[index][0]).length()); }
        return lit;
    }
    float brightest() const { return light.empty() ? 0.0f : *std::max_element(light.begin(), light.end()); }

    // The curve as steps from the middle of each node it passes through to
    // the next's (see curveAt), along each loop, none to or from a node
    // lit less than `darkest`.
    std::vector<Step> steps(double finest, double exposure, double gain, double darkest) const {
        std::vector<Step> found;
        std::optional<std::pair<std::uint32_t, surface::Weights>> previous;
        curveAt(finest, exposure, gain, [&](std::optional<std::uint32_t> node, double share) {
            if (!node.has_value() || share < darkest) {
                previous.reset();
                return;
            }
            const auto middle = curve->middleOf(*node);
            if (previous.has_value()) { found.push_back({previous->first, middle.first, previous->second, middle.second}); }
            previous = middle;
        });
        return found;
    }

    // The curve as coarse as the light asks: `visit(node, light)` for each
    // node it passes through in order, a node divided while it is wider than
    // `finest` over its light (out of `exposure`) times `gain`, each by its
    // own threshold, fixed to it, so as light grows its pieces divide one by
    // one; `visit(none)` between loops.
    template <typename Visit>
    void curveAt(double finest, double exposure, double gain, Visit&& visit) const {
        for (const auto root : curve->roots) {
            const auto walk = [&](const auto& self, std::uint32_t index) -> void {
                const auto& node = curve->nodes[index];
                const auto most = light[index];
                const auto share = most < 0 ? -1.0 : std::clamp(most / exposure, 0.0, 1.0);
                auto hash = static_cast<std::uint64_t>(index) * 0x9e3779b97f4a7c15ull;
                hash ^= hash >> 29;
                hash *= 0xbf58476d1ce4e5b9ull;
                hash ^= hash >> 32;
                const auto threshold = 1 + static_cast<double>(hash >> 11) / static_cast<double>(1ull << 53);
                if (node.first >= 0 && share > 0 && width[index] * share * gain > finest * threshold) {
                    self(self, static_cast<std::uint32_t>(node.first));
                    self(self, static_cast<std::uint32_t>(node.second));
                    return;
                }
                visit(std::optional<std::uint32_t>(index), share);
            };
            walk(walk, root);
            visit(std::optional<std::uint32_t>(), -1.0);
        }
    }
};
}
