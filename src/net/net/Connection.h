#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <vector>

#include "net/Address.h"
#include "net/Serialize.h"
#include "net/PacketSink.h"

struct reliable_endpoint_t;

namespace vw::net {

using Bytes = std::vector<std::uint8_t>;

struct ConnectionStats {
    float rtt = 0.0f;
    float packetLoss = 0.0f;
    float sentKbps = 0.0f;
    float receivedKbps = 0.0f;
};

class Connection {
public:
    using UnreliableHandler = std::function<bool(Reader&)>;

    Connection(PacketSink& sink, Address remote, double now, std::uint64_t id);
    ~Connection();
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    void sendReliable(Bytes message);
    void setUnreliable(Bytes payload) { unreliable_ = std::move(payload); }
    std::uint16_t flush();
    void receive(const std::uint8_t* data, int size, double now);
    void update(double now);

    bool popMessage(Bytes& out);
    std::vector<std::uint16_t> takeAckedPackets();
    void setUnreliableHandler(UnreliableHandler handler) { onUnreliable_ = std::move(handler); }

    const Address& address() const { return remote_; }
    double lastReceiveTime() const { return lastReceive_; }
    void touch(double now) { lastReceive_ = now; }
    std::size_t pendingReliable() const { return outgoing_.size(); }
    ConnectionStats stats() const;

private:
    struct Outgoing {
        std::uint16_t id;
        Bytes data;
    };
    struct SentPacket {
        std::uint16_t seq = 0;
        bool valid = false;
        std::vector<std::uint16_t> messages;
    };

    static void transmit(void* context, std::uint64_t id, std::uint16_t seq, std::uint8_t* data, int size);
    static int process(void* context, std::uint64_t id, std::uint16_t seq, std::uint8_t* data, int size);
    bool processPayload(const std::uint8_t* data, int size);
    void deliver(std::uint16_t id, Bytes data);

    PacketSink* sink_;
    Address remote_;
    reliable_endpoint_t* endpoint_ = nullptr;
    double lastReceive_ = 0.0;

    std::uint16_t nextSendId_ = 0;
    std::deque<Outgoing> outgoing_;
    std::array<SentPacket, 256> sent_{};
    Bytes unreliable_;

    std::uint16_t nextReceiveId_ = 0;
    std::map<std::uint16_t, Bytes> pending_;
    std::deque<Bytes> received_;
    std::vector<std::uint16_t> acked_;
    UnreliableHandler onUnreliable_;
};

}
