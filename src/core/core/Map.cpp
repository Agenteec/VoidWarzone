#include "core/Map.h"

#include <algorithm>
#include <stdexcept>

#include <glm/common.hpp>
#include <nlohmann/json.hpp>

#include "core/GameData.h"

namespace vw {

using nlohmann::json;

namespace {

glm::vec2 toVec2(const json& v) {
    return {v.at(0).get<float>(), v.at(1).get<float>()};
}

std::vector<glm::vec2> toPoints(const json& v) {
    std::vector<glm::vec2> out;
    for (const auto& p : v)
        out.push_back(toVec2(p));
    return out;
}

}

bool pointInPolygon(glm::vec2 p, const std::vector<glm::vec2>& poly) {
    bool in = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const glm::vec2 a = poly[i];
        const glm::vec2 b = poly[j];
        if ((a.y > p.y) != (b.y > p.y) && p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
            in = !in;
    }
    return in;
}

void Map::fill(const std::vector<glm::vec2>& poly, CellKind kind) {
    if (poly.size() < 3)
        return;
    glm::vec2 lo = poly[0];
    glm::vec2 hi = poly[0];
    glm::vec2 sum{0.0f};
    for (const auto p : poly) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
        sum += p;
    }
    bool any = false;
    for (int y = std::max(0, static_cast<int>(lo.y)); y <= std::min(height_ - 1, static_cast<int>(hi.y)); ++y)
        for (int x = std::max(0, static_cast<int>(lo.x)); x <= std::min(width_ - 1, static_cast<int>(hi.x)); ++x)
            if (pointInPolygon(center({x, y}), poly)) {
                cells_[index(x, y)] = static_cast<std::uint8_t>(kind);
                any = true;
            }
    const glm::ivec2 c = cellOf(sum / static_cast<float>(poly.size()));
    if (!any && inside(c.x, c.y))
        cells_[index(c.x, c.y)] = static_cast<std::uint8_t>(kind);
}

Map::Map(int width, int height) : width_(width), height_(height), cells_(static_cast<std::size_t>(width * height), 0) {
    if (width <= 0 || height <= 0)
        throw std::runtime_error("invalid map size");
}

int Map::laneIndex(std::string_view name) const {
    for (std::size_t i = 0; i < lanes_.size(); ++i)
        if (lanes_[i].name == name)
            return static_cast<int>(i);
    return -1;
}

int Map::requireLane(std::string_view name) const {
    const int i = laneIndex(name);
    if (i < 0)
        throw std::runtime_error("unknown lane: " + std::string(name));
    return i;
}

void Map::addWall(const Rect& r) {
    walls_.push_back(r);
    for (int y = r.y; y < r.y + r.h; ++y)
        for (int x = r.x; x < r.x + r.w; ++x)
            if (inside(x, y))
                cells_[index(x, y)] = 1;
}

Map Map::parse(std::string_view text) {
    const auto j = json::parse(text);
    Map m(j.at("width").get<int>(), j.at("height").get<int>());
    m.name_ = j.value("name", "");

    m.theme_ = j.value("theme", "");
    for (const auto& w : j.value("walls", json::array()))
        m.addWall({w.at(0).get<int>(), w.at(1).get<int>(), w.at(2).get<int>(), w.at(3).get<int>()});
    for (const auto& a : j.value("areas", json::array())) {
        AreaDef area{a.at("kind").get<std::string>(), toPoints(a.at("poly"))};
        if (area.kind == "forest")
            m.fill(area.poly, CellKind::Forest);
        m.areas_.push_back(std::move(area));
    }
    for (const auto& b : j.value("buildings", json::array())) {
        BuildingDef building{toPoints(b.at("poly")), b.value("levels", 5), b.value("kind", "")};
        m.fill(building.poly, CellKind::Building);
        m.buildings_.push_back(std::move(building));
    }
    for (const auto& p : j.value("paths", json::array()))
        m.paths_.push_back({p.value("kind", ""), p.value("width", 1.0f), toPoints(p.at("points"))});

    for (const auto& l : j.value("lanes", json::array())) {
        Lane lane;
        lane.name = l.at("name").get<std::string>();
        for (const auto& p : l.at("points"))
            lane.points.push_back(toVec2(p));
        if (lane.points.size() < 2)
            throw std::runtime_error("lane needs 2+ points: " + lane.name);
        m.lanes_.push_back(std::move(lane));
    }

    for (const auto& u : j.value("units", json::array())) {
        PlacedUnit pu;
        pu.unit = u.at("unit").get<std::string>();
        pu.role = u.value("role", "");
        pu.tag = u.value("tag", "");
        pu.team = u.value("team", std::uint8_t{0});
        pu.owner = u.value("owner", -1);
        pu.pos = toVec2(u.at("pos"));
        for (const auto& mod : u.value("modules", json::array()))
            pu.modules.push_back(mod.get<std::string>());
        m.units_.push_back(std::move(pu));
    }

    for (const auto& s : j.value("spawners", json::array())) {
        SpawnerDef sd;
        sd.unit = s.at("unit").get<std::string>();
        sd.team = s.value("team", std::uint8_t{0});
        sd.lane = m.requireLane(s.at("lane").get<std::string>());
        sd.interval = s.value("interval", sd.interval);
        sd.delay = s.value("delay", sd.delay);
        sd.count = s.value("count", sd.count);
        sd.offset = s.value("offset", sd.offset);
        sd.building = s.value("building", "");
        m.spawners_.push_back(std::move(sd));
    }

    if (const auto ps = j.find("playerStart"); ps != j.end()) {
        m.playerStart_.pos = toVec2(ps->at("pos"));
        for (const auto& u : ps->at("units"))
            m.playerStart_.units.push_back(u.get<std::string>());
    }

    for (const auto& r : j.value("resourcePoints", json::array())) {
        ResourcePointDef rp;
        rp.name = r.value("name", "");
        rp.pos = toVec2(r.at("pos"));
        rp.radius = r.value("radius", rp.radius);
        rp.income = r.value("income", rp.income);
        m.resourcePoints_.push_back(std::move(rp));
    }

    if (const auto d = j.find("defense"); d != j.end()) {
        auto& def = m.defense_;
        def.enabled = true;
        def.firstWaveDelay = d->value("firstWaveDelay", def.firstWaveDelay);
        def.waveInterval = d->value("waveInterval", def.waveInterval);
        def.baseIncome = d->value("baseIncome", def.baseIncome);
        def.startEnergy = d->value("startEnergy", def.startEnergy);
        for (const auto& w : d->at("waves")) {
            WaveDef wave;
            wave.delay = w.value("delay", wave.delay);
            for (const auto& g : w.value("groups", json::array())) {
                WaveGroup group;
                group.unit = g.at("unit").get<std::string>();
                group.count = g.value("count", group.count);
                const auto& lanes = g.at("lanes");
                if (lanes.is_string() && lanes.get<std::string>() == "raid") {
                    group.raid = true;
                } else if (lanes.is_string() && lanes.get<std::string>() == "all") {
                    for (std::size_t i = 0; i < m.lanes_.size(); ++i)
                        group.lanes.push_back(static_cast<int>(i));
                } else {
                    for (const auto& l : lanes)
                        group.lanes.push_back(m.requireLane(l.get<std::string>()));
                }
                wave.groups.push_back(std::move(group));
            }
            if (const auto b = w.find("boss"); b != w.end()) {
                wave.boss = b->at("unit").get<std::string>();
                wave.bossLane = m.requireLane(b->at("lane").get<std::string>());
            }
            def.waves.push_back(std::move(wave));
        }
    }

    m.hash_ = fnv1a(text);
    return m;
}

}
