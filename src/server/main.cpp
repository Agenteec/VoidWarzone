#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <string_view>
#include <thread>

#include <spdlog/spdlog.h>

#include "core/Version.h"
#include "core/World.h"
#include "net/NetLibrary.h"
#include "net/Protocol.h"

namespace {
std::atomic_bool g_running{true};
}

int main(int argc, char** argv) {
    std::uint64_t maxTicks = 0;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string_view(argv[i]) == "--ticks")
            maxTicks = std::strtoull(argv[i + 1], nullptr, 10);

    std::signal(SIGINT, [](int) { g_running = false; });
    std::signal(SIGTERM, [](int) { g_running = false; });

    vw::net::NetLibrary net;
    if (!net.ok())
        return 1;

    spdlog::info("Void Warzone server {} | port {} | {} Hz", vw::kVersion, vw::net::kGamePort, vw::kTickRate);

    using Clock = std::chrono::steady_clock;
    constexpr auto tickDuration = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(vw::kTickDt));

    vw::World world;
    auto next = Clock::now();
    while (g_running && (maxTicks == 0 || world.tick() < maxTicks)) {
        world.step();
        next += tickDuration;
        std::this_thread::sleep_until(next);
    }

    spdlog::info("stopped at tick {}", world.tick());
    return 0;
}
