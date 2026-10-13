#include "net/Connection.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

#include <reliable.h>

#include "net/Protocol.h"

namespace vw::net {

namespace {

constexpr std::size_t kReliableBudget = 900;
constexpr int kReceiveWindow = 1024;

bool seqLess(std::uint16_t a, std::uint16_t b) {
    return static_cast<std::int16_t>(a - b) < 0;
}

std::size_t varuSize(std::uint64_t v) {
    std::size_t n = 1;
    while (v >= 0x80) {
        v >>= 7;
        ++n;
    }
    return n;
}

}

Connection::Connection(PacketSink& sink, Address remote, double now, std::uint64_t id) : sink_(&sink), remote_(remote), lastReceive_(now) {
    reliable_config_t config;
    reliable_default_config(&config);
    std::snprintf(config.name, sizeof(config.name), "%s", remote.toString().c_str());
    config.context = this;
    config.id = id;
    config.max_packet_size = 32 * 1024;
    config.fragment_above = 1100;
    config.fragment_size = 1024;
    config.max_fragments = 32;
    config.rtt_smoothing_factor = 0.05f;
    config.transmit_packet_function = &Connection::transmit;
    config.process_packet_function = &Connection::process;
    endpoint_ = reliable_endpoint_create(&config, now);
}

Connection::~Connection() {
    if (endpoint_)
        reliable_endpoint_destroy(endpoint_);
}

void Connection::transmit(void* context, std::uint64_t, std::uint16_t, std::uint8_t* data, int size) {
    auto* self = static_cast<Connection*>(context);
    std::uint8_t buf[2048];
    if (size + 1 > static_cast<int>(sizeof(buf)))
        return;
    buf[0] = static_cast<std::uint8_t>(PacketType::Payload);
    std::memcpy(buf + 1, data, static_cast<std::size_t>(size));
    self->sink_->send(self->remote_, buf, static_cast<std::size_t>(size) + 1);
}

int Connection::process(void* context, std::uint64_t, std::uint16_t, std::uint8_t* data, int size) {
    return static_cast<Connection*>(context)->processPayload(data, size) ? 1 : 0;
}

void Connection::sendReliable(Bytes message) {
    outgoing_.push_back({nextSendId_++, std::move(message)});
}

std::uint16_t Connection::flush() {
    std::vector<const Outgoing*> included;
    std::size_t used = 0;
    for (const auto& m : outgoing_) {
        const std::size_t sz = 2 + varuSize(m.data.size()) + m.data.size();
        if (used + sz > kReliableBudget && !included.empty())
            break;
        included.push_back(&m);
        used += sz;
    }

    Writer w;
    w.varu(included.size());
    for (const auto* m : included) {
        w.u16(m->id);
        w.varu(m->data.size());
        w.bytes(m->data.data(), m->data.size());
    }
    w.bytes(unreliable_.data(), unreliable_.size());
    unreliable_.clear();

    const std::uint16_t seq = reliable_endpoint_next_packet_sequence(endpoint_);
    auto& sent = sent_[seq % sent_.size()];
    sent.seq = seq;
    sent.valid = true;
    sent.messages.clear();
    for (const auto* m : included)
        sent.messages.push_back(m->id);

    reliable_endpoint_send_packet(endpoint_, w.data().data(), static_cast<int>(w.size()));
    return seq;
}

void Connection::receive(const std::uint8_t* data, int size, double now) {
    lastReceive_ = now;
    reliable_endpoint_receive_packet(endpoint_, const_cast<std::uint8_t*>(data), size);

    int count = 0;
    const std::uint16_t* acks = reliable_endpoint_get_acks(endpoint_, &count);
    for (int i = 0; i < count; ++i) {
        auto& sent = sent_[acks[i] % sent_.size()];
        if (sent.valid && sent.seq == acks[i]) {
            for (const auto id : sent.messages)
                std::erase_if(outgoing_, [id](const Outgoing& m) { return m.id == id; });
            sent.valid = false;
        }
        acked_.push_back(acks[i]);
    }
    reliable_endpoint_clear_acks(endpoint_);
}

bool Connection::processPayload(const std::uint8_t* data, int size) {
    Reader r(data, static_cast<std::size_t>(size));
    const auto count = r.varu();
    if (count > 256)
        return false;
    std::vector<std::pair<std::uint16_t, Bytes>> messages;
    for (std::uint64_t i = 0; i < count && r.ok(); ++i) {
        const std::uint16_t id = r.u16();
        const auto len = r.varu();
        if (len > static_cast<std::uint64_t>(r.remaining()))
            return false;
        Bytes msg(r.current(), r.current() + len);
        r.skip(static_cast<std::size_t>(len));
        messages.emplace_back(id, std::move(msg));
    }
    if (!r.ok())
        return false;

    if (r.remaining() > 0 && onUnreliable_) {
        Reader ur(r.current(), r.remaining());
        if (!onUnreliable_(ur))
            return false;
    }

    for (auto& [id, msg] : messages)
        deliver(id, std::move(msg));
    return true;
}

void Connection::deliver(std::uint16_t id, Bytes data) {
    if (seqLess(id, nextReceiveId_) || static_cast<std::uint16_t>(id - nextReceiveId_) > kReceiveWindow)
        return;
    pending_.try_emplace(id, std::move(data));
    for (auto it = pending_.find(nextReceiveId_); it != pending_.end(); it = pending_.find(nextReceiveId_)) {
        received_.push_back(std::move(it->second));
        pending_.erase(it);
        ++nextReceiveId_;
    }
}

void Connection::update(double now) {
    reliable_endpoint_update(endpoint_, now);
}

bool Connection::popMessage(Bytes& out) {
    if (received_.empty())
        return false;
    out = std::move(received_.front());
    received_.pop_front();
    return true;
}

std::vector<std::uint16_t> Connection::takeAckedPackets() {
    return std::exchange(acked_, {});
}

ConnectionStats Connection::stats() const {
    ConnectionStats s;
    s.rtt = reliable_endpoint_rtt_avg(endpoint_);
    s.packetLoss = reliable_endpoint_packet_loss(endpoint_);
    float acked = 0.0f;
    reliable_endpoint_bandwidth(endpoint_, &s.sentKbps, &s.receivedKbps, &acked);
    return s;
}

}
