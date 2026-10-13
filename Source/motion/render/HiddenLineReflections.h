#pragma once

#include "HiddenLineBake.h"
#include "HiddenLineCrossings.h"
#include "HiddenLineScene.h"
#include "HiddenLineStyles.h"
#include "Reflections.h"
#include "SceneRays.h"
#include <optional>
#include <vector>

namespace motion::hidden {
// A line as a mirror shows it: the points in the scene where it is seen, each
// in the colour it is drawn there, walked at an even pace along them.
class ReflectedLine final : public osci::Shape {
public:
    explicit ReflectedLine(std::vector<osci::Point> stops) : points(std::move(stops)) {
        lengths.reserve(points.size());
        double total = 0;
        for (std::size_t index = 0; index < points.size(); ++index) {
            if (index > 0) {
                const auto& a = points[index - 1];
                const auto& b = points[index];
                total += std::hypot(b.x - a.x, b.y - a.y, b.z - a.z);
            }
            lengths.push_back(total);
        }
        len = static_cast<float>(total);
    }
    osci::Point nextVector(float progress) override {
        if (points.size() < 2 || !(len > 0)) { return points.empty() ? osci::Point() : points.front(); }
        const auto along = std::clamp(static_cast<double>(progress), 0.0, 1.0) * lengths.back();
        const auto next = std::upper_bound(lengths.begin() + 1, lengths.end() - 1, along);
        const auto index = static_cast<std::size_t>(next - lengths.begin());
        const auto span = lengths[index] - lengths[index - 1];
        const auto share = static_cast<float>(span > 0 ? (along - lengths[index - 1]) / span : 0.0);
        const auto& a = points[index - 1];
        const auto& b = points[index];
        const auto mix = [share](float x, float y) { return x + (y - x) * share; };
        return {mix(a.x, b.x), mix(a.y, b.y), mix(a.z, b.z), mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b)};
    }
    void scale(float, float, float) override {}
    void translate(float, float, float) override {}
    float length() override { return len; }
    std::unique_ptr<osci::Shape> clone() override { return std::make_unique<ReflectedLine>(*this); }
    std::string type() override { return "Reflection"; }

private:
    std::vector<osci::Point> points;
    std::vector<double> lengths;
};

// A curve is cut where a straight line from the last cut would stray from
// it by more than this, on screen (a third of a pixel), so the beam is at
// each cut at a sample of its own: drawn from sample to sample in straight
// steps, it still draws a curve as a curve, with as few as do that.
inline constexpr double reflectedStray = DepthBuffer::pixel / 3;

// A mirror's drawing as it is traced: runs that meet go on as one line, and
// a line is cut where it bends (see reflectedStray), or where its share of
// the beam changes. A whole line too short to read is left out: it
// would only cost the beam a stroke's samples.
class ReflectedLines {
public:
    // A point of a run: where it is in the scene and on screen, its colour,
    // and whether it starts the run.
    void add(osci::Point point, Screen at, double weight, bool starts) {
        if (starts) {
            const auto goesOn = !stops.empty() && std::abs(point.x - stops.back().x) < 1.0e-6f && std::abs(point.y - stops.back().y) < 1.0e-6f && std::abs(point.z - stops.back().z) < 1.0e-6f;
            if (goesOn && weight == pieceWeight) { return; }
            if (goesOn) {
                cut();
            } else {
                finishLine();
            }
            pieceWeight = weight;
        } else if (last.has_value()) {
            lineLength += std::hypot(at.x - last->x, at.y - last->y);
            if (strays(at) && stops.size() > 1) { cut(); }
        }
        stops.push_back(point);
        onScreen.push_back(at);
        last = at;
    }
    // The drawing, once every run is in: every join a corner.
    std::shared_ptr<const PreparedDrawing> drawing() {
        finishLine();
        if (shapes.empty()) { return nullptr; }
        return std::make_shared<const PreparedDrawing>(std::move(shapes), true, weights, true);
    }

private:
    // Whether the straight line from where the piece starts to `at` strays
    // from the points between by more than reflectedStray.
    bool strays(Screen at) const {
        const auto& from = onScreen.front();
        const auto dx = at.x - from.x, dy = at.y - from.y;
        const auto length = std::hypot(dx, dy);
        for (std::size_t index = 1; index < onScreen.size(); ++index) {
            const auto ox = onScreen[index].x - from.x, oy = onScreen[index].y - from.y;
            const auto off = length > 0 ? std::abs(ox * dy - oy * dx) / length : std::hypot(ox, oy);
            if (off > reflectedStray) { return true; }
        }
        return false;
    }
    // Ends the piece at the last point, the next going on from it.
    void cut() {
        if (stops.size() > 1) {
            const auto join = stops.back();
            line.push_back(std::make_unique<ReflectedLine>(std::move(stops)));
            lineWeights.push_back(pieceWeight);
            stops = {join};
            onScreen = {onScreen.back()};
        }
    }
    void finishLine() {
        if (stops.size() > 1) {
            line.push_back(std::make_unique<ReflectedLine>(std::move(stops)));
            lineWeights.push_back(pieceWeight);
        }
        if (lineLength >= shortestOnScreen) {
            for (auto& piece : line) { shapes.push_back(std::move(piece)); }
            weights.insert(weights.end(), lineWeights.begin(), lineWeights.end());
        }
        line.clear();
        lineWeights.clear();
        stops.clear();
        onScreen.clear();
        last.reset();
        lineLength = 0;
    }
    std::vector<std::unique_ptr<osci::Shape>> shapes, line;
    std::vector<double> weights, lineWeights;
    std::vector<osci::Point> stops;
    std::vector<Screen> onScreen;
    std::optional<Screen> last;
    double lineLength = 0, pieceWeight = 1;
};

// What each surface that reflects shows of the scene, for a frame: every line
// of every other clip, where the mirror shows it to the eye, and the
// mirror's own edges (a ball's rim, round, as the eye sees it). Where
// something hides the mirror from the eye there is nothing, as where the
// camera hides a line; where something stands between the mirror and a line,
// the line is hidden in it, as it would be seen from there. Each part is
// drawn in the colour the line is drawn in where it is (lit as it is lit),
// times how brightly the mirror reflects and how bright it is (its opacity,
// or a dim colour), whatever colour it is. A surface's lines are never seen
// in itself.
//
// `place(item, point)` puts a point of a clip in the scene; `normalsOf(item,
// shape)` gives the faces a line runs along, to light it by; `depth` is the
// camera's, when it hides lines. Only what the Scope shows (`reach`) is
// kept: the beam's time is not spent off it. None when cancelled.
template <typename Place, typename NormalsOf, typename Cancelled>
std::optional<std::vector<Frame::Entry>> reflect(const PreparedComposition& composition, double time, const std::vector<SceneItem>& items,
    const std::vector<std::vector<std::optional<Vec3>>>& world, Place&& place, NormalsOf&& normalsOf, const std::vector<LightView>& lights, bool lit,
    const PreparedComposition::Viewpoint& view, Reach reach, const DepthBuffer* depth, const SceneRays& rays, LaidSurfaces& laid, Cancelled&& cancelled) {
    std::vector<Frame::Entry> entries;
    const Eye eye {view.position, view.right, view.up, view.forward};
    const Projection projection {view.focalLength};
    const auto screen = [&](const std::optional<Vec3>& at) { return at.has_value() ? projection(eye.see(*at)) : std::nullopt; };
    const auto distance = [](const std::optional<Screen>& a, const std::optional<Screen>& b) { return a.has_value() && b.has_value() ? std::hypot(b->x - a->x, b->y - a->y) : 0.0; };

    struct Reflector {
        std::uint32_t surface;
        Mirror mirror;
        double amount;
    };
    std::vector<Reflector> reflectors;
    for (std::uint32_t surface = 0; surface < items.size(); ++surface) {
        const auto& item = items[surface];
        if (item.drawing == nullptr || item.drawing->faces().empty()) { continue; }
        const auto amount = item.mirrors;
        if (!(amount > 0)) { continue; }
        auto mirror = Mirror::of(world[surface], item.drawing->faces().triangles);
        mirror.face(eye.position);
        if (mirror.sphere.has_value() || !mirror.flats.empty()) { reflectors.push_back({surface, std::move(mirror), amount}); }
    }
    if (reflectors.empty()) { return entries; }
    // Where surfaces cut through each other, anywhere in the scene, with the
    // surface and face each piece lies on: lines drawn only where the
    // camera hides lines.
    std::vector<crossing::Triangle> crossingTriangles;
    std::vector<std::pair<std::uint32_t, std::uint32_t>> crossingFaces;
    for (std::uint32_t surface = 0; depth != nullptr && surface < items.size(); ++surface) {
        const auto& item = items[surface];
        if (item.drawing == nullptr) { continue; }
        const auto& triangles = item.drawing->faces().triangles;
        for (std::uint32_t face = 0; face < triangles.size(); ++face) {
            const auto& a = world[surface][triangles[face][0]];
            const auto& b = world[surface][triangles[face][1]];
            const auto& c = world[surface][triangles[face][2]];
            if (!a.has_value() || !b.has_value() || !c.has_value()) { continue; }
            crossingTriangles.push_back({{*a, *b, *c}, surface, static_cast<float>(item.mirrors)});
            crossingFaces.emplace_back(surface, face);
        }
    }
    const auto crossings = crossing::runsInScene(crossingTriangles, nullptr);

    // Scratch, per stretch of line.
    std::vector<double> shares;
    std::vector<std::optional<Vec3>> seenAt;
    std::vector<std::optional<Screen>> onScreen;
    std::vector<char> showing, clear, rimward;
    std::vector<std::array<float, 3>> colours;
    std::vector<double> shades;
    std::vector<std::size_t> kept;
    std::vector<std::pair<std::size_t, std::size_t>> pending;
    for (const auto& reflector : reflectors) {
        if (cancelled()) { return std::nullopt; }
        const auto& mirror = reflector.mirror;
        // What it shows keeps its own colours, as bright as the mirror
        // reflects and is (its opacity, or a dim colour, dims it); its edges
        // are in its own colour.
        const auto own = composition.applyCompositionEffects(items[reflector.surface].clip->processPoint(osci::Point(0, 0, 0, -1, -1, -1), time), time);
        const std::array<float, 3> tint {std::max(0.0f, own.r), std::max(0.0f, own.g), std::max(0.0f, own.b)};
        const auto brightness = std::max({tint[0], tint[1], tint[2]});
        const auto amount = static_cast<float>(reflector.amount) * brightness;
        const std::array<float, 3> gain {amount, amount, amount};
        ReflectedLines lines;

        // A straight stretch of line from `from` to `to` in the scene, where
        // the mirror shows it between those shares of it: `reflection(point,
        // near)` is where a point of it is seen (`near`, where a point next
        // to it is), and `colourAt(share, shade, bright)` the colour it is
        // drawn there, telling what shadows take and how bright it is drawn.
        // A ball is round where its mesh is not: what hides it from the eye
        // is found along the line to the eye, not in the camera's depth
        // buffer, where its own faces seen edge on would cut its rim.
        const auto byRay = depth != nullptr && mirror.sphere.has_value();
        const auto trace = [&](Vec3 from, Vec3 to, double low, double high, auto&& reflection, std::uint32_t ignored, auto&& colourAt, GrazedSurface grazed = {}) {
            const auto pointAt = [&](double share) { return from + (to - from) * share; };
            // As finely as a pixel of its reflection, from a few points of it.
            double length = 0;
            std::optional<Vec3> near;
            std::optional<Screen> previous;
            for (int probe = 0; probe <= 3; ++probe) {
                near = reflection(pointAt(low + (high - low) * probe / 3.0), near);
                const auto next = screen(near);
                length += distance(previous, next);
                previous = next;
            }
            const auto steps = stepCount(length * 1.25 / testStep, 1, 4096);
            const auto count = static_cast<std::size_t>(steps) + 1;
            shares.resize(count);
            seenAt.resize(count);
            onScreen.resize(count);
            showing.resize(count);
            clear.resize(count);
            rimward.resize(count);
            for (std::size_t step = 0; step < count; ++step) {
                shares[step] = low + (high - low) * static_cast<double>(step) / steps;
                seenAt[step] = reflection(pointAt(shares[step]), step > 0 ? seenAt[step - 1] : std::nullopt);
                onScreen[step] = screen(seenAt[step]);
                // The eye sees the mirror there, on what the Scope shows.
                const auto& at = onScreen[step];
                const auto shown = at.has_value() && std::abs(at->x) <= reach.x * DepthBuffer::overscan && std::abs(at->y) <= reach.y * DepthBuffer::overscan;
                if (!shown || depth == nullptr) {
                    showing[step] = shown ? 1 : 0;
                    continue;
                }
                // Facing the eye squarely, a ball's faces are where it is;
                // only near its rim is the line to the eye followed instead.
                rimward[step] = 0;
                if (byRay) {
                    const auto toEye = eye.position - *seenAt[step];
                    const auto facing = (*seenAt[step] - mirror.sphere->centre).dot(toEye) / (mirror.sphere->radius * toEye.length());
                    rimward[step] = facing < .3 ? 1 : 0;
                }
                showing[step] = rimward[step] != 0 || depth->shows(*at) ? 1 : 0;
            }
            if (byRay) {
                findChanges(steps, [&](int step) {
                    const auto index = static_cast<std::size_t>(step);
                    if (rimward[index] == 0 || showing[index] == 0) { return true; }
                    const auto apart = (eye.position - *seenAt[index]).length();
                    const auto seen = !rays.blocked(*seenAt[index], eye.position, 1.0e-6 * (1 + apart), 0, reflector.surface);
                    showing[index] = seen ? 1 : 0;
                    return seen;
                }, [&](int step, bool seen) {
                    if (!seen) { showing[static_cast<std::size_t>(step)] = 0; }
                });
            }
            // And the mirror sees the line: nothing between them. A point on
            // a surface is not hidden by it, to within a little of how far it
            // is, as the camera's depth test allows.
            // Only where the eye sees the mirror.
            std::fill(clear.begin(), clear.end(), 0);
            for (std::size_t start = 0; start < count;) {
                if (showing[start] == 0) {
                    ++start;
                    continue;
                }
                auto end = start;
                while (end + 1 < count && showing[end + 1] != 0) { ++end; }
                findChanges(static_cast<int>(end - start), [&](int step) {
                    const auto index = start + static_cast<std::size_t>(step);
                    const auto point = pointAt(shares[index]);
                    const auto apart = (point - *seenAt[index]).length();
                    clear[index] = rays.blocked(*seenAt[index], point, 1.0e-6 * (1 + apart), 1.0e-6 + DepthBuffer::depthPush * apart, ignored, grazed) ? 0 : 1;
                    return clear[index] != 0;
                }, [&](int step, bool answer) { clear[start + static_cast<std::size_t>(step)] = answer ? 1 : 0; });
                start = end + 1;
            }
            // Runs of it that show, from halfway to what doesn't either side.
            std::size_t index = 0;
            while (index < count) {
                if (showing[index] == 0 || clear[index] == 0) {
                    ++index;
                    continue;
                }
                auto last = index;
                while (last + 1 < count && showing[last + 1] != 0 && clear[last + 1] != 0) { ++last; }
                const auto first = index;
                index = last + 1;
                // Its colour where it is, found finely only near a shadow's
                // edge.
                const auto runSteps = static_cast<int>(last - first);
                colours.resize(static_cast<std::size_t>(runSteps) + 1);
                shades.resize(static_cast<std::size_t>(runSteps) + 1);
                float brightest = 0;
                findSparsely(runSteps, [&](int step) {
                    double shade = 0;
                    float light = 1;
                    const auto colour = colourAt(shares[first + static_cast<std::size_t>(step)], shade, light);
                    colours[static_cast<std::size_t>(step)] = {colour[0] * gain[0], colour[1] * gain[1], colour[2] * gain[2]};
                    shades[static_cast<std::size_t>(step)] = shade;
                    brightest = std::max(brightest, light);
                }, [&](int from, int to) {
                    const auto& a = colours[static_cast<std::size_t>(from)];
                    const auto& b = colours[static_cast<std::size_t>(to)];
                    constexpr float alike = 1.0f / 128;
                    return std::abs(shades[static_cast<std::size_t>(from)] - shades[static_cast<std::size_t>(to)]) > 1.0e-6
                        || std::abs(a[0] - b[0]) > alike || std::abs(a[1] - b[1]) > alike || std::abs(a[2] - b[2]) > alike;
                }, [&](int step, int from, int to, float share) {
                    for (std::size_t channel = 0; channel < 3; ++channel) {
                        colours[static_cast<std::size_t>(step)][channel] = colours[static_cast<std::size_t>(from)][channel] + (colours[static_cast<std::size_t>(to)][channel] - colours[static_cast<std::size_t>(from)][channel]) * share;
                    }
                });
                // Its points: where each step is seen, its ends halfway to
                // the steps either side that don't show (where they are seen
                // at all), as few as keep within a quarter of a pixel and a
                // little of its colour all along.
                kept.assign({0, static_cast<std::size_t>(runSteps)});
                pending.assign({{0, static_cast<std::size_t>(runSteps)}});
                while (!pending.empty()) {
                    const auto [a, b] = pending.back();
                    pending.pop_back();
                    double worst = 0;
                    std::size_t at = a;
                    const auto& sa = *onScreen[first + a];
                    const auto& sb = *onScreen[first + b];
                    for (auto middle = a + 1; middle < b; ++middle) {
                        const auto share = static_cast<float>(middle - a) / static_cast<float>(b - a);
                        const auto& sm = *onScreen[first + middle];
                        auto stray = std::hypot(sm.x - (sa.x + (sb.x - sa.x) * share), sm.y - (sa.y + (sb.y - sa.y) * share)) / (.25 * DepthBuffer::pixel);
                        for (std::size_t channel = 0; channel < 3; ++channel) {
                            const auto straight = colours[a][channel] + (colours[b][channel] - colours[a][channel]) * share;
                            stray = std::max(stray, static_cast<double>(std::abs(colours[middle][channel] - straight)) * 128);
                        }
                        if (stray > worst) {
                            worst = stray;
                            at = middle;
                        }
                    }
                    if (worst > 1) {
                        kept.push_back(at);
                        pending.push_back({a, at});
                        pending.push_back({at, b});
                    }
                }
                std::sort(kept.begin(), kept.end());
                kept.erase(std::unique(kept.begin(), kept.end()), kept.end());
                const auto endAt = [&](std::size_t inside, std::size_t outside) {
                    const auto halfway = outside < count ? reflection(pointAt((shares[inside] + shares[outside]) / 2), seenAt[inside]) : std::nullopt;
                    return halfway.has_value() ? *halfway : *seenAt[inside];
                };
                const auto weight = litWeight(reflector.amount * brightest, composition.scope.xyOnly);
                for (std::size_t stop = 0; stop < kept.size(); ++stop) {
                    const auto step = kept[stop];
                    const auto at = first + step;
                    const auto where = step == 0 && first > 0 ? endAt(at, at - 1) : step == static_cast<std::size_t>(runSteps) ? endAt(at, at + 1) : *seenAt[at];
                    const auto& colour = colours[step];
                    lines.add(osci::Point(static_cast<float>(where.x), static_cast<float>(where.y), static_cast<float>(where.z), colour[0], colour[1], colour[2]),
                        screen(where).value_or(*onScreen[at]), weight, stop == 0);
                }
            }
        };
        // What it shows is seen where `reflection` says; its edges where they are.
        const auto atItself = [](Vec3 point, std::optional<Vec3>) { return std::optional<Vec3>(point); };
        const auto ownColour = [&](double, double&, float&) {
            return brightness > 0 ? std::array<float, 3> {tint[0] / brightness, tint[1] / brightness, tint[2] / brightness} : std::array<float, 3> {};
        };

        // Its edges: a ball's rim, round, or each flat piece's outline.
        if (mirror.sphere.has_value()) {
            const auto rim = mirror.sphere->rimFrom(eye.position);
            if (rim.has_value()) {
                // Round from its lowest point, where a ball rests on what is
                // under it, so where the loop meets itself is least seen.
                const auto lowest = std::atan2(-rim->second.y, -rim->first.y);
                constexpr int around = 256;
                const auto turn = 2 * 3.14159265358979323846 / around;
                for (int stretch = 0; stretch < around; ++stretch) { trace(rim->at(lowest + stretch * turn), rim->at(lowest + (stretch + 1) * turn), 0.0, 1.0, atItself, reflector.surface, ownColour); }
            }
        } else {
            for (const auto& flat : mirror.flats) {
                for (const auto& [a, b] : flat.outline) { trace(a, b, 0.0, 1.0, atItself, SceneRays::none, ownColour); }
            }
        }

        // A straight stretch of a clip's line from `from` to `to` in the
        // scene (its shape's progress from `start` to `end`), as the mirror
        // shows it: lit as it is lit, then coloured by its clip. Only in
        // `only`, of a flat mirror's pieces, when given.
        const auto reflectStretch = [&](const SceneItem& item, osci::Shape& shape, const LineNormals& normals, Vec3 from, Vec3 to, double start, double end, float itself, const FlatMirror* only = nullptr, GrazedSurface grazed = {}) {
            const auto response = static_cast<float>(item.clip->lit(time));
            // As bright as it is drawn: by its style, as little as its own
            // surface mirrors, and its light.
            const auto* dimmed = dynamic_cast<const DimmedShape*>(&shape);
            const auto drawn = (dimmed != nullptr ? dimmed->gain() : 1.0f) * itself;
            const auto lineColour = [&](double share, double& shade, float& reaching) {
                std::array<float, 3> light {1, 1, 1};
                if (lit) { light = lightAt(lights, from + (to - from) * share, normals.at(start + (end - start) * share), response, &shade); }
                reaching = drawn * (lit ? std::max({light[0], light[1], light[2]}) : 1.0f);
                const auto drawnAs = composition.applyCompositionEffects(item.clip->processPoint(litColour(shape.nextVector(static_cast<float>(start + (end - start) * share)), light), time), time);
                return std::array<float, 3> {std::max(0.0f, drawnAs.r) * itself, std::max(0.0f, drawnAs.g) * itself, std::max(0.0f, drawnAs.b) * itself};
            };
            if (mirror.sphere.has_value()) {
                const auto& sphere = *mirror.sphere;
                trace(from, to, 0.0, 1.0, [&](Vec3 point, std::optional<Vec3> near) { return sphere.reflectionOf(eye.position, point, near); }, reflector.surface, lineColour, grazed);
                return;
            }
            for (const auto& flat : mirror.flats) {
                if (only != nullptr && &flat != only) { continue; }
                for (const auto& [low, high] : flat.spans(eye.position, from, to)) {
                    trace(from, to, low, high, [&](Vec3 point, std::optional<Vec3>) { return flat.reflectionOf(eye.position, point); }, SceneRays::none, lineColour, grazed);
                }
            }
        };
        // A surface's outline as the mirror's view of it has it: from the
        // eye's image through each flat piece, or from where a ball shows
        // the surface's middle.
        const auto reflectOutline = [&](std::uint32_t source, double amount, float itself) {
            const auto& item = items[source];
            const auto& styled = *item.styled;
            const auto& surface = laid.of(*styled.unstyled, item.source);
            Vec3 middle {};
            int placed = 0;
            for (const auto& corner : world[source]) {
                if (corner.has_value()) {
                    middle = middle + *corner;
                    ++placed;
                }
            }
            if (placed == 0) { return; }
            middle = middle / placed;
            const auto draw = [&](Vec3 seenFrom, const FlatMirror* only) {
                smoothLines(surface, surface::outline(styled.corners, surface.faces, styled.normals, seenFrom), outlineTurn, [&](osci::Point from, osci::Point to, std::uint32_t face, const surface::Weights& starts, const surface::Weights& ends) {
                    const auto a = place(item, from), b = place(item, to);
                    if (!a.has_value() || !b.has_value()) { return; }
                    DimmedShape line(std::make_unique<osci::Line>(from, to), static_cast<float>(amount));
                    const auto normals = LineNormals::turning(styled.normals.at(face, starts), styled.normals.at(face, ends));
                    reflectStretch(item, line, normals, *a, *b, 0.0, 1.0, itself, only, {source, styled.grazing(face)});
                });
            };
            if (mirror.sphere.has_value()) {
                const auto shownAt = mirror.sphere->reflectionOf(eye.position, middle);
                if (shownAt.has_value()) { draw(*shownAt, nullptr); }
                return;
            }
            for (const auto& flat : mirror.flats) { draw(flat.image(eye.position), &flat); }
        };

        for (std::uint32_t source = 0; source < items.size(); ++source) {
            if (source == reflector.surface) { continue; }
            const auto& item = items[source];
            if (item.drawing == nullptr) { continue; }
            // Another mirror shows here as it shows to the eye: its own lines
            // as dim as it draws them, and a ball by its rim, as the
            // mirror's view of it has it (from the eye's image through it).
            const auto itself = static_cast<float>(1 - item.mirrors);
            const auto other = std::find_if(reflectors.begin(), reflectors.end(), [source](const Reflector& candidate) { return candidate.surface == source; });
            if (other != reflectors.end() && other->mirror.sphere.has_value()) {
                const auto otherTint = composition.applyCompositionEffects(item.clip->processPoint(osci::Point(0, 0, 0, -1, -1, -1), time), time);
                const auto otherAmount = static_cast<float>(other->amount);
                const auto rimColour = [&](double, double&, float&) {
                    return std::array<float, 3> {std::max(0.0f, otherTint.r) * otherAmount, std::max(0.0f, otherTint.g) * otherAmount, std::max(0.0f, otherTint.b) * otherAmount};
                };
                for (const auto& flat : mirror.flats) {
                    const auto rim = other->mirror.sphere->rimFrom(flat.image(eye.position));
                    if (!rim.has_value()) { continue; }
                    constexpr int around = 256;
                    const auto turn = 2 * 3.14159265358979323846 / around;
                    for (int stretch = 0; stretch < around; ++stretch) {
                        const auto from = rim->at(stretch * turn), to = rim->at((stretch + 1) * turn);
                        for (const auto& [low, high] : flat.spans(eye.position, from, to)) {
                            trace(from, to, low, high, [&](Vec3 point, std::optional<Vec3>) { return flat.reflectionOf(eye.position, point); }, source, rimColour);
                        }
                    }
                }
            }
            if (!(itself > 0)) { continue; }
            const auto eyeOnlyFrom = item.styled != nullptr ? item.styled->eyeOnlyFrom : item.drawing->shapeCount();
            if (eyeOnlyFrom < item.drawing->shapeCount()) { reflectOutline(source, item.clip->lines(time).outline, itself); }
            for (std::size_t index = 0; index < eyeOnlyFrom; ++index) {
                if (cancelled()) { return std::nullopt; }
                auto* shape = item.drawing->shape(index);
                if (shape == nullptr || !(shape->length() > 0)) { continue; }
                const auto normals = lit ? normalsOf(source, index) : LineNormals {};
                // In the scene as straight stretches: one for a straight line
                // nothing bends, else as many as keep close to its path.
                std::vector<std::pair<double, std::optional<Vec3>>> path;
                if (!item.bent && dynamic_cast<osci::Line*>(shape) != nullptr) {
                    path = {{0.0, place(item, shape->nextVector(0))}, {1.0, place(item, shape->nextVector(1))}};
                } else {
                    double length = 0;
                    std::optional<Screen> previous;
                    for (int probe = 0; probe <= 8; ++probe) {
                        const auto next = screen(place(item, shape->nextVector(probe / 8.0f)));
                        length += distance(previous, next);
                        previous = next;
                    }
                    const auto stretches = stepCount(length / (6 * DepthBuffer::pixel), 4, 256);
                    for (int stretch = 0; stretch <= stretches; ++stretch) {
                        const auto progress = static_cast<double>(stretch) / stretches;
                        path.emplace_back(progress, place(item, shape->nextVector(static_cast<float>(progress))));
                    }
                }
                for (std::size_t stretch = 0; stretch + 1 < path.size(); ++stretch) {
                    const auto& [start, from] = path[stretch];
                    const auto& [end, to] = path[stretch + 1];
                    if (from.has_value() && to.has_value()) { reflectStretch(item, *shape, normals, *from, *to, start, end, itself); }
                }
            }
        }
        // Where surfaces cut through each other: a line on the earlier one,
        // as it is drawn, wherever it is in the scene.
        for (const auto& run : crossings) {
            for (const auto& piece : run) {
                const auto& [surface, face] = crossingFaces[piece.triangle];
                if (surface == reflector.surface) { continue; }
                const auto& item = items[surface];
                const auto itself = static_cast<float>(1 - item.mirrors);
                if (!(itself > 0)) { continue; }
                const auto& corners = item.drawing->faces().triangles[face];
                const auto& vertices = item.drawing->faces().vertices;
                const auto source = [&](const crossing::Weights& weights) {
                    const auto mixed = [&](float osci::Point::* axis) { return static_cast<float>(vertices[corners[0]].*axis * weights[0] + vertices[corners[1]].*axis * weights[1] + vertices[corners[2]].*axis * weights[2]); };
                    return osci::Point(mixed(&osci::Point::x), mixed(&osci::Point::y), mixed(&osci::Point::z));
                };
                osci::Line line(source(piece.from), source(piece.to));
                LineNormals normals;
                if (lit) { normals.add(normalOf(crossingTriangles[piece.triangle].eye[0], crossingTriangles[piece.triangle].eye[1], crossingTriangles[piece.triangle].eye[2])); }
                reflectStretch(item, line, normals, piece.eyeFrom, piece.eyeTo, 0.0, 1.0, itself);
            }
        }
        auto drawing = lines.drawing();
        if (drawing != nullptr) { entries.push_back({items[reflector.surface].index, std::move(drawing), 1}); }
    }
    return entries;
}
}
