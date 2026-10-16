#pragma once

#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <nlohmann/json.hpp>

#include "core/Map.h"

namespace vw::client {

enum class EditorTool : int { Select, Units, Terrain, Buildings, Lanes, Points, Count };

struct EditorState {
    nlohmann::json doc;
    std::vector<nlohmann::json> undo;
    std::vector<CellKind> cells;
    std::string id;
    EditorTool tool = EditorTool::Select;
    int unitDef = 0;
    int team = 0;
    int role = 0;
    int brush = 2;
    int brushKind = 1;
    int levels = 9;
    int buildingKind = 0;
    bool ruin = false;
    int lane = 0;
    int selectedUnit = -1;
    int selectedBuilding = -1;
    int selectedPoint = -1;
    bool pointIsStart = false;
    bool dragging = false;
    bool changed = false;
    bool dirty = false;
    double dirtyAt = 0.0;
    glm::vec2 dragStart{0.0f};
    glm::vec2 dragNow{0.0f};
    std::string status;
    char name[64] = "";
    char saveId[49] = "";
    char laneName[32] = "";
    int newWidth = 160;
    int newHeight = 120;
    bool newWinter = true;
    std::vector<std::string> maps;
    int openChoice = 0;
};

}
