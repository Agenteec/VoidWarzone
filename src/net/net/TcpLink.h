#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "net/Address.h"
#include "net/Connection.h"

namespace vw::net {

class TcpLink {
public:
    enum class State { Closed, Connecting, Open, Failed };

    TcpLink() = default;
    ~TcpLink();
    TcpLink(const TcpLink&) = delete;
    TcpLink& operator=(const TcpLink&) = delete;

    bool connect(const Address& address);
    void close();
    void update();
    void send(const Bytes& message);
    bool pop(Bytes& message);
    State state() const { return state_; }

private:
    void fail();

    std::uintptr_t handle_ = ~std::uintptr_t{0};
    State state_ = State::Closed;
    Bytes out_;
    Bytes in_;
    std::deque<Bytes> messages_;
};

void appendFrame(Bytes& out, const Bytes& message);
bool extractFrame(Bytes& in, Bytes& message, bool& error);

}
