#pragma once

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "core/GameData.h"
#include "core/Map.h"
#include "render/Pie.h"
#include "render/Renderer.h"

namespace vw::client {

struct Model {
    MeshId mesh = 0;
    TextureId texture = 0;
    TextureId mask = 0;
    bool glow = false;
    std::vector<glm::vec3> tips;
    glm::vec4 gate{0.0f};
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
    std::vector<glm::vec3> connectors;
    std::vector<TexVertex> vertices;
    std::vector<std::uint32_t> indices;
};

struct ModelPart {
    const Model* model = nullptr;
    glm::vec3 offset{0.0f};
    bool turret = false;
    bool pitch = false;
};

struct UnitVisual {
    Mesh shape = Mesh::Cube;
    float height = 1.0f;
    std::vector<ModelPart> parts;
    float scale = 1.0f;
    float yaw = 0.0f;
    bool turret = false;
    glm::vec3 pivot{0.0f};
    glm::vec3 muzzle{0.0f};
    glm::vec3 lo{0.0f};
    glm::vec3 hi{0.0f};
    std::vector<const Model*> legs;
    std::vector<glm::vec3> hips;
    std::vector<glm::vec3> tips;
    glm::vec4 gate{0.0f};
    bool crystalShot = false;
    bool frostShot = false;
    std::optional<glm::vec3> tint;
};

inline constexpr float kModelYaw = 1.5707963f;
inline constexpr float kMetre = 0.13f;

class ModelLibrary {
public:
    using Loader = std::function<std::string(const std::string&)>;

    ModelLibrary(Renderer& renderer, Loader load) : renderer_(renderer), load_(std::move(load)) {}

    const Model* model(const std::string& name);
    TextureId texture(const std::string& file);
    void loadGlb(const std::string& file);

private:
    Renderer& renderer_;
    Loader load_;
    std::unordered_map<std::string, std::unique_ptr<Model>> models_;
    std::unordered_map<std::string, TextureId> textures_;
};

std::vector<UnitVisual> loadUnitVisuals(const std::string& unitsJson, const GameData& data, ModelLibrary& models);

struct StaticBatch {
    MeshId mesh = 0;
    TextureId texture = 0;
    TextureId mask = 0;
    bool glow = false;
    bool cutaway = false;
};

struct MapChunk {
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};
    MeshId terrain = 0;
    std::vector<StaticBatch> props;
};

struct MapVisuals {
    std::vector<MapChunk> chunks;
    std::array<TextureId, 4> terrainTextures{};
    int columns = 0;
    int rows = 0;
    std::vector<float> heights;
    std::vector<float> roofs;
    const Model* derrick = nullptr;

    float heightAt(float x, float z) const;
};

MapVisuals buildMapVisuals(const Map& map, const GameData& data, Renderer& renderer, ModelLibrary& models);
void makeModelMatrix(float* mtx, glm::vec3 pos, float yaw, float scale, glm::vec3 offset);
void makeTurretMatrix(float* mtx, glm::vec3 pos, float hullYaw, glm::vec3 pivot, float yaw, float pitch, float scale);
void makeLegMatrix(float* mtx, glm::vec3 pos, float hullYaw, float scale, glm::vec3 hip, float swing, float lift);
glm::vec3 modelPoint(glm::vec3 pos, float yaw, float scale, glm::vec3 p);
glm::vec3 turretPoint(glm::vec3 pos, float hullYaw, glm::vec3 pivot, float yaw, float pitch, float scale, glm::vec3 local);

}
