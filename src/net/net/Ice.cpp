#include "net/Ice.h"

#include <utility>

#include <juice/juice.h>

namespace vw::net {

namespace {

constexpr std::size_t kMaxInbox = 1024;

IceAgent* self(void* user) {
    return static_cast<IceAgent*>(user);
}

}

IceAgent::IceAgent(const IceServers& servers) : servers_(servers) {
    juice_config_t config{};
    config.concurrency_mode = JUICE_CONCURRENCY_MODE_POLL;
    if (!servers_.stunHost.empty()) {
        config.stun_server_host = servers_.stunHost.c_str();
        config.stun_server_port = servers_.stunPort;
    }
    juice_turn_server_t turn{};
    if (!servers_.turnHost.empty()) {
        turn.host = servers_.turnHost.c_str();
        turn.port = servers_.turnPort;
        turn.username = servers_.turnUser.c_str();
        turn.password = servers_.turnPass.c_str();
        config.turn_servers = &turn;
        config.turn_servers_count = 1;
    }
    config.cb_state_changed = [](juice_agent_t* a, juice_state_t s, void* u) { onState(a, static_cast<int>(s), u); };
    config.cb_candidate = [](juice_agent_t* a, const char* sdp, void* u) { onCandidate(a, sdp, u); };
    config.cb_gathering_done = [](juice_agent_t* a, void* u) { onGatheringDone(a, u); };
    config.cb_recv = [](juice_agent_t* a, const char* d, std::size_t n, void* u) { onRecv(a, d, n, u); };
    config.user_ptr = this;
    agent_ = juice_create(&config);
    if (!agent_)
        state_ = State::Failed;
}

IceAgent::~IceAgent() {
    if (agent_)
        juice_destroy(agent_);
}

std::string IceAgent::localDescription() const {
    if (!agent_)
        return {};
    char buf[JUICE_MAX_SDP_STRING_LEN];
    if (juice_get_local_description(agent_, buf, sizeof(buf)) != JUICE_ERR_SUCCESS)
        return {};
    return buf;
}

bool IceAgent::setRemoteDescription(const std::string& sdp) {
    return agent_ && juice_set_remote_description(agent_, sdp.c_str()) == JUICE_ERR_SUCCESS;
}

void IceAgent::addRemoteCandidate(const std::string& sdp) {
    if (agent_)
        juice_add_remote_candidate(agent_, sdp.c_str());
}

void IceAgent::remoteGatheringDone() {
    if (agent_)
        juice_set_remote_gathering_done(agent_);
}

void IceAgent::gather() {
    if (agent_)
        juice_gather_candidates(agent_);
}

std::vector<std::string> IceAgent::takeCandidates() {
    std::lock_guard lock(mutex_);
    return std::exchange(candidates_, {});
}

bool IceAgent::takeGatheringDone() {
    std::lock_guard lock(mutex_);
    return std::exchange(gatheringDone_, false);
}

std::string IceAgent::selectedPath() const {
    if (!agent_)
        return {};
    char local[JUICE_MAX_CANDIDATE_SDP_STRING_LEN];
    char remote[JUICE_MAX_CANDIDATE_SDP_STRING_LEN];
    if (juice_get_selected_candidates(agent_, local, sizeof(local), remote, sizeof(remote)) != JUICE_ERR_SUCCESS)
        return {};
    const std::string l = local;
    const std::string r = remote;
    if (l.find("typ relay") != std::string::npos || r.find("typ relay") != std::string::npos)
        return "relay";
    if (l.find("typ srflx") != std::string::npos || r.find("typ srflx") != std::string::npos)
        return "p2p (nat)";
    return "p2p (direct)";
}

bool IceAgent::send(const Address&, const void* data, std::size_t size) {
    if (!agent_ || state_.load() != State::Connected)
        return false;
    return juice_send(agent_, static_cast<const char*>(data), size) == JUICE_ERR_SUCCESS;
}

bool IceAgent::receive(Bytes& out) {
    std::lock_guard lock(mutex_);
    if (inbox_.empty())
        return false;
    out = std::move(inbox_.front());
    inbox_.pop_front();
    return true;
}

void IceAgent::onState(juice_agent*, int state, void* user) {
    switch (static_cast<juice_state_t>(state)) {
    case JUICE_STATE_CONNECTED:
    case JUICE_STATE_COMPLETED:
        self(user)->state_ = State::Connected;
        break;
    case JUICE_STATE_FAILED:
        self(user)->state_ = State::Failed;
        break;
    case JUICE_STATE_CONNECTING:
        self(user)->state_ = State::Connecting;
        break;
    default:
        break;
    }
}

void IceAgent::onCandidate(juice_agent*, const char* sdp, void* user) {
    std::lock_guard lock(self(user)->mutex_);
    self(user)->candidates_.emplace_back(sdp);
}

void IceAgent::onGatheringDone(juice_agent*, void* user) {
    std::lock_guard lock(self(user)->mutex_);
    self(user)->gatheringDone_ = true;
}

void IceAgent::onRecv(juice_agent*, const char* data, std::size_t size, void* user) {
    std::lock_guard lock(self(user)->mutex_);
    if (self(user)->inbox_.size() < kMaxInbox)
        self(user)->inbox_.emplace_back(reinterpret_cast<const std::uint8_t*>(data), reinterpret_cast<const std::uint8_t*>(data) + size);
}

IceAgent& IceHub::create(std::uint32_t peer, const IceServers& servers) {
    auto& slot = agents_[peer & 0x0fffffffu];
    slot = std::make_unique<IceAgent>(servers);
    return *slot;
}

IceAgent* IceHub::find(std::uint32_t peer) {
    const auto it = agents_.find(peer & 0x0fffffffu);
    return it != agents_.end() ? it->second.get() : nullptr;
}

IceAgent* IceHub::find(const Address& address) {
    return isVirtual(address) ? find(address.ip) : nullptr;
}

void IceHub::remove(std::uint32_t peer) {
    agents_.erase(peer & 0x0fffffffu);
}

void IceHub::remove(const Address& address) {
    if (isVirtual(address))
        remove(address.ip);
}

bool IceHub::send(const Address& to, const void* data, std::size_t size) {
    IceAgent* agent = find(to);
    return agent && agent->send(to, data, size);
}

void IceHub::poll(const std::function<void(const Address&, const Bytes&)>& handler) {
    std::vector<std::pair<Address, Bytes>> packets;
    Bytes packet;
    for (auto& [peer, agent] : agents_)
        while (agent->receive(packet))
            packets.emplace_back(addressOf(peer), std::move(packet));
    for (const auto& [address, data] : packets)
        handler(address, data);
}

}
