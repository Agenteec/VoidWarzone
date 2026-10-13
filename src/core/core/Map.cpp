#include "core/Map.h"

#include <stdexcept>

#include <nlohmann/json.hpp>

#include "core/GameData.h"

namespace vw {

using nlohmann::json;

namespace {

glm::vec2 toVec2(const json& v) {
    return {v.at(0).get<float>(), v.at(1).get<float>()};
}

}

Map::Map(int width, int height) : width_(width), height_(height), cells_(static_cast<std::size_t>(width * height), 0) {
    if (width <= 0 || height <= 0)
        throw std::runtime_error("invalid map size");
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

    for (const auto& w : j.value("walls", json::array()))
        m.addWall({w.at(0).get<int>(), w.at(1).get<int>(), w.at(2).get<int>(), w.at(3).get<int>()});

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
        pu.team = u.value("team", std::uint8_t{0});
        pu.owner = u.value("owner", -1);
        pu.pos = toVec2(u.at("pos"));
        m.units_.push_back(std::move(pu));
    }

    for (const auto& s : j.value("spawners", json::array())) {
        SpawnerDef sd;
        sd.unit = s.at("unit").get<std::string>();
        sd.team = s.value("team", std::uint8_t{0});
        const auto laneName = s.at("lane").get<std::string>();
        sd.lane = -1;
        for (std::size_t i = 0; i < m.lanes_.size(); ++i)
            if (m.lanes_[i].name == laneName)
                sd.lane = static_cast<int>(i);
        if (sd.lane < 0)
            throw std::runtime_error("unknown lane: " + laneName);
        sd.interval = s.value("interval", sd.interval);
        sd.delay = s.value("delay", sd.delay);
        sd.count = s.value("count", sd.count);
        sd.offset = s.value("offset", sd.offset);
        m.spawners_.push_back(std::move(sd));
    }

    m.hash_ = fnv1a(text);
    return m;
}

}
