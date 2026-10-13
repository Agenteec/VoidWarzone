#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "net/Connection.h"
#include "net/PacketSink.h"

struct juice_agent;

namespace vw::net {

struct IceServers {
    std::string stunHost;
    std::uint16_t stunPort = 3478;
    std::string turnHost;
    std::uint16_t turnPort = 3478;
    std::string turnUser;
    std::string turnPass;
};

class IceAgent : public PacketSink {
public:
    enum class State { Gathering, Connecting, Connected, Failed };

    explicit IceAgent(const IceServers& servers);
    ~IceAgent() override;
    IceAgent(const IceAgent&) = delete;
    IceAgent& operator=(const IceAgent&) = delete;

    bool valid() const { return agent_ != nullptr; }
    std::string localDescription() const;
    bool setRemoteDescription(const std::string& sdp);
    void addRemoteCandidate(const std::string& sdp);
    void remoteGatheringDone();
    void gather();

    std::vector<std::string> takeCandidates();
    bool takeGatheringDone();
    State state() const { return state_.load(); }
    std::string selectedPath() const;

    bool send(const Address& to, const void* data, std::size_t size) override;
    bool receive(Bytes& out);

private:
    static void onState(juice_agent* agent, int state, void* user);
    static void onCandidate(juice_agent* agent, const char* sdp, void* user);
    static void onGatheringDone(juice_agent* agent, void* user);
    static void onRecv(juice_agent* agent, const char* data, std::size_t size, void* user);

    IceServers servers_;
    juice_agent* agent_ = nullptr;
    std::atomic<State> state_{State::Gathering};
    mutable std::mutex mutex_;
    std::deque<Bytes> inbox_;
    std::vector<std::string> candidates_;
    bool gatheringDone_ = false;
};

class IceHub : public PacketSink {
public:
    static Address addressOf(std::uint32_t peer) { return {0xf0000000u | (peer & 0x0fffffffu), 1}; }
    static bool isVirtual(const Address& a) { return (a.ip & 0xf0000000u) == 0xf0000000u && a.port == 1; }

    IceAgent& create(std::uint32_t peer, const IceServers& servers);
    IceAgent* find(std::uint32_t peer);
    IceAgent* find(const Address& address);
    void remove(std::uint32_t peer);
    void remove(const Address& address);

    bool send(const Address& to, const void* data, std::size_t size) override;
    void poll(const std::function<void(const Address&, const Bytes&)>& handler);

private:
    std::map<std::uint32_t, std::unique_ptr<IceAgent>> agents_;
};

}
