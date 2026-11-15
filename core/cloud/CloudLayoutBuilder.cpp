#include "CloudLayoutBuilder.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>

namespace sonicator::cloud
{
namespace
{
constexpr double goldenAngle = 2.39996322972865332; // pi * (3 - sqrt 5)

struct Vec2
{
    double x = 0, y = 0;
};

double length(double x, double y) { return std::sqrt(x * x + y * y); }

void orthonormalise(std::vector<double>& v, const std::vector<std::vector<double>>& basis)
{
    for (const auto& b : basis)
    {
        double dot = 0;
        for (size_t i = 0; i < v.size(); ++i) dot += v[i] * b[i];
        for (size_t i = 0; i < v.size(); ++i) v[i] -= dot * b[i];
    }
}

double normalise(std::vector<double>& v)
{
    double n = 0;
    for (auto x : v) n += x * x;
    n = std::sqrt(n);
    if (n > 1e-12)
        for (auto& x : v) x /= n;
    return n;
}

// Deterministic power iteration with deflation. No random start: the same rows always give the
// same components, which is what makes the whole layout reproducible.
std::vector<std::vector<double>> principalComponents(const std::vector<std::vector<float>>& rows, int wanted)
{
    if (rows.size() < 3) return {};
    const int dim = (int) rows[0].size();
    if (dim == 0 || wanted <= 0) return {};
    std::vector<double> mean((size_t) dim, 0.0);
    for (const auto& r : rows)
        for (int d = 0; d < dim; ++d) mean[(size_t) d] += r[(size_t) d];
    for (auto& m : mean) m /= (double) rows.size();

    std::vector<std::vector<double>> cov((size_t) dim, std::vector<double>((size_t) dim, 0.0));
    std::vector<double> centred((size_t) dim);
    for (const auto& r : rows)
    {
        for (int d = 0; d < dim; ++d) centred[(size_t) d] = r[(size_t) d] - mean[(size_t) d];
        for (int i = 0; i < dim; ++i)
            for (int j = i; j < dim; ++j) cov[(size_t) i][(size_t) j] += centred[(size_t) i] * centred[(size_t) j];
    }
    for (int i = 0; i < dim; ++i)
        for (int j = 0; j < i; ++j) cov[(size_t) i][(size_t) j] = cov[(size_t) j][(size_t) i];

    std::vector<std::vector<double>> components;
    const int count = std::min(wanted, dim);
    for (int c = 0; c < count; ++c)
    {
        std::vector<double> v((size_t) dim);
        for (int i = 0; i < dim; ++i) v[(size_t) i] = 1.0 + 0.001 * (double) (i + 1);
        orthonormalise(v, components);
        if (normalise(v) < 1e-12) break;
        for (int iter = 0; iter < 120; ++iter)
        {
            std::vector<double> w((size_t) dim, 0.0);
            for (int i = 0; i < dim; ++i)
            {
                double sum = 0;
                for (int j = 0; j < dim; ++j) sum += cov[(size_t) i][(size_t) j] * v[(size_t) j];
                w[(size_t) i] = sum;
            }
            orthonormalise(w, components);
            if (normalise(w) < 1e-12) break;
            v = std::move(w);
        }
        orthonormalise(v, components);
        if (normalise(v) < 1e-12) break;
        components.push_back(v);
    }
    return components;
}

double median(std::vector<double> values)
{
    if (values.empty()) return 0;
    const auto middle = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + (long) middle, values.end());
    return values[middle];
}

double quantile(std::vector<double> values, double q)
{
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    const auto pos = std::clamp(q, 0.0, 1.0) * (double) (values.size() - 1);
    const auto lower = (size_t) std::floor(pos);
    const auto upper = std::min(values.size() - 1, lower + 1);
    const auto t = pos - (double) lower;
    return values[lower] * (1.0 - t) + values[upper] * t;
}

struct Draft
{
    int64_t tagId = 0;
    juce::String name;
    uint32_t colour = 0xff8a8f9c;
    int count = 0;
    double x = 0, y = 0, radius = 0;
};

void layoutIslands(std::vector<Draft>& islands, const std::vector<std::vector<double>>& weights,
                   const CloudLayoutOptions& opt)
{
    // Start on a golden-angle spiral in ascending tag id: deterministic, and no tag starts closer
    // to the centre than another because of a name it happens to have.
    for (size_t i = 0; i < islands.size(); ++i)
    {
        const auto angle = (double) i * goldenAngle;
        const auto distance = 70.0 * std::sqrt((double) (i + 1));
        islands[i].x = distance * std::cos(angle);
        islands[i].y = distance * std::sin(angle);
    }

    for (int iter = 0; iter < opt.islandIterations; ++iter)
    {
        const double step = 0.85 * std::pow(0.99, (double) iter) + 0.03;
        std::vector<Vec2> move(islands.size());
        for (size_t i = 0; i < islands.size(); ++i)
            for (size_t j = i + 1; j < islands.size(); ++j)
            {
                auto dx = islands[j].x - islands[i].x;
                auto dy = islands[j].y - islands[i].y;
                auto d = std::max(length(dx, dy), 1e-6);
                const auto w = weights[i][j];
                const auto touching = islands[i].radius + islands[j].radius + opt.islandGap;
                const auto desired = touching * (w > 0 ? 1.0 - 0.45 * w : 1.35);
                const auto force = std::clamp(0.35 * (d - desired), -80.0, 80.0);
                const auto ux = dx / d, uy = dy / d;
                move[i].x += ux * force;
                move[i].y += uy * force;
                move[j].x -= ux * force;
                move[j].y -= uy * force;
            }
        for (size_t i = 0; i < islands.size(); ++i)
        {
            islands[i].x += move[i].x * step;
            islands[i].y += move[i].y * step;
        }
    }
    // The spring pass settles the structure; this pass guarantees the promise the UI makes — no
    // island overlaps another, so a click always lands on the tag the user sees.
    for (int pass = 0; pass < opt.separationIterations; ++pass)
    {
        bool moved = false;
        for (size_t i = 0; i < islands.size(); ++i)
            for (size_t j = i + 1; j < islands.size(); ++j)
            {
                auto dx = islands[j].x - islands[i].x;
                auto dy = islands[j].y - islands[i].y;
                auto d = std::max(length(dx, dy), 1e-6);
                const auto minimum = islands[i].radius + islands[j].radius + opt.islandGap;
                if (d >= minimum) continue;
                const auto push = (minimum - d) * 0.5 + 0.5;
                const auto ux = dx / d, uy = dy / d;
                islands[i].x -= ux * push;
                islands[i].y -= uy * push;
                islands[j].x += ux * push;
                islands[j].y += uy * push;
                moved = true;
            }
        if (! moved) break;
    }
}
} // namespace

CloudLayout buildCloudLayout(const CloudInput& input, const CloudLayoutOptions& opt)
{
    CloudLayout layout;
    layout.fingerprint = input.fingerprint;
    layout.algorithmVersion = opt.algorithmVersion;
    if (input.nodes.empty()) return layout;

    std::map<int64_t, int> tagCounts;
    for (const auto& node : input.nodes)
        for (auto id : node.tagIds) ++tagCounts[id];

    std::vector<Draft> islands;
    for (const auto& tag : input.tags)
    {
        auto found = tagCounts.find(tag.id);
        if (found == tagCounts.end() || found->second <= 0) continue;
        Draft draft;
        draft.tagId = tag.id;
        draft.name = tag.name;
        draft.colour = tag.colour;
        draft.count = found->second;
        draft.radius = opt.islandBaseRadius + opt.islandRadiusScale * std::sqrt((double) draft.count);
        islands.push_back(std::move(draft));
    }
    if (islands.empty()) return layout;

    std::map<int64_t, size_t> islandByTag;
    for (size_t i = 0; i < islands.size(); ++i) islandByTag[islands[i].tagId] = i;
    // Co-occurrence as a share of the smaller tag: "of the 909 files, this share also carries
    // Rave". Raw counts would make a 9k-tag pair look closer than a tight two-tag pair.
    std::vector<std::vector<double>> weights(islands.size(), std::vector<double>(islands.size(), 0.0));
    for (const auto& c : input.cooccurrence)
    {
        auto a = islandByTag.find(c.tagA);
        auto b = islandByTag.find(c.tagB);
        if (a == islandByTag.end() || b == islandByTag.end()) continue;
        const auto smaller = std::max(1, std::min(islands[a->second].count, islands[b->second].count));
        const auto share = std::clamp((double) c.count / (double) smaller, 0.0, 1.0);
        weights[a->second][b->second] = share;
        weights[b->second][a->second] = share;
    }
    layoutIslands(islands, weights, opt);

    double minX = std::numeric_limits<double>::max(), minY = minX;
    double maxX = std::numeric_limits<double>::lowest(), maxY = maxX;
    for (const auto& island : islands)
    {
        minX = std::min(minX, island.x - island.radius);
        minY = std::min(minY, island.y - island.radius);
        maxX = std::max(maxX, island.x + island.radius);
        maxY = std::max(maxY, island.y + island.radius);
    }
    const auto shiftX = 24.0 - minX, shiftY = 24.0 - minY;
    for (auto& island : islands)
    {
        island.x += shiftX;
        island.y += shiftY;
    }

    // Base acoustic projection: eight universal descriptors, z-scored by median and IQR so one
    // very long pad cannot flatten the axes. Files without descriptors stay at their island centre.
    std::vector<size_t> basicRows;
    for (size_t i = 0; i < input.nodes.size(); ++i)
    {
        if (! input.nodes[i].hasBasic) continue;
        bool finite = true;
        for (auto value : input.nodes[i].basic) finite = finite && std::isfinite(value);
        if (finite) basicRows.push_back(i);
    }
    std::vector<std::vector<float>> basic;
    basic.reserve(basicRows.size());
    for (auto index : basicRows)
    {
        const auto& b = input.nodes[index].basic;
        basic.push_back({ b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7] });
    }
    for (size_t dim = 0; dim < 8; ++dim)
    {
        std::vector<double> column;
        column.reserve(basic.size());
        for (const auto& row : basic) column.push_back(row[dim]);
        const auto centre = median(column);
        const auto spread = std::max(quantile(column, 0.75) - quantile(column, 0.25), 1e-6);
        for (auto& row : basic) row[dim] = (float) std::clamp((row[dim] - centre) / spread, -4.0, 4.0);
    }
    const auto basicComponents = principalComponents(basic, 2);

    // Raw projection first, one value pair per file with descriptors.
    std::vector<Vec2> raw(input.nodes.size());
    std::vector<size_t> rawRows;
    if (basicComponents.size() == 2)
    {
        for (size_t i = 0; i < basic.size(); ++i)
        {
            Vec2 value;
            for (size_t d = 0; d < 8; ++d)
            {
                value.x += basic[i][d] * basicComponents[0][d];
                value.y += basic[i][d] * basicComponents[1][d];
            }
            raw[basicRows[i]] = value;
            rawRows.push_back(basicRows[i]);
        }
    }
    struct Span
    {
        double lowX = 0, highX = 1, lowY = 0, highY = 1;
        bool valid = false;
    };
    Span globalSpan;
    if (! rawRows.empty())
    {
        std::vector<double> xs, ys;
        for (auto index : rawRows) { xs.push_back(raw[index].x); ys.push_back(raw[index].y); }
        globalSpan = { quantile(xs, 0.02), quantile(xs, 0.98), quantile(ys, 0.02), quantile(ys, 0.98), true };
    }
    // Each island stretches its own points to its own 2-98 percentile. A library-wide range would
    // let a handful of outliers flatten every other file into a narrow band, leaving an island that
    // looks empty even though it holds thousands of sounds.
    std::vector<Span> spans(islands.size(), globalSpan);
    for (size_t islandIdx = 0; islandIdx < islands.size(); ++islandIdx)
    {
        std::vector<double> xs, ys;
        for (size_t i = 0; i < input.nodes.size(); ++i)
        {
            const auto& node = input.nodes[i];
            if (node.tagIds.size() != 1 || node.tagIds[0] != islands[islandIdx].tagId) continue;
            if (! node.hasBasic) continue;
            xs.push_back(raw[i].x);
            ys.push_back(raw[i].y);
        }
        if (xs.size() < 8) continue;
        spans[islandIdx] = { quantile(xs, 0.02), quantile(xs, 0.98), quantile(ys, 0.02), quantile(ys, 0.98), true };
    }
    auto unit = [](double v, double low, double high)
    {
        if (high - low < 1e-9) return 0.0;
        return std::clamp(2.0 * (v - low) / (high - low) - 1.0, -1.0, 1.0);
    };

    layout.points.resize(input.nodes.size());
    for (size_t i = 0; i < input.nodes.size(); ++i)
    {
        const auto& node = input.nodes[i];
        double anchorX = 0, anchorY = 0, radius = opt.islandBaseRadius;
        int islandIndex = -1;
        if (! node.tagIds.empty())
        {
            for (auto tagId : node.tagIds)
            {
                auto found = islandByTag.find(tagId);
                if (found == islandByTag.end()) continue;
                const auto& island = islands[found->second];
                anchorX += island.x;
                anchorY += island.y;
                if (islandIndex < 0 || island.radius < radius)
                {
                    radius = island.radius;
                    islandIndex = (int) found->second;
                }
            }
            anchorX /= (double) node.tagIds.size();
            anchorY /= (double) node.tagIds.size();
        }
        const auto& span = islandIndex >= 0 ? spans[(size_t) islandIndex] : globalSpan;
        const Vec2 offset = node.hasBasic
            ? Vec2 { unit(raw[i].x, span.lowX, span.highX), unit(raw[i].y, span.lowY, span.highY) }
            : Vec2 {};
        const auto spread = radius * (node.tagIds.size() > 1 ? opt.pointSpread * opt.multiTagSpread : opt.pointSpread);
        auto& point = layout.points[i];
        point.sampleId = node.sampleId;
        point.colour = node.colour;
        point.islandIndex = islandIndex;
        point.x = (float) (anchorX + offset.x * spread);
        point.y = (float) (anchorY + offset.y * spread);
        if (node.hasBasic) point.flags |= cloudHasBasic;
        if (node.hasV2) point.flags |= cloudHasV2;
        if (node.tagIds.size() > 1) point.flags |= cloudMultiTag;
        if (! node.hasBasic) point.flags |= cloudNoPosition;
    }

    // The 84-value refinement. It only ever moves a point inside its own island cap, so the tag
    // structure stays the map's skeleton and the acoustics sharpen the local neighbourhood.
    std::vector<size_t> vectorRows;
    for (size_t i = 0; i < input.nodes.size(); ++i)
    {
        if (! input.nodes[i].hasV2 || i >= input.v2.size() || input.v2[i].size() != 84) continue;
        bool finite = true;
        for (auto value : input.v2[i]) finite = finite && std::isfinite(value);
        if (finite) vectorRows.push_back(i);
    }
    if (vectorRows.size() >= 3)
    {
        std::vector<std::vector<float>> rows;
        rows.reserve(vectorRows.size());
        for (auto index : vectorRows) rows.push_back(input.v2[index]);
        const auto components = principalComponents(rows, 8);
        if (! components.empty())
        {
            std::vector<std::vector<float>> reduced(rows.size(), std::vector<float>(components.size()));
            for (size_t i = 0; i < rows.size(); ++i)
                for (size_t c = 0; c < components.size(); ++c)
                {
                    double sum = 0;
                    for (size_t d = 0; d < rows[i].size(); ++d) sum += rows[i][d] * components[c][d];
                    reduced[i][(size_t) c] = (float) sum;
                }

            auto anchorOf = [&](size_t index)
            {
                const auto& node = input.nodes[index];
                Vec2 anchor;
                for (auto tagId : node.tagIds)
                {
                    auto found = islandByTag.find(tagId);
                    if (found == islandByTag.end()) continue;
                    anchor.x += islands[found->second].x;
                    anchor.y += islands[found->second].y;
                }
                if (! node.tagIds.empty())
                {
                    anchor.x /= (double) node.tagIds.size();
                    anchor.y /= (double) node.tagIds.size();
                }
                return anchor;
            };

            for (int pass = 0; pass < 2; ++pass)
            {
                // Candidate neighbours come from a grid over the current 2D positions; the actual
                // distance is measured in the reduced 84-value space.
                const auto cell = 48.0f;
                std::map<std::pair<int, int>, std::vector<size_t>> grid;
                for (size_t i = 0; i < vectorRows.size(); ++i)
                {
                    const auto& p = layout.points[vectorRows[i]];
                    grid[{ (int) std::floor(p.x / cell), (int) std::floor(p.y / cell) }].push_back(i);
                }
                const auto alpha = opt.refinementAlpha * (pass == 0 ? 1.0 : 0.5);
                std::vector<Vec2> target(vectorRows.size());
                for (size_t i = 0; i < vectorRows.size(); ++i)
                {
                    const auto& p = layout.points[vectorRows[i]];
                    const auto cx = (int) std::floor(p.x / cell), cy = (int) std::floor(p.y / cell);
                    std::vector<std::pair<float, size_t>> candidates;
                    for (int ox = -1; ox <= 1; ++ox)
                        for (int oy = -1; oy <= 1; ++oy)
                        {
                            auto found = grid.find({ cx + ox, cy + oy });
                            if (found == grid.end()) continue;
                            for (auto j : found->second)
                            {
                                if (j == i) continue;
                                double distance = 0;
                                for (size_t c = 0; c < components.size(); ++c)
                                {
                                    const auto d = (double) reduced[i][c] - (double) reduced[j][c];
                                    distance += d * d;
                                }
                                candidates.push_back({ (float) distance, j });
                            }
                        }
                    if (candidates.size() < 2) continue;
                    const auto k = std::min<size_t>((size_t) std::max(1, opt.kNeighbours), candidates.size());
                    std::partial_sort(candidates.begin(), candidates.begin() + (long) k, candidates.end());
                    // An average of the neighbours' current positions, not of their feature values:
                    // that pulls the point into the local cluster the user is looking at.
                    Vec2 pos;
                    for (size_t n = 0; n < k; ++n)
                    {
                        const auto& q = layout.points[vectorRows[candidates[n].second]];
                        pos.x += q.x;
                        pos.y += q.y;
                    }
                    target[i] = { (pos.x / (double) k - p.x) * alpha, (pos.y / (double) k - p.y) * alpha };
                }
                for (size_t i = 0; i < vectorRows.size(); ++i)
                {
                    const auto index = vectorRows[i];
                    const auto anchor = anchorOf(index);
                    const auto& node = input.nodes[index];
                    double radius = opt.islandBaseRadius;
                    for (auto tagId : node.tagIds)
                    {
                        auto found = islandByTag.find(tagId);
                        if (found != islandByTag.end()) radius = std::min(radius, islands[found->second].radius);
                    }
                    const auto cap = (opt.pointSpread + opt.v2Spread) * radius;
                    auto& p = layout.points[index];
                    auto nx = (double) p.x + target[i].x;
                    auto ny = (double) p.y + target[i].y;
                    const auto dx = nx - anchor.x, dy = ny - anchor.y;
                    const auto distance = length(dx, dy);
                    if (distance > cap && distance > 1e-9)
                    {
                        nx = anchor.x + dx / distance * cap;
                        ny = anchor.y + dy / distance * cap;
                    }
                    p.x = (float) nx;
                    p.y = (float) ny;
                }
            }
        }
    }

    // Two files with identical vectors land on the same spot; a deterministic spiral keeps both
    // visible and clickable without a random jitter that would break reproducibility.
    {
        std::map<std::pair<int, int>, std::vector<size_t>> occupied;
        for (size_t i = 0; i < layout.points.size(); ++i)
        {
            auto& p = layout.points[i];
            auto key = std::pair<int, int> { (int) std::lround(p.x * 2.0f), (int) std::lround(p.y * 2.0f) };
            auto& bucket = occupied[key];
            if (bucket.empty()) { bucket.push_back(i); continue; }
            const auto n = (double) bucket.size();
            const auto angle = n * goldenAngle;
            const auto distance = 0.9 * std::sqrt(n);
            p.x += (float) (std::cos(angle) * distance);
            p.y += (float) (std::sin(angle) * distance);
            bucket.push_back(i);
        }
    }

    layout.islands.reserve(islands.size());
    for (const auto& island : islands)
        layout.islands.push_back({ island.tagId, island.name, island.colour,
                                   (float) island.x, (float) island.y, (float) island.radius, island.count });

    double highX = std::numeric_limits<double>::lowest(), highY = highX;
    double boundMinX = std::numeric_limits<double>::max(), boundMinY = boundMinX;
    for (const auto& p : layout.points)
    {
        boundMinX = std::min(boundMinX, (double) p.x);
        boundMinY = std::min(boundMinY, (double) p.y);
        highX = std::max(highX, (double) p.x);
        highY = std::max(highY, (double) p.y);
    }
    for (const auto& island : layout.islands)
    {
        boundMinX = std::min(boundMinX, (double) island.x - island.radius);
        boundMinY = std::min(boundMinY, (double) island.y - island.radius);
        highX = std::max(highX, (double) island.x + island.radius);
        highY = std::max(highY, (double) island.y + island.radius);
    }
    constexpr double margin = 24.0;
    layout.bounds = juce::Rectangle<float>((float) (boundMinX - margin), (float) (boundMinY - margin),
                                           (float) (highX - boundMinX + 2 * margin),
                                           (float) (highY - boundMinY + 2 * margin));
    return layout;
}

std::vector<std::pair<size_t, float>> nearestNeighbours(const CloudLayout& layout, size_t index, int k)
{
    std::vector<std::pair<size_t, float>> out;
    if (index >= layout.points.size() || k <= 0) return out;
    const auto& origin = layout.points[index];
    for (size_t i = 0; i < layout.points.size(); ++i)
    {
        if (i == index) continue;
        const auto& p = layout.points[i];
        out.push_back({ i, (float) length((double) p.x - origin.x, (double) p.y - origin.y) });
    }
    const auto count = std::min<size_t>((size_t) k, out.size());
    std::partial_sort(out.begin(), out.begin() + (long) count, out.end(),
                      [](const auto& a, const auto& b) { return a.second < b.second; });
    out.resize(count);
    return out;
}
} // namespace sonicator::cloud
