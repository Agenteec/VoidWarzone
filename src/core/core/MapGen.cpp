#include "core/MapGen.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <vector>

#include <nlohmann/json.hpp>

namespace vw {

namespace {

using nlohmann::json;

constexpr int kSize = 200;
constexpr int kCenter = kSize / 2;
constexpr int kRing = 72;
constexpr std::uint8_t kOpen = 0, kRock = 1, kForest = 3, kWater = 4;
// unit vectors * 1000 for the 8 start slots and the 8 directions between them
constexpr std::array<std::array<int, 2>, 8> kSlot{{{1000, 0}, {707, 707}, {0, 1000}, {-707, 707}, {-1000, 0}, {-707, -707}, {0, -1000}, {707, -707}}};
constexpr std::array<std::array<int, 2>, 8> kMid{{{924, 383}, {383, 924}, {-383, 924}, {-924, 383}, {-924, -383}, {-383, -924}, {383, -924}, {924, -383}}};

struct Rng {
    std::uint64_t s;
    std::uint32_t next() {
        s += 0x9E3779B97F4A7C15ull;
        std::uint64_t z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return static_cast<std::uint32_t>((z ^ (z >> 31)) >> 32);
    }
    int range(int lo, int hi) { return lo + static_cast<int>(next() % static_cast<std::uint32_t>(hi - lo + 1)); }
};

std::uint32_t hash2(std::uint32_t seed, int x, int y) {
    std::uint32_t h = seed ^ (static_cast<std::uint32_t>(x) * 0x8da6b343u) ^ (static_cast<std::uint32_t>(y) * 0xd8163841u);
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

int noise(std::uint32_t seed, int x, int y, int cell) {
    const int gx = x / cell, gy = y / cell;
    const int fx = (x % cell) * 1024 / cell, fy = (y % cell) * 1024 / cell;
    const int sx = static_cast<int>(static_cast<std::int64_t>(fx) * fx * (3072 - 2 * fx) / (1024 * 1024));
    const int sy = static_cast<int>(static_cast<std::int64_t>(fy) * fy * (3072 - 2 * fy) / (1024 * 1024));
    const int v00 = static_cast<int>(hash2(seed, gx, gy) & 0xffff), v10 = static_cast<int>(hash2(seed, gx + 1, gy) & 0xffff);
    const int v01 = static_cast<int>(hash2(seed, gx, gy + 1) & 0xffff), v11 = static_cast<int>(hash2(seed, gx + 1, gy + 1) & 0xffff);
    const int a = v00 + (v10 - v00) * sx / 1024;
    const int b = v01 + (v11 - v01) * sx / 1024;
    return a + (b - a) * sy / 1024;
}

int field(std::uint32_t seed, int x, int y) {
    return (noise(seed, x, y, 48) * 4 + noise(seed ^ 0x51u, x, y, 24) * 2 + noise(seed ^ 0xa3u, x, y, 12)) / 7;
}

struct Grid {
    std::vector<std::uint8_t> kind = std::vector<std::uint8_t>(kSize * kSize, kWater);
    std::vector<std::uint8_t> reserved = std::vector<std::uint8_t>(kSize * kSize, 0);
    std::uint8_t& at(int x, int y) { return kind[static_cast<std::size_t>(y * kSize + x)]; }
    std::uint8_t& res(int x, int y) { return reserved[static_cast<std::size_t>(y * kSize + x)]; }
    static bool inside(int x, int y) { return x >= 0 && y >= 0 && x < kSize && y < kSize; }

    void disc(int cx, int cy, int r, std::uint8_t value, std::uint8_t mark = 0) {
        for (int y = cy - r; y <= cy + r; ++y)
            for (int x = cx - r; x <= cx + r; ++x)
                if (inside(x, y) && (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) {
                    at(x, y) = value;
                    res(x, y) = std::max(res(x, y), mark);
                }
    }
    void line(int ax, int ay, int bx, int by, int half, std::uint8_t value, std::uint8_t mark) {
        const int steps = std::max(std::abs(bx - ax), std::abs(by - ay));
        for (int i = 0; i <= steps; ++i)
            disc(ax + (bx - ax) * i / std::max(steps, 1), ay + (by - ay) * i / std::max(steps, 1), half, value, mark);
    }
};

std::string encode(const std::vector<std::uint8_t>& cells) {
    std::string out;
    std::size_t i = 0;
    while (i < cells.size()) {
        std::size_t j = i;
        while (j < cells.size() && cells[j] == cells[i])
            ++j;
        if (!out.empty())
            out += ',';
        out += std::to_string(cells[i]) + ':' + std::to_string(j - i);
        i = j;
    }
    return out;
}

json point2(int x, int y) {
    return json::array({x, y});
}

}

std::string generateMap(std::uint32_t seed) {
    Rng rng{seed * 0x2545F4914F6CDD1Dull + 1};
    const int layout = rng.range(0, 2);
    const bool winter = rng.range(0, 1) == 0;
    Grid g;
    const std::uint32_t ns = rng.next();
    const int riverPhase = rng.range(0, 1000);
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x) {
            const int h = field(ns, x, y);
            bool land = layout == 0 ? h > 36500 : layout == 1 ? h > 27000 : true;
            if (layout == 2) {
                const int dx = x - kCenter, dy = y - kCenter;
                const int wobble = (noise(ns ^ 0x77u, x + riverPhase, y, 24) - 32768) * 5 / 32768;
                const int r2 = dx * dx + dy * dy;
                const int inner = 30 + wobble, outer = 39 + wobble;
                land = r2 < inner * inner || r2 > outer * outer;
                if (land && h < 22000)
                    land = false;
            }
            if (x < 2 || y < 2 || x >= kSize - 2 || y >= kSize - 2)
                land = layout == 1 || layout == 2 ? true : land;
            g.at(x, y) = land ? kOpen : kWater;
        }

    std::array<std::array<int, 2>, 8> starts{};
    for (int k = 0; k < 8; ++k)
        starts[static_cast<std::size_t>(k)] = {kCenter + kSlot[static_cast<std::size_t>(k)][0] * kRing / 1000, kCenter + kSlot[static_cast<std::size_t>(k)][1] * kRing / 1000};
    std::array<std::array<int, 2>, 8> mids{};
    for (int k = 0; k < 8; ++k)
        mids[static_cast<std::size_t>(k)] = {kCenter + kMid[static_cast<std::size_t>(k)][0] * 50 / 1000, kCenter + kMid[static_cast<std::size_t>(k)][1] * 50 / 1000};

    for (const auto& s : starts) {
        g.disc(s[0], s[1], 18, kOpen, 1);
        g.line(s[0], s[1], kCenter, kCenter, 4, kOpen, 2);
    }
    for (const auto& m : mids)
        g.disc(m[0], m[1], 7, kOpen, 1);
    for (int k = 0; k < 8; ++k) {
        const auto& m = mids[static_cast<std::size_t>(k)];
        for (const int j : {k, (k + 1) % 8}) {
            const auto& s = starts[static_cast<std::size_t>(j)];
            g.line(s[0], s[1], m[0], m[1], 3, kOpen, 2);
        }
    }
    g.disc(kCenter, kCenter, 20, kOpen, 1);

    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x) {
            if (g.at(x, y) != kOpen || g.res(x, y))
                continue;
            bool clear = true;
            for (const auto& s : starts)
                clear = clear && (x - s[0]) * (x - s[0]) + (y - s[1]) * (y - s[1]) > 22 * 22;
            if (clear && field(ns ^ 0x99u, x, y) > 50000)
                g.at(x, y) = kRock;
        }

    json buildings = json::array(), areas = json::array(), paths = json::array(), units = json::array(), points = json::array(), props = json::array();
    auto freeRect = [&](int x0, int y0, int w, int h) {
        for (int y = y0 - 1; y <= y0 + h; ++y)
            for (int x = x0 - 1; x <= x0 + w; ++x)
                if (!Grid::inside(x, y) || g.at(x, y) != kOpen || g.res(x, y) >= 2)
                    return false;
        for (const auto& s : starts)
            if ((x0 + w / 2 - s[0]) * (x0 + w / 2 - s[0]) + (y0 + h / 2 - s[1]) * (y0 + h / 2 - s[1]) < 10 * 10)
                return false;
        return true;
    };
    auto addBuilding = [&](int cx, int cy, int w, int h, const char* kind, int levels, bool ruin) {
        const int x0 = cx - w / 2, y0 = cy - h / 2;
        if (!freeRect(x0, y0, w, h))
            return false;
        for (int y = y0 - 1; y <= y0 + h; ++y)
            for (int x = x0 - 1; x <= x0 + w; ++x)
                g.res(x, y) = 3;
        buildings.push_back({{"poly", json::array({point2(x0, y0), point2(x0 + w, y0), point2(x0 + w, y0 + h), point2(x0, y0 + h)})},
                             {"levels", levels}, {"kind", kind}, {"ruin", ruin}});
        return true;
    };

    for (int k = 0; k < 8; ++k) {
        const auto& v = kSlot[static_cast<std::size_t>(k)];
        const auto& s = starts[static_cast<std::size_t>(k)];
        const int ox = s[0] + v[0] * 14 / 1000, oy = s[1] + v[1] * 14 / 1000;
        const int px = -v[1], py = v[0];
        const bool wide = std::abs(px) >= std::abs(py);
        for (int i = -1; i <= 1; ++i) {
            const int cx = ox + px * i * 9 / 1000, cy = oy + py * i * 9 / 1000;
            if (i == 0)
                addBuilding(cx, cy, wide ? 8 : 2, wide ? 2 : 8, "garage", 1, false);
            else if (rng.range(0, 2) == 0)
                addBuilding(cx, cy, 3, 3, "house", 2, false);
            else
                addBuilding(cx, cy, wide ? 8 : 3, wide ? 3 : 8, "panel", rng.range(0, 1) ? 9 : 5, rng.range(0, 3) == 0);
        }
        points.push_back({{"name", "H" + std::to_string(k + 1)}, {"pos", point2(s[0] + px * 10 / 1000, s[1] + py * 10 / 1000)}, {"radius", 3.0}, {"income", 2}});
        paths.push_back({{"kind", "road"}, {"width", 1.5}, {"points", json::array({point2(s[0], s[1]), point2(kCenter, kCenter)})}});
    }
    for (int k = 0; k < 8; ++k) {
        const auto& m = mids[static_cast<std::size_t>(k)];
        points.push_back({{"name", std::string(1, static_cast<char>('A' + k))}, {"pos", point2(m[0], m[1])}, {"radius", 3.5}, {"income", 3}});
        units.push_back({{"unit", "raider_bunker"}, {"team", 1}, {"pos", point2(m[0] + 3, m[1] + 2)}});
        units.push_back({{"unit", rng.range(0, 1) ? "raider_technical" : "raider_gatling"}, {"team", 1}, {"pos", point2(m[0] - 2, m[1] - 3)}});

        const auto& v = kMid[static_cast<std::size_t>(k)];
        addBuilding(kCenter + v[0] * 15 / 1000, kCenter + v[1] * 15 / 1000, std::abs(v[0]) > std::abs(v[1]) ? 3 : 7,
                    std::abs(v[0]) > std::abs(v[1]) ? 7 : 3, "panel", rng.range(0, 1) ? 9 : 5, rng.range(0, 2) != 0);
    }
    points.push_back({{"name", "Center1"}, {"pos", point2(kCenter + 7, kCenter)}, {"radius", 4.0}, {"income", 5}});
    points.push_back({{"name", "Center2"}, {"pos", point2(kCenter - 7, kCenter)}, {"radius", 4.0}, {"income", 5}});
    units.push_back({{"unit", "raider_camp"}, {"team", 1}, {"pos", point2(kCenter, kCenter - 4)}});
    units.push_back({{"unit", "raider_bunker"}, {"team", 1}, {"pos", point2(kCenter + 4, kCenter + 5)}});
    units.push_back({{"unit", "raider_rocket"}, {"team", 1}, {"pos", point2(kCenter - 3, kCenter + 3)}});
    props.push_back({{"model", "prop_cell_tower"}, {"pos", point2(kCenter + 12, kCenter - 12)}, {"yaw", 0}, {"radius", 1.0}});

    for (int i = 0; i < 40 && areas.size() < 16; ++i) {
        const int x = rng.range(8, kSize - 9), y = rng.range(8, kSize - 9), r = rng.range(3, 5);
        bool ok = true;
        for (int yy = y - r - 1; yy <= y + r + 1 && ok; ++yy)
            for (int xx = x - r - 1; xx <= x + r + 1 && ok; ++xx)
                ok = g.at(xx, yy) == kOpen && g.res(xx, yy) == 0;
        if (!ok)
            continue;
        json poly = json::array();
        for (const auto& v : kSlot)
            poly.push_back(point2(x + v[0] * r / 1000, y + v[1] * r / 1000));
        areas.push_back({{"kind", "forest"}, {"poly", poly}});
        for (int yy = y - r; yy <= y + r; ++yy)
            for (int xx = x - r; xx <= x + r; ++xx)
                g.res(xx, yy) = 3;
    }

    json slots = json::array();
    for (const auto& s : starts)
        slots.push_back(point2(s[0], s[1]));
    static constexpr const char* kLayouts[] = {"islands", "lakes", "moat"};
    json doc = {
        {"name", std::string("Random ") + kLayouts[layout] + " #" + std::to_string(seed)},
        {"width", kSize},
        {"height", kSize},
        {"theme", winter ? "winter" : "autumn"},
        {"cells", encode(g.kind)},
        {"playerStart", {{"pos", point2(starts[0][0], starts[0][1])}, {"units", {"kshm", "builder_truck", "builder_truck", "uaz", "uaz", "mtlb"}}, {"slots", slots}}},
        {"defense", {{"startEnergy", 600}, {"baseIncome", 2.0}, {"waves", json::array()}}},
        {"resourcePoints", points},
        {"units", units},
        {"buildings", buildings},
        {"areas", areas},
        {"paths", paths},
        {"props", props},
        {"lanes", json::array()},
    };
    (void)kForest;
    return doc.dump();
}

std::optional<std::string> generatedMap(std::string_view name) {
    if (name.substr(0, kRandomMapPrefix.size()) != kRandomMapPrefix)
        return std::nullopt;
    const std::string_view digits = name.substr(kRandomMapPrefix.size());
    std::uint32_t seed = 0;
    const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), seed);
    if (ec != std::errc() || end != digits.data() + digits.size())
        return std::nullopt;
    return generateMap(seed);
}

}
