#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <entt/entity/entity.hpp>
#include <glm/vec2.hpp>

namespace vw {

class SpatialHash {
public:
    struct Item {
        entt::entity entity;
        glm::vec2 pos;
        glm::vec2 vel;
        float radius;
        std::uint8_t team;
        bool structure;
        bool air = false;
    };

    void reset(float width, float height, float cellSize) {
        inv_ = 1.0f / cellSize;
        cols_ = std::max(1, static_cast<int>(std::ceil(width / cellSize)));
        rows_ = std::max(1, static_cast<int>(std::ceil(height / cellSize)));
        buckets_.assign(static_cast<std::size_t>(cols_ * rows_), {});
    }

    void clear() {
        for (auto& b : buckets_)
            b.clear();
    }

    void insert(const Item& item) { buckets_[bucket(col(item.pos.x), row(item.pos.y))].push_back(item); }

    template <class F>
    void query(glm::vec2 p, float radius, F&& f) const {
        const int c0 = col(p.x - radius);
        const int c1 = col(p.x + radius);
        const int r0 = row(p.y - radius);
        const int r1 = row(p.y + radius);
        for (int r = r0; r <= r1; ++r)
            for (int c = c0; c <= c1; ++c)
                for (const Item& item : buckets_[bucket(c, r)])
                    f(item);
    }

private:
    int col(float x) const { return std::clamp(static_cast<int>(std::floor(x * inv_)), 0, cols_ - 1); }
    int row(float y) const { return std::clamp(static_cast<int>(std::floor(y * inv_)), 0, rows_ - 1); }
    std::size_t bucket(int c, int r) const { return static_cast<std::size_t>(r * cols_ + c); }

    float inv_ = 0.25f;
    int cols_ = 1;
    int rows_ = 1;
    std::vector<std::vector<Item>> buckets_{1};
};

}
