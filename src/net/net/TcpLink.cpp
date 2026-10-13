#include "net/TcpLink.h"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using SocketHandle = SOCKET;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
#endif

namespace vw::net {

namespace {

constexpr std::uintptr_t kInvalid = ~std::uintptr_t{0};
constexpr std::size_t kMaxFrame = 32 * 1024;

SocketHandle native(std::uintptr_t h) {
    return static_cast<SocketHandle>(h);
}

bool wouldBlock() {
#if defined(_WIN32)
    const int err = WSAGetLastError();
    return err == WSAEWOULDBLOCK || err == WSAEINPROGRESS;
#else
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINPROGRESS;
#endif
}

}

void appendFrame(Bytes& out, const Bytes& message) {
    const auto size = static_cast<std::uint32_t>(message.size());
    out.push_back(static_cast<std::uint8_t>(size));
    out.push_back(static_cast<std::uint8_t>(size >> 8));
    out.push_back(static_cast<std::uint8_t>(size >> 16));
    out.insert(out.end(), message.begin(), message.end());
}

bool extractFrame(Bytes& in, Bytes& message, bool& error) {
    error = false;
    if (in.size() < 3)
        return false;
    const std::size_t size = in[0] | (in[1] << 8) | (in[2] << 16);
    if (size > kMaxFrame) {
        error = true;
        return false;
    }
    if (in.size() < 3 + size)
        return false;
    message.assign(in.begin() + 3, in.begin() + 3 + static_cast<std::ptrdiff_t>(size));
    in.erase(in.begin(), in.begin() + 3 + static_cast<std::ptrdiff_t>(size));
    return true;
}

TcpLink::~TcpLink() {
    close();
}

bool TcpLink::connect(const Address& address) {
    close();
    const SocketHandle s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#if defined(_WIN32)
    if (s == INVALID_SOCKET)
        return false;
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
#else
    if (s < 0)
        return false;
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    handle_ = static_cast<std::uintptr_t>(s);
    const int one = 1;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(address.ip);
    addr.sin_port = htons(address.port);
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 && !wouldBlock()) {
        fail();
        return false;
    }
    state_ = State::Connecting;
    return true;
}

void TcpLink::close() {
    if (handle_ != kInvalid) {
#if defined(_WIN32)
        closesocket(native(handle_));
#else
        ::close(native(handle_));
#endif
        handle_ = kInvalid;
    }
    if (state_ != State::Failed)
        state_ = State::Closed;
    out_.clear();
    in_.clear();
}

void TcpLink::fail() {
    close();
    state_ = State::Failed;
}

void TcpLink::send(const Bytes& message) {
    appendFrame(out_, message);
}

bool TcpLink::pop(Bytes& message) {
    if (messages_.empty())
        return false;
    message = std::move(messages_.front());
    messages_.pop_front();
    return true;
}

void TcpLink::update() {
    if (handle_ == kInvalid)
        return;
    const SocketHandle s = native(handle_);

    if (state_ == State::Connecting) {
        fd_set writable;
        fd_set failed;
        FD_ZERO(&writable);
        FD_ZERO(&failed);
        FD_SET(s, &writable);
        FD_SET(s, &failed);
        timeval tv{};
        if (::select(static_cast<int>(s) + 1, nullptr, &writable, &failed, &tv) <= 0)
            return;
        int err = 0;
#if defined(_WIN32)
        int len = sizeof(err);
#else
        socklen_t len = sizeof(err);
#endif
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len);
        if (FD_ISSET(s, &failed) || err != 0) {
            fail();
            return;
        }
        state_ = State::Open;
    }

    while (!out_.empty()) {
        const auto n = ::send(s, reinterpret_cast<const char*>(out_.data()), static_cast<int>(out_.size()), 0);
        if (n > 0) {
            out_.erase(out_.begin(), out_.begin() + n);
            continue;
        }
        if (n < 0 && wouldBlock())
            break;
        fail();
        return;
    }

    char buf[4096];
    bool closed = false;
    for (;;) {
        const auto n = ::recv(s, buf, sizeof(buf), 0);
        if (n > 0) {
            in_.insert(in_.end(), buf, buf + n);
            continue;
        }
        closed = !(n < 0 && wouldBlock());
        break;
    }

    Bytes message;
    bool error = false;
    while (extractFrame(in_, message, error))
        messages_.push_back(std::move(message));
    if (error || closed)
        fail();
}

}
