#include <doctest/doctest.h>

#include "core/World.h"
#include "net/NetLibrary.h"

TEST_CASE("world step moves entities") {
    vw::World world;
    auto& reg = world.registry();
    const auto e = reg.create();
    reg.emplace<vw::Position>(e);
    reg.emplace<vw::Velocity>(e, glm::vec2{3.0f, 0.0f});

    for (int i = 0; i < vw::kTickRate; ++i)
        world.step();

    CHECK(world.tick() == vw::kTickRate);
    CHECK(reg.get<vw::Position>(e).value.x == doctest::Approx(3.0f));
}

TEST_CASE("net library init") {
    vw::net::NetLibrary net;
    CHECK(net.ok());
}
