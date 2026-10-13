#include "core/World.h"

namespace vw {

void World::step() {
    registry_.view<Position, const Velocity>().each([](Position& p, const Velocity& v) {
        p.value += v.value * kTickDt;
    });
    ++tick_;
}

}
