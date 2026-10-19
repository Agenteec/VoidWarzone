#include "App.h"

#include <algorithm>
#include <cmath>

namespace vw::client {

bool App::fowActive() const {
    const auto* latest = net_.latest();
    return latest && latest->game.fog && map_ && screen_ == Screen::Game;
}

void App::updateFow() {
    if (!fowActive()) {
        if (!fowGrid_.empty()) {
            fowGrid_.clear();
            renderer_.setFogOfWar(0, 0, nullptr);
        }
        return;
    }
    if (now_ < fowNext_ && !fowGrid_.empty())
        return;
    fowNext_ = now_ + 0.12;
    const int w = map_->width();
    const int h = map_->height();
    const auto n = static_cast<std::size_t>(w * h);
    if (explored_.size() != n || fowHash_ != map_->hash()) {
        explored_.assign(n, 0);
        fowHash_ = map_->hash();
    }
    visible_.assign(n, 0);
    const std::uint8_t team = myTeam();
    for (const auto& e : entities_) {
        if (e.kind != net::EntityKind::Unit || e.team != team || (e.flags & net::kStateCarried))
            continue;
        const float r = data_->unit(e.def).vision();
        const int cx = static_cast<int>(std::floor(e.pos.x));
        const int cy = static_cast<int>(std::floor(e.pos.y));
        const int ir = static_cast<int>(std::ceil(r));
        for (int y = std::max(0, cy - ir); y <= std::min(h - 1, cy + ir); ++y) {
            const float dy = static_cast<float>(y) + 0.5f - e.pos.y;
            for (int x = std::max(0, cx - ir); x <= std::min(w - 1, cx + ir); ++x) {
                const float dx = static_cast<float>(x) + 0.5f - e.pos.x;
                if (dx * dx + dy * dy <= r * r)
                    visible_[static_cast<std::size_t>(y * w + x)] = 1;
            }
        }
    }
    fowGrid_.resize(n);
    minimapFowPixels_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        explored_[i] |= visible_[i];
        fowGrid_[i] = visible_[i] ? 255 : explored_[i] ? 105 : 16;
        const std::uint32_t alpha = visible_[i] ? 0u : explored_[i] ? 110u : 235u;
        minimapFowPixels_[i] = (alpha << 24) | 0x0a0806u;
    }
    fowSmooth_.resize(n);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int sum = 0;
            int count = 0;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int sx = std::clamp(x + dx, 0, w - 1);
                    const int sy = std::clamp(y + dy, 0, h - 1);
                    sum += fowGrid_[static_cast<std::size_t>(sy * w + sx)];
                    ++count;
                }
            fowSmooth_[static_cast<std::size_t>(y * w + x)] = static_cast<std::uint8_t>(sum / count);
        }
    renderer_.setFogOfWar(w, h, fowSmooth_.data());
    if (minimapFowSize_ != glm::ivec2(w, h)) {
        minimapFow_ = renderer_.createTextureRgba(w, h, nullptr);
        minimapFowSize_ = {w, h};
    }
    renderer_.updateTextureRgba(minimapFow_, w, h, minimapFowPixels_.data());
}

bool App::fowVisible(glm::vec2 p) const {
    if (fowGrid_.empty() || !map_)
        return true;
    const int x = static_cast<int>(std::floor(p.x));
    const int y = static_cast<int>(std::floor(p.y));
    if (x < 0 || y < 0 || x >= map_->width() || y >= map_->height())
        return false;
    return visible_[static_cast<std::size_t>(y * map_->width() + x)] != 0;
}

}
