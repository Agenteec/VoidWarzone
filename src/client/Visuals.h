#pragma once

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
};

struct UnitVisual {
    Mesh shape = Mesh::Cube;
    float height = 1.0f;
    std::vector<ModelPart> parts;
    float scale = 1.0f;
    float yaw = 0.0f;
};

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
    MeshId ground = 0;
    MeshId lanes = 0;
    MeshId walls = 0;
    TextureId groundTexture = 0;
    TextureId laneTexture = 0;
    TextureId wallTexture = 0;
    const Model* derrick = nullptr;
    std::vector<Placement> props;
};

MapVisuals buildMapVisuals(const Map& map, Renderer& renderer, ModelLibrary& models);
void makeModelMatrix(float* mtx, glm::vec3 pos, float yaw, float scale, glm::vec3 offset);

}
