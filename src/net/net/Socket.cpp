#include "net/Socket.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mstcpip.h>
#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif
using SocketHandle = SOCKET;
using SockLen = int;
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using SocketHandle = int;
using SockLen = socklen_t;
#endif

namespace vw::net {

namespace {

constexpr std::uintptr_t kInvalid = ~std::uintptr_t{0};

SocketHandle native(std::uintptr_t h) {
    return static_cast<SocketHandle>(h);
}

void closeNative(std::uintptr_t h) {
#if defined(_WIN32)
    closesocket(native(h));
#else
    ::close(native(h));
#endif
}

}

std::optional<Address> Address::parse(std::string_view text, std::uint16_t defaultPort) {
    std::string host(text);
    std::uint16_t port = defaultPort;
    if (const auto colon = host.rfind(':'); colon != std::string::npos) {
        const int p = std::atoi(host.c_str() + colon + 1);
        if (p <= 0 || p > 65535)
            return std::nullopt;
        port = static_cast<std::uint16_t>(p);
        host.resize(colon);
    }
    if (host.empty())
        return std::nullopt;

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result)
        return std::nullopt;
    const auto* sin = reinterpret_cast<const sockaddr_in*>(result->ai_addr);
    Address a{ntohl(sin->sin_addr.s_addr), port};
    freeaddrinfo(result);
    return a;
}

std::string Address::toString() const {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u:%u", (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff, port);
    return buf;
}

UdpSocket::~UdpSocket() {
    close();
}

bool UdpSocket::isOpen() const {
    return handle_ != kInvalid;
}

bool UdpSocket::open(std::uint16_t port, bool broadcast) {
    close();
    const SocketHandle s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#if defined(_WIN32)
    if (s == INVALID_SOCKET)
        return false;
#else
    if (s < 0)
        return false;
#endif
    handle_ = static_cast<std::uintptr_t>(s);

    const int one = 1;
    const int bufSize = 1 << 20;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));
    setsockopt(s, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<const char*>(&bufSize), sizeof(bufSize));
    if (broadcast)
        setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&one), sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close();
        return false;
    }

    sockaddr_in bound{};
    SockLen len = sizeof(bound);
    getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len);
    port_ = ntohs(bound.sin_port);

#if defined(_WIN32)
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    BOOL noReset = FALSE;
    DWORD bytes = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &noReset, sizeof(noReset), nullptr, 0, &bytes, nullptr, nullptr);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    return true;
}

void UdpSocket::close() {
    if (handle_ != kInvalid) {
        closeNative(handle_);
        handle_ = kInvalid;
        port_ = 0;
    }
}

bool UdpSocket::send(const Address& to, const void* data, std::size_t size) {
    if (!isOpen())
        return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(to.ip);
    addr.sin_port = htons(to.port);
    const auto sent = ::sendto(native(handle_), static_cast<const char*>(data), static_cast<int>(size), 0,
                               reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    return sent == static_cast<decltype(sent)>(size);
}

int UdpSocket::receive(Address& from, void* data, std::size_t capacity) {
    if (!isOpen())
        return -1;
    for (;;) {
        sockaddr_in addr{};
        SockLen len = sizeof(addr);
        const auto n = ::recvfrom(native(handle_), static_cast<char*>(data), static_cast<int>(capacity), 0,
                                  reinterpret_cast<sockaddr*>(&addr), &len);
        if (n >= 0) {
            from = {ntohl(addr.sin_addr.s_addr), ntohs(addr.sin_port)};
            return static_cast<int>(n);
        }
#if defined(_WIN32)
        const int err = WSAGetLastError();
        if (err == WSAECONNRESET || err == WSAEMSGSIZE)
            continue;
#else
        if (errno == EINTR)
            continue;
#endif
        return -1;
    }
}

}
