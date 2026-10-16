#pragma once

#include <functional>
#include <optional>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace vw::client {

class Camera {
public:
    glm::vec2 target{0.0f, 0.0f};
    float distance = 40.0f;
    float pitchDeg = 55.0f;
    float fovDeg = 45.0f;
    float height = 0.0f;
    std::function<float(float, float)> ground;

    void update(int width, int height, bool homogeneousDepth);

    const float* view() const { return view_; }
    const float* proj() const { return proj_; }
    glm::vec3 eye() const { return eye_; }

    std::optional<glm::vec2> screenToGround(float sx, float sy) const;
    glm::vec2 worldToScreen(glm::vec3 p) const;

private:
    float view_[16]{};
    float proj_[16]{};
    float viewProj_[16]{};
    glm::vec3 eye_{};
    glm::vec3 forward_{};
    glm::vec3 right_{};
    glm::vec3 up_{};
    int width_ = 1;
    int height_ = 1;
};

}
