#include "render/Camera.h"

#include <algorithm>
#include <cmath>

#include <bx/math.h>
#include <glm/geometric.hpp>

namespace vw::client {

void Camera::update(int width, int height, bool homogeneousDepth) {
    width_ = std::max(width, 1);
    height_ = std::max(height, 1);

    const float pitch = bx::toRad(pitchDeg);
    eye_ = {target.x, distance * std::sin(pitch), target.y + distance * std::cos(pitch)};
    const glm::vec3 at{target.x, 0.0f, target.y};

    forward_ = glm::normalize(at - eye_);
    const glm::vec3 side = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), forward_));
    up_ = glm::cross(forward_, side);
    right_ = -side;

    // map y grows down the screen: mirror view-space x so +x stays on the right
    bx::mtxLookAt(view_, {eye_.x, eye_.y, eye_.z}, {at.x, at.y, at.z});
    for (const int i : {0, 4, 8, 12})
        view_[i] = -view_[i];
    bx::mtxProj(proj_, fovDeg, static_cast<float>(width_) / static_cast<float>(height_), 0.5f, 500.0f, homogeneousDepth);
    bx::mtxMul(viewProj_, view_, proj_);
}

std::optional<glm::vec2> Camera::screenToGround(float sx, float sy) const {
    const float ndcX = sx / static_cast<float>(width_) * 2.0f - 1.0f;
    const float ndcY = 1.0f - sy / static_cast<float>(height_) * 2.0f;
    const float tanHalf = std::tan(bx::toRad(fovDeg) * 0.5f);
    const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
    const glm::vec3 dir = forward_ + right_ * (ndcX * tanHalf * aspect) + up_ * (ndcY * tanHalf);
    if (dir.y > -1e-4f)
        return std::nullopt;
    const float t = -eye_.y / dir.y;
    const glm::vec3 hit = eye_ + dir * t;
    return glm::vec2{hit.x, hit.z};
}

glm::vec2 Camera::worldToScreen(glm::vec3 p) const {
    const bx::Vec3 ndc = bx::mulH({p.x, p.y, p.z}, viewProj_);
    return {(ndc.x * 0.5f + 0.5f) * static_cast<float>(width_), (0.5f - ndc.y * 0.5f) * static_cast<float>(height_)};
}

}
