#include "App.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>
#include <glm/geometric.hpp>

#include "Editor.h"
#include "ui/Widgets.h"

namespace vw::client {

using nlohmann::json;
using namespace ui;

namespace {

constexpr const char* kToolNames[] = {ICON_FA_MOUSE_POINTER "  Select", ICON_FA_CUBES "  Units", ICON_FA_PAINT_BRUSH "  Terrain",
                                      ICON_FA_BUILDING "  Buildings", ICON_FA_ROAD "  Lanes", ICON_FA_MAP_MARKER "  Points"};
constexpr const char* kBuildingKinds[] = {"panel", "block", "public", "industrial", "house", "garage"};
constexpr const char* kRoles[] = {"", "core", "portal"};
constexpr const char* kCellNames[] = {"Open", "Rock", "Building", "Forest"};
const glm::vec4 kLaneColors[] = {{1.0f, 0.4f, 0.2f, 1.0f}, {0.3f, 0.8f, 1.0f, 1.0f}, {0.6f, 1.0f, 0.3f, 1.0f}, {1.0f, 0.8f, 0.2f, 1.0f},
                                 {0.9f, 0.4f, 1.0f, 1.0f}};

json point(glm::vec2 p) {
    return json::array({std::round(p.x * 100.0f) / 100.0f, std::round(p.y * 100.0f) / 100.0f});
}

glm::vec2 jvec(const json& j) {
    return {j.at(0).get<float>(), j.at(1).get<float>()};
}

}

void App::openEditor(const std::string& mapId) {
    editor_ = std::make_unique<EditorState>();
    auto& es = *editor_;
    try {
        if (mapId.empty())
            throw std::runtime_error("new map");
        es.doc = json::parse(readMap(mapId));
        es.id = mapId;
    } catch (const std::exception&) {
        newEditorMap(es.newWidth, es.newHeight, es.newWinter);
        return;
    }
    std::snprintf(es.saveId, sizeof(es.saveId), "%s", es.id.c_str());
    std::snprintf(es.name, sizeof(es.name), "%s", es.doc.value("name", es.id).c_str());
    es.cells.clear();
    editorReparse(true);
    camera_.target = options_.camera.value_or(glm::vec2(map_->width(), map_->height()) * 0.5f);
    camera_.distance = options_.zoom > 0.0f ? options_.zoom : 60.0f;
    setScreen(Screen::Editor);
}

void App::newEditorMap(int width, int height, bool winter) {
    if (!editor_)
        editor_ = std::make_unique<EditorState>();
    auto& es = *editor_;
    width = std::clamp(width, 32, 1024);
    height = std::clamp(height, 32, 1024);
    const float my = static_cast<float>(height) * 0.5f;
    es.doc = {{"name", "New map"},
              {"width", width},
              {"height", height},
              {"units", json::array({{{"unit", "core"}, {"role", "core"}, {"team", 0}, {"pos", point({8.0f, my})}},
                                     {{"unit", "portal"}, {"role", "portal"}, {"team", 1}, {"pos", point({static_cast<float>(width) - 8.0f, my})}}})},
              {"lanes", json::array({{{"name", "mid"}, {"points", json::array({point({8.0f, my}), point({static_cast<float>(width) - 8.0f, my})})}}})},
              {"playerStart", {{"pos", point({20.0f, my})}, {"units", json::array({"tank", "tank", "scout", "engineer"})}}}};
    if (winter)
        es.doc["theme"] = "winter";
    try {
        auto defense = json::parse(readMap("siege_test")).at("defense");
        for (auto& wave : defense.at("waves"))
            if (wave.contains("boss"))
                wave["boss"]["lane"] = "mid";
        es.doc["defense"] = defense;
    } catch (const std::exception&) {
    }
    es.id.clear();
    es.saveId[0] = 0;
    std::snprintf(es.name, sizeof(es.name), "New map");
    es.cells.clear();
    es.undo.clear();
    editorReparse(true);
    camera_.target = glm::vec2(map_->width(), map_->height()) * 0.5f;
    camera_.distance = 60.0f;
    setScreen(Screen::Editor);
}

void App::closeEditor() {
    editor_.reset();
    map_.reset();
    entities_.clear();
    mapVisualsHash_ = 0;
    renderer_.setGround({});
    camera_.ground = {};
    camera_.height = 0.0f;
    applyGraphics();
    setScreen(Screen::Menu);
}

void App::editorWriteCells() {
    auto& es = *editor_;
    if (es.cells.empty() || !map_)
        return;
    Map grid(map_->width(), map_->height());
    for (int y = 0; y < grid.height(); ++y)
        for (int x = 0; x < grid.width(); ++x)
            grid.setCell(x, y, es.cells[static_cast<std::size_t>(y * grid.width() + x)]);
    es.doc["cells"] = grid.encodeCells();
    es.doc.erase("walls");
    if (es.doc.contains("areas")) {
        json kept = json::array();
        for (const auto& a : es.doc.at("areas"))
            if (a.value("kind", "") != "forest")
                kept.push_back(a);
        es.doc["areas"] = kept;
    }
}

bool App::editorReparse(bool visuals) {
    auto& es = *editor_;
    editorWriteCells();
    try {
        auto parsed = std::make_unique<Map>(Map::parse(es.doc.dump()));
        map_ = std::move(parsed);
    } catch (const std::exception& e) {
        es.status = std::string("invalid map: ") + e.what();
        return false;
    }
    if (es.cells.empty()) {
        es.cells.resize(static_cast<std::size_t>(map_->width() * map_->height()));
        for (int y = 0; y < map_->height(); ++y)
            for (int x = 0; x < map_->width(); ++x) {
                const auto k = map_->cellKind(x, y);
                es.cells[static_cast<std::size_t>(y * map_->width() + x)] = k == CellKind::Building ? CellKind::Open : k;
            }
    }
    entities_.clear();
    std::uint32_t id = 1;
    for (const auto& u : map_->units()) {
        const UnitDefId def = data_->find(u.unit);
        if (def == kInvalidUnitDef)
            continue;
        net::InterpolatedEntity e;
        e.id = id++;
        e.def = def;
        e.team = u.team;
        e.owner = u.owner >= 0 ? static_cast<std::uint8_t>(u.owner) : net::kNoOwner;
        e.pos = u.pos;
        e.facing = u.team == 0 ? 0.0f : 3.14159f;
        e.aim = e.facing;
        e.hp = 1;
        e.maxHp = 1;
        entities_.push_back(e);
    }
    if (visuals) {
        mapVisuals_ = buildMapVisuals(*map_, *data_, renderer_, *models_);
        mapVisualsHash_ = map_->hash();
        minimapHash_ = 0;
        buildMinimap();
        applyGraphics();
        renderer_.setGround([this](float x, float z) { return mapVisuals_.heightAt(x, z); });
        camera_.ground = [this](float x, float z) { return mapVisuals_.heightAt(x, z); };
        es.dirty = false;
    }
    return true;
}

void App::editorChange() {
    auto& es = *editor_;
    editorWriteCells();
    es.undo.push_back(es.doc);
    if (es.undo.size() > 40)
        es.undo.erase(es.undo.begin());
    es.changed = true;
}

void App::editorUndo() {
    auto& es = *editor_;
    if (es.undo.empty())
        return;
    es.doc = es.undo.back();
    es.undo.pop_back();
    es.cells.clear();
    es.selectedUnit = es.selectedBuilding = es.selectedPoint = -1;
    editorReparse(true);
}

void App::editorSave() {
    auto& es = *editor_;
    const std::string id = es.saveId;
    if (!validMapName(id)) {
        es.status = "map id: lowercase letters, digits and _ only";
        return;
    }
    es.doc["name"] = std::string(es.name);
    editorWriteCells();
    std::error_code ec;
    std::filesystem::create_directories(userMapsDir(), ec);
    std::ofstream f(std::filesystem::path(userMapsDir()) / (id + ".json"), std::ios::binary);
    f << es.doc.dump();
    if (!f) {
        es.status = "cannot write " + userMapsDir() + id + ".json";
        return;
    }
    es.id = id;
    es.changed = false;
    es.status = "saved to " + userMapsDir() + id + ".json";
    loadUserMaps();
}

void App::editorPlay() {
    auto& es = *editor_;
    editorSave();
    if (es.changed || es.id.empty())
        return;
    options_.map = es.id;
    const std::string id = es.id;
    closeEditor();
    options_.map = id;
    startHost(false);
}

void App::loadUserMaps() {
    std::erase_if(maps_, [](const CatalogEntry& m) { return m.custom; });
    for (const auto& id : listJson(userMapsDir())) {
        if (!validMapName(id) || std::any_of(maps_.begin(), maps_.end(), [&](const CatalogEntry& m) { return m.id == id; }))
            continue;
        std::string name = id;
        try {
            name = json::parse(readMap(id)).value("name", id);
        } catch (const std::exception&) {
            continue;
        }
        maps_.push_back({id, name + " (custom)", "1-8", "Made in the map editor", true});
    }
}

void App::updateEditor(float dt) {
    auto& es = *editor_;
    if (es.dirty && !es.dragging && now_ - es.dirtyAt > 0.35)
        editorReparse(true);
    updateCamera(dt);
}

void App::editorApply(glm::vec2 p, bool start) {
    auto& es = *editor_;
    const auto ground = camera_.screenToGround(p.x, p.y);
    if (!ground || !map_)
        return;
    const glm::vec2 g = glm::clamp(*ground, glm::vec2(0.5f), glm::vec2(map_->width(), map_->height()) - 0.5f);
    es.dragNow = g;
    auto& units = es.doc["units"];
    switch (es.tool) {
    case EditorTool::Select: {
        if (start) {
            es.selectedUnit = es.selectedBuilding = es.selectedPoint = -1;
            float best = 1e9f;
            for (std::size_t i = 0; i < units.size(); ++i) {
                const UnitDefId def = data_->find(units[i].value("unit", ""));
                const float r = def == kInvalidUnitDef ? 1.0f : data_->unit(def).radius;
                const float d = glm::distance(jvec(units[i].at("pos")), g);
                if (d < r + 0.6f && d < best) {
                    best = d;
                    es.selectedUnit = static_cast<int>(i);
                }
            }
            if (es.selectedUnit < 0 && es.doc.contains("buildings"))
                for (std::size_t i = 0; i < map_->buildings().size(); ++i)
                    if (pointInPolygon(g, map_->buildings()[i].poly))
                        es.selectedBuilding = static_cast<int>(i);
            if (es.selectedUnit >= 0) {
                editorChange();
                es.dragging = true;
            }
        } else if (es.dragging && es.selectedUnit >= 0 && es.selectedUnit < static_cast<int>(units.size())) {
            units[static_cast<std::size_t>(es.selectedUnit)]["pos"] = point(g);
            editorReparse(false);
        }
        break;
    }
    case EditorTool::Units:
        if (!start)
            break;
        editorChange();
        {
            json u = {{"unit", data_->unit(static_cast<UnitDefId>(es.unitDef)).id}, {"team", es.team}, {"pos", point(g)}};
            if (es.role > 0)
                u["role"] = kRoles[es.role];
            units.push_back(u);
        }
        es.selectedUnit = static_cast<int>(units.size()) - 1;
        editorReparse(false);
        break;
    case EditorTool::Terrain: {
        if (start)
            editorChange();
        es.dragging = true;
        const int r = es.brush;
        for (int y = static_cast<int>(g.y) - r; y <= static_cast<int>(g.y) + r; ++y)
            for (int x = static_cast<int>(g.x) - r; x <= static_cast<int>(g.x) + r; ++x) {
                if (!map_->inside(x, y) || glm::distance(Map::center({x, y}), g) > static_cast<float>(r) + 0.5f)
                    continue;
                const auto kind = static_cast<CellKind>(es.brushKind);
                es.cells[static_cast<std::size_t>(y * map_->width() + x)] = kind;
                if (map_->cellKind(x, y) != CellKind::Building)
                    map_->setCell(x, y, kind);
            }
        es.dirty = true;
        es.dirtyAt = now_;
        break;
    }
    case EditorTool::Buildings:
        if (start) {
            es.dragStart = g;
            es.dragging = true;
        }
        break;
    case EditorTool::Lanes: {
        if (!start)
            break;
        auto& lanes = es.doc["lanes"];
        if (lanes.empty())
            break;
        es.lane = std::clamp(es.lane, 0, static_cast<int>(lanes.size()) - 1);
        editorChange();
        auto& pts = lanes[static_cast<std::size_t>(es.lane)]["points"];
        int best = -1;
        for (std::size_t i = 0; i < pts.size(); ++i)
            if (glm::distance(jvec(pts[i]), g) < 1.5f)
                best = static_cast<int>(i);
        if (best >= 0) {
            es.selectedPoint = best;
            es.dragging = true;
        } else {
            pts.insert(pts.end() - (pts.size() >= 2 ? 1 : 0), point(g));
            es.selectedPoint = static_cast<int>(pts.size()) - 2;
        }
        es.dirty = true;
        es.dirtyAt = now_;
        editorReparse(false);
        break;
    }
    case EditorTool::Points:
        if (!start)
            break;
        editorChange();
        if (es.pointIsStart) {
            es.doc["playerStart"]["pos"] = point(g);
        } else {
            auto& pts = es.doc["resourcePoints"];
            const std::string name(1, static_cast<char>('A' + std::min<std::size_t>(pts.size(), 25)));
            pts.push_back({{"name", name}, {"pos", point(g)}, {"radius", 3.5f}, {"income", 3}});
        }
        editorReparse(false);
        break;
    default:
        break;
    }
}

void App::editorRelease(glm::vec2 p) {
    auto& es = *editor_;
    if (!es.dragging)
        return;
    es.dragging = false;
    if (es.tool == EditorTool::Buildings) {
        const glm::vec2 a = glm::min(es.dragStart, es.dragNow);
        const glm::vec2 b = glm::max(es.dragStart, es.dragNow);
        if (b.x - a.x >= 0.8f && b.y - a.y >= 0.8f) {
            editorChange();
            json poly = json::array({point(a), point({b.x, a.y}), point(b), point({a.x, b.y})});
            json building = {{"poly", poly}, {"levels", es.levels}, {"kind", kBuildingKinds[es.buildingKind]}};
            if (es.ruin)
                building["ruin"] = true;
            es.doc["buildings"].push_back(building);
            editorReparse(true);
        }
        return;
    }
    if (es.tool == EditorTool::Lanes && es.selectedPoint >= 0 && es.lane >= 0) {
        auto& pts = es.doc["lanes"][static_cast<std::size_t>(es.lane)]["points"];
        if (es.selectedPoint < static_cast<int>(pts.size())) {
            if (const auto g = camera_.screenToGround(p.x, p.y))
                pts[static_cast<std::size_t>(es.selectedPoint)] = point(*g);
            editorReparse(false);
        }
    }
    if (es.tool == EditorTool::Terrain) {
        es.dirty = true;
        es.dirtyAt = now_;
    }
}

void App::editorDelete() {
    auto& es = *editor_;
    if (es.selectedUnit >= 0 && es.selectedUnit < static_cast<int>(es.doc["units"].size())) {
        editorChange();
        es.doc["units"].erase(static_cast<std::size_t>(es.selectedUnit));
        es.selectedUnit = -1;
        editorReparse(false);
    } else if (es.selectedBuilding >= 0 && es.doc.contains("buildings") && es.selectedBuilding < static_cast<int>(es.doc["buildings"].size())) {
        editorChange();
        es.doc["buildings"].erase(static_cast<std::size_t>(es.selectedBuilding));
        es.selectedBuilding = -1;
        editorReparse(true);
    } else if (es.tool == EditorTool::Lanes && es.selectedPoint >= 0) {
        auto& pts = es.doc["lanes"][static_cast<std::size_t>(es.lane)]["points"];
        if (pts.size() > 2 && es.selectedPoint < static_cast<int>(pts.size())) {
            editorChange();
            pts.erase(static_cast<std::size_t>(es.selectedPoint));
            es.selectedPoint = -1;
            editorReparse(false);
        }
    }
}

void App::handleEditorEvent(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_MOUSE_WHEEL:
        camera_.distance = std::clamp(camera_.distance * (1.0f - e.wheel.y * 0.1f), 12.0f, 180.0f);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        const glm::vec2 p = glm::vec2(e.button.x, e.button.y) * pixelDensity_;
        mouse_ = p;
        if (e.button.button == SDL_BUTTON_LEFT)
            editorApply(p, true);
        else if (e.button.button == SDL_BUTTON_RIGHT) {
            editor_->selectedUnit = editor_->selectedBuilding = editor_->selectedPoint = -1;
            editor_->dragging = false;
        }
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
        if (editor_->dragging && (e.motion.state & SDL_BUTTON_LMASK))
            editorApply(mouse_, false);
        else if (const auto g = camera_.screenToGround(mouse_.x, mouse_.y))
            editor_->dragNow = *g;
        if (e.motion.state & SDL_BUTTON_MMASK)
            camera_.target -= glm::vec2(e.motion.xrel, e.motion.yrel) * pixelDensity_ * (camera_.distance / 900.0f);
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button == SDL_BUTTON_LEFT)
            editorRelease(glm::vec2(e.button.x, e.button.y) * pixelDensity_);
        break;
    default:
        break;
    }
}

void App::handleEditorKey(SDL_Keycode key, bool) {
    const bool ctrl = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
    if (key == SDLK_DELETE || key == SDLK_BACKSPACE)
        editorDelete();
    else if (ctrl && key == SDLK_Z)
        editorUndo();
    else if (ctrl && key == SDLK_S)
        editorSave();
    else if (!ctrl && key >= SDLK_1 && key < SDLK_1 + static_cast<int>(EditorTool::Count))
        editor_->tool = static_cast<EditorTool>(key - SDLK_1);
    else if (key == SDLK_ESCAPE)
        editor_->selectedUnit = editor_->selectedBuilding = editor_->selectedPoint = -1;
}

void App::renderEditorOverlay() {
    auto& es = *editor_;
    const float now = static_cast<float>(now_);
    const auto& lanes = es.doc.value("lanes", json::array());
    for (std::size_t li = 0; li < lanes.size(); ++li) {
        const auto& pts = lanes[li].at("points");
        const glm::vec4 c = kLaneColors[li % std::size(kLaneColors)] * (static_cast<int>(li) == es.lane && es.tool == EditorTool::Lanes ? 1.0f : 0.45f);
        for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
            const glm::vec2 a = jvec(pts[i]);
            const glm::vec2 b = jvec(pts[i + 1]);
            const int steps = std::max(1, static_cast<int>(glm::distance(a, b) / 4.0f));
            for (int s = 0; s < steps; ++s) {
                const glm::vec2 p0 = glm::mix(a, b, static_cast<float>(s) / static_cast<float>(steps));
                const glm::vec2 p1 = glm::mix(a, b, static_cast<float>(s + 1) / static_cast<float>(steps));
                const float h0 = mapVisuals_.heightAt(p0.x, p0.y) - mapVisuals_.heightAt((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
                const float h1 = mapVisuals_.heightAt(p1.x, p1.y) - mapVisuals_.heightAt((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
                renderer_.drawBeam({p0.x, 0.4f + h0, p0.y}, {p1.x, 0.4f + h1, p1.y}, 0.14f, c, true);
            }
        }
        for (std::size_t i = 0; i < pts.size(); ++i) {
            const glm::vec2 p = jvec(pts[i]);
            const bool sel = static_cast<int>(li) == es.lane && static_cast<int>(i) == es.selectedPoint;
            renderer_.draw(Mesh::Sphere, {p.x, 0.5f, p.y}, glm::vec3(sel ? 0.9f : 0.55f), 0.0f, c, Unlit);
        }
    }
    if (es.doc.contains("playerStart")) {
        const glm::vec2 p = jvec(es.doc["playerStart"].at("pos"));
        renderer_.drawBeam({p.x, 0.0f, p.y}, {p.x, 4.0f, p.y}, 0.15f, {0.3f, 1.0f, 0.4f, 1.0f});
        renderer_.drawGlow(Mesh::Sphere, {p.x, 4.0f, p.y}, glm::vec3(0.8f), {0.3f, 1.0f, 0.4f, 1.0f});
    }
    if (es.selectedUnit >= 0 && es.selectedUnit < static_cast<int>(es.doc["units"].size())) {
        const glm::vec2 p = jvec(es.doc["units"][static_cast<std::size_t>(es.selectedUnit)].at("pos"));
        const float pulse = 1.0f + 0.1f * std::sin(now * 6.0f);
        renderer_.draw(Mesh::Cylinder, {p.x, 0.05f, p.y}, {4.0f * pulse, 0.06f, 4.0f * pulse}, 0.0f, {1.0f, 0.9f, 0.3f, 0.35f}, Alpha | Unlit);
    }
    if (es.selectedBuilding >= 0 && es.selectedBuilding < static_cast<int>(map_->buildings().size())) {
        const auto& poly = map_->buildings()[static_cast<std::size_t>(es.selectedBuilding)].poly;
        for (std::size_t i = 0; i < poly.size(); ++i) {
            const glm::vec2 a = poly[i];
            const glm::vec2 b = poly[(i + 1) % poly.size()];
            renderer_.drawBeam({a.x, 0.3f, a.y}, {b.x, 0.3f, b.y}, 0.12f, {1.0f, 0.9f, 0.3f, 1.0f}, true);
        }
    }
    const glm::vec2 g = es.dragNow;
    if (es.tool == EditorTool::Terrain) {
        const float d = static_cast<float>(es.brush * 2 + 1);
        const glm::vec4 c = es.brushKind == 1 ? glm::vec4(0.8f, 0.6f, 0.4f, 0.3f) : es.brushKind == 3 ? glm::vec4(0.3f, 0.8f, 0.4f, 0.3f)
                                                                                     : glm::vec4(0.9f, 0.9f, 0.9f, 0.3f);
        renderer_.draw(Mesh::Cylinder, {g.x, 0.1f, g.y}, {d, 0.08f, d}, 0.0f, c, Alpha | Unlit);
    } else if (es.tool == EditorTool::Buildings && es.dragging) {
        const glm::vec2 a = glm::min(es.dragStart, g);
        const glm::vec2 b = glm::max(es.dragStart, g);
        const float h = 0.34f * static_cast<float>(es.levels);
        renderer_.draw(Mesh::Cube, {(a.x + b.x) * 0.5f, h * 0.5f, (a.y + b.y) * 0.5f}, {b.x - a.x, h, b.y - a.y}, 0.0f, {0.6f, 0.8f, 1.0f, 0.35f},
                       Alpha | Unlit);
    } else if (es.tool == EditorTool::Units || es.tool == EditorTool::Points) {
        renderer_.drawGlow(Mesh::Sphere, {g.x, 0.4f, g.y}, glm::vec3(0.6f), {1.0f, 1.0f, 1.0f, 1.0f});
    }
}

void App::editorSelfTest() {
    auto& es = *editor_;
    auto screen = [&](glm::vec2 p) { return camera_.worldToScreen({p.x, 0.0f, p.y}); };
    camera_.update(renderer_.width(), renderer_.height(), renderer_.homogeneousDepth());
    const glm::vec2 c = camera_.target;
    const std::size_t units0 = es.doc["units"].size();
    es.tool = EditorTool::Units;
    es.unitDef = data_->find("gun_tower");
    editorApply(screen(c), true);
    es.tool = EditorTool::Terrain;
    es.brushKind = 1;
    editorApply(screen(c + glm::vec2(6.0f, 0.0f)), true);
    editorApply(screen(c + glm::vec2(8.0f, 0.0f)), false);
    editorRelease(screen(c + glm::vec2(8.0f, 0.0f)));
    const bool rock = map_->cellKind(static_cast<int>(c.x + 6.0f), static_cast<int>(c.y)) == CellKind::Rock;
    const std::size_t buildings0 = es.doc.value("buildings", json::array()).size();
    es.tool = EditorTool::Buildings;
    editorApply(screen(c + glm::vec2(-8.0f, -8.0f)), true);
    editorApply(screen(c + glm::vec2(-4.0f, -5.0f)), false);
    editorRelease(screen(c + glm::vec2(-4.0f, -5.0f)));
    const std::size_t lane0 = es.doc["lanes"][0]["points"].size();
    es.tool = EditorTool::Lanes;
    es.lane = 0;
    editorApply(screen(c + glm::vec2(0.0f, 10.0f)), true);
    editorRelease(screen(c + glm::vec2(0.0f, 10.0f)));
    es.tool = EditorTool::Points;
    editorApply(screen(c + glm::vec2(10.0f, 10.0f)), true);
    editorReparse(true);
    spdlog::info("editor selftest: units {}->{} rock {} buildings {}->{} lane points {}->{} points {} | {}", units0, es.doc["units"].size(), rock,
                 buildings0, es.doc.value("buildings", json::array()).size(), lane0, es.doc["lanes"][0]["points"].size(),
                 map_->resourcePoints().size(), es.status);
    editorUndo();
    spdlog::info("editor selftest: after undo points {}", map_->resourcePoints().size());
    std::snprintf(es.saveId, sizeof(es.saveId), "editor_selftest");
    editorSave();
    const bool saved = std::filesystem::exists(std::filesystem::path(userMapsDir()) / "editor_selftest.json");
    bool loads = false;
    try {
        loads = Map::parse(readMap("editor_selftest")).cellKind(static_cast<int>(c.x + 6.0f), static_cast<int>(c.y)) == CellKind::Rock;
    } catch (const std::exception& e) {
        spdlog::warn("editor selftest load: {}", e.what());
    }
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(userMapsDir()) / "editor_selftest.json", ec);
    loadUserMaps();
    spdlog::info("editor selftest: saved {} reload keeps rock {} | {}", saved, loads, es.status);
}

void App::drawEditor() {
    auto& es = *editor_;
    const float s = uiScale();
    ImGui::SetNextWindowPos({10.0f * s, 10.0f * s}, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints({320.0f * s, 0.0f}, {320.0f * s, ImGui::GetIO().DisplaySize.y - 20.0f * s});
    ImGui::Begin("Map editor", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);
    const float full = ImGui::GetContentRegionAvail().x;

    if (ImGui::CollapsingHeader(ICON_FA_FILE "  File", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(full * 0.6f);
        ImGui::InputText("Name", es.name, sizeof(es.name));
        ImGui::SetNextItemWidth(full * 0.6f);
        ImGui::InputText("Id", es.saveId, sizeof(es.saveId), ImGuiInputTextFlags_CharsNoBlank);
        if (ImGui::Button(ICON_FA_FLOPPY_O "  Save", {full * 0.32f, 0.0f}))
            editorSave();
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_PLAY "  Play", {full * 0.32f, 0.0f}))
            editorPlay();
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_SIGN_OUT "  Exit", {-1.0f, 0.0f})) {
            closeEditor();
            ImGui::End();
            return;
        }
        if (es.maps.empty())
            for (const auto& m : maps_)
                es.maps.push_back(m.id);
        if (!es.maps.empty()) {
            es.openChoice = std::clamp(es.openChoice, 0, static_cast<int>(es.maps.size()) - 1);
            ImGui::SetNextItemWidth(full * 0.6f);
            if (ImGui::BeginCombo("##open", es.maps[static_cast<std::size_t>(es.openChoice)].c_str())) {
                for (int i = 0; i < static_cast<int>(es.maps.size()); ++i)
                    if (ImGui::Selectable(es.maps[static_cast<std::size_t>(i)].c_str(), i == es.openChoice))
                        es.openChoice = i;
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Open", {-1.0f, 0.0f})) {
                const std::string id = es.maps[static_cast<std::size_t>(es.openChoice)];
                openEditor(id);
                ImGui::End();
                return;
            }
        }
        ImGui::SetNextItemWidth(full * 0.25f);
        ImGui::InputInt("##w", &es.newWidth, 0);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(full * 0.25f);
        ImGui::InputInt("##h", &es.newHeight, 0);
        ImGui::SameLine();
        ImGui::Checkbox("Winter", &es.newWinter);
        ImGui::SameLine();
        if (ImGui::Button("New", {-1.0f, 0.0f})) {
            newEditorMap(es.newWidth, es.newHeight, es.newWinter);
            ImGui::End();
            return;
        }
    }

    if (ImGui::CollapsingHeader(ICON_FA_WRENCH "  Tools  [1-6]", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (int i = 0; i < static_cast<int>(EditorTool::Count); ++i) {
            if (i % 2)
                ImGui::SameLine();
            if (menuButton(kToolNames[i], es.tool == static_cast<EditorTool>(i), full * 0.49f, 30.0f * s))
                es.tool = static_cast<EditorTool>(i);
        }
        ImGui::Separator();
        switch (es.tool) {
        case EditorTool::Select:
            ImGui::TextWrapped("Click a unit or building. Drag units to move. Del removes, Ctrl+Z undoes, middle mouse pans.");
            break;
        case EditorTool::Units: {
            const auto& units = data_->units();
            es.unitDef = std::clamp(es.unitDef, 0, static_cast<int>(units.size()) - 1);
            ImGui::SetNextItemWidth(full * 0.6f);
            if (ImGui::BeginCombo("Unit", units[static_cast<std::size_t>(es.unitDef)].id.c_str(), ImGuiComboFlags_HeightLarge)) {
                for (int i = 0; i < static_cast<int>(units.size()); ++i)
                    if (ImGui::Selectable(units[static_cast<std::size_t>(i)].id.c_str(), i == es.unitDef))
                        es.unitDef = i;
                ImGui::EndCombo();
            }
            ImGui::RadioButton("Colony", &es.team, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Enemy", &es.team, 1);
            ImGui::SetNextItemWidth(full * 0.6f);
            ImGui::Combo("Role", &es.role, "none\0core\0portal\0");
            break;
        }
        case EditorTool::Terrain:
            ImGui::SetNextItemWidth(full * 0.6f);
            ImGui::SliderInt("Brush", &es.brush, 0, 10);
            ImGui::RadioButton("Open", &es.brushKind, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Rock", &es.brushKind, 1);
            ImGui::SameLine();
            ImGui::RadioButton("Forest", &es.brushKind, 3);
            ImGui::TextDisabled("Rock is raised and impassable, forest is impassable.");
            break;
        case EditorTool::Buildings:
            ImGui::SetNextItemWidth(full * 0.6f);
            ImGui::SliderInt("Storeys", &es.levels, 1, 25);
            ImGui::SetNextItemWidth(full * 0.6f);
            ImGui::Combo("Kind", &es.buildingKind, "panel\0block\0public\0industrial\0house\0garage\0");
            ImGui::Checkbox("Ruin", &es.ruin);
            ImGui::TextDisabled("Drag a rectangle on the ground.");
            break;
        case EditorTool::Lanes: {
            auto& lanes = es.doc["lanes"];
            for (int i = 0; i < static_cast<int>(lanes.size()); ++i) {
                const std::string name = lanes[static_cast<std::size_t>(i)].value("name", "?");
                if (ImGui::RadioButton(name.c_str(), es.lane == i))
                    es.lane = i;
                ImGui::SameLine();
            }
            ImGui::NewLine();
            ImGui::SetNextItemWidth(full * 0.5f);
            ImGui::InputText("##lane", es.laneName, sizeof(es.laneName), ImGuiInputTextFlags_CharsNoBlank);
            ImGui::SameLine();
            if (ImGui::Button("Add lane", {-1.0f, 0.0f}) && es.laneName[0]) {
                editorChange();
                const auto& core = map_->units();
                glm::vec2 a{8.0f}, b{static_cast<float>(map_->width()) - 8.0f};
                for (const auto& u : core) {
                    if (u.role == "core")
                        a = u.pos;
                    if (u.role == "portal")
                        b = u.pos;
                }
                lanes.push_back({{"name", es.laneName}, {"points", json::array({point(b), point(a)})}});
                es.lane = static_cast<int>(lanes.size()) - 1;
                es.laneName[0] = 0;
                editorReparse(false);
            }
            ImGui::TextWrapped("Click to insert a point before the end, drag points to move, Del removes the selected point.");
            break;
        }
        case EditorTool::Points:
            ImGui::Checkbox("Player start", &es.pointIsStart);
            ImGui::TextDisabled(es.pointIsStart ? "Click to move the player start." : "Click to add a resource point.");
            break;
        default:
            break;
        }
    }

    if (es.selectedUnit >= 0 && es.selectedUnit < static_cast<int>(es.doc["units"].size()) &&
        ImGui::CollapsingHeader(ICON_FA_INFO "  Selected unit", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& u = es.doc["units"][static_cast<std::size_t>(es.selectedUnit)];
        ImGui::Text("%s  at %.1f, %.1f", u.value("unit", "?").c_str(), jvec(u["pos"]).x, jvec(u["pos"]).y);
        int team = u.value("team", 0);
        if (ImGui::RadioButton("Colony##u", team == 0) && team != 0) {
            editorChange();
            u["team"] = 0;
            editorReparse(false);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Enemy##u", team == 1) && team != 1) {
            editorChange();
            u["team"] = 1;
            editorReparse(false);
        }
        char tag[32];
        std::snprintf(tag, sizeof(tag), "%s", u.value("tag", "").c_str());
        ImGui::SetNextItemWidth(full * 0.6f);
        if (ImGui::InputText("Tag", tag, sizeof(tag), ImGuiInputTextFlags_EnterReturnsTrue)) {
            editorChange();
            u["tag"] = std::string(tag);
            editorReparse(false);
        }
    }
    if (es.selectedBuilding >= 0 && es.doc.contains("buildings") && es.selectedBuilding < static_cast<int>(es.doc["buildings"].size()) &&
        ImGui::CollapsingHeader(ICON_FA_BUILDING "  Selected building", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& b = es.doc["buildings"][static_cast<std::size_t>(es.selectedBuilding)];
        int levels = b.value("levels", 5);
        ImGui::SetNextItemWidth(full * 0.6f);
        if (ImGui::SliderInt("Storeys##b", &levels, 1, 25))
            b["levels"] = levels;
        if (ImGui::IsItemActivated())
            editorChange();
        if (ImGui::IsItemDeactivatedAfterEdit())
            editorReparse(true);
        bool ruin = b.value("ruin", false);
        if (ImGui::Checkbox("Ruin##b", &ruin)) {
            editorChange();
            b["ruin"] = ruin;
            editorReparse(true);
        }
        ImGui::TextDisabled("%s, %d corners", b.value("kind", "").c_str(), static_cast<int>(b["poly"].size()));
    }

    if (ImGui::CollapsingHeader(ICON_FA_GLOBE "  Map")) {
        bool winter = es.doc.value("theme", "") == "winter";
        if (ImGui::Checkbox("Winter theme", &winter)) {
            editorChange();
            es.doc["theme"] = winter ? "winter" : "";
            editorReparse(true);
        }
        ImGui::Text("%d x %d cells, %d units, %d buildings", map_->width(), map_->height(), static_cast<int>(map_->units().size()),
                    static_cast<int>(map_->buildings().size()));
        ImGui::Text("%d lanes, %d waves, %d resource points", static_cast<int>(map_->lanes().size()), static_cast<int>(map_->defense().waves.size()),
                    static_cast<int>(map_->resourcePoints().size()));
        if (const auto g = camera_.screenToGround(mouse_.x, mouse_.y))
            ImGui::TextDisabled("cursor %.1f, %.1f  %s", g->x, g->y, kCellNames[static_cast<int>(map_->cellKind(static_cast<int>(g->x), static_cast<int>(g->y)))]);
    }
    if (!es.status.empty()) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(toVec(kAccent), "%s", es.status.c_str());
        ImGui::PopTextWrapPos();
    }
    if (es.changed)
        ImGui::TextDisabled("unsaved changes");
    trackWindow();
    ImGui::End();
}

}
