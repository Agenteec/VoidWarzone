#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace vw::net {

inline constexpr std::uint16_t kIcePortBase = 27470;
inline constexpr std::uint16_t kIcePortCount = 12;

class PortMapper {
public:
    enum class State { Off, Discovering, Ready, Unavailable };

    PortMapper() = default;
    ~PortMapper();
    PortMapper(const PortMapper&) = delete;
    PortMapper& operator=(const PortMapper&) = delete;

    void start(std::vector<std::uint16_t> ports);
    void stop();

    State state() const { return state_.load(); }
    std::string status() const;
    std::string externalIp() const;
    std::optional<std::uint16_t> external(std::uint16_t internalPort) const;
    std::string candidateFor(const std::string& localDescription) const;

private:
    void run(std::vector<std::uint16_t> ports);
    void removeAll();

    std::atomic<State> state_{State::Off};
    std::thread thread_;
    mutable std::mutex mutex_;
    std::string status_;
    std::string externalIp_;
    std::string lanIp_;
    std::string controlUrl_;
    std::string serviceType_;
    std::map<std::uint16_t, std::uint16_t> mapped_;
};

}
