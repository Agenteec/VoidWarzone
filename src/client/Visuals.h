#pragma once

#include <array>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include "core/GameData.h"
#include "core/Map.h"
#include "render/Renderer.h"

namespace vw::client {

struct Model {
    MeshId mesh = 0;
    TextureId texture = 0;
    TextureId mask = 0;
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
    std::vector<glm::vec3> connectors;
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
};

inline constexpr float kModelYaw = 1.5707963f;

struct Placement {
    const Model* model = nullptr;
    float mtx[16];
};

class ModelLibrary {
public:
    using Loader = std::function<std::string(const std::string&)>;

    ModelLibrary(Renderer& renderer, Loader load) : renderer_(renderer), load_(std::move(load)) {}

    const Model* model(const std::string& name);
    TextureId texture(const std::string& file);

private:
    Renderer& renderer_;
    Loader load_;
    std::unordered_map<std::string, std::unique_ptr<Model>> models_;
    std::unordered_map<std::string, TextureId> textures_;
};

std::vector<UnitVisual> loadUnitVisuals(const std::string& unitsJson, const GameData& data, ModelLibrary& models);

struct MapVisuals {
    MeshId terrain = 0;
    std::array<TextureId, 4> terrainTextures{};
    int columns = 0;
    int rows = 0;
    std::vector<float> heights;
    const Model* derrick = nullptr;
    std::vector<Placement> props;

    float heightAt(float x, float z) const;
};

MapVisuals buildMapVisuals(const Map& map, const GameData& data, Renderer& renderer, ModelLibrary& models);
void makeModelMatrix(float* mtx, glm::vec3 pos, float yaw, float scale, glm::vec3 offset);
void makeTurretMatrix(float* mtx, glm::vec3 pos, float hullYaw, glm::vec3 pivot, float yaw, float pitch, float scale);
glm::vec3 turretPoint(glm::vec3 pos, float hullYaw, glm::vec3 pivot, float yaw, float pitch, float scale, glm::vec3 local);

}
