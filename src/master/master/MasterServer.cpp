#include "master/MasterServer.h"

#include <deque>

#include <spdlog/spdlog.h>

#include "net/TcpLink.h"

namespace vw::master {

using asio::ip::tcp;

namespace {

constexpr std::size_t kMaxFrame = 32 * 1024;
constexpr std::size_t kMaxSessions = 4096;

}

class MasterServer::Session : public std::enable_shared_from_this<Session> {
public:
    Session(MasterServer& owner, tcp::socket socket, std::uint32_t id) : owner_(owner), socket_(std::move(socket)), id_(id) {}

    std::string remote() const {
        std::error_code ec;
        const auto ep = socket_.remote_endpoint(ec);
        return ec ? std::string{} : ep.address().to_string() + ":" + std::to_string(ep.port());
    }

    void start() { readHeader(); }

    void send(const net::Bytes& message) {
        const bool idle = queue_.empty();
        net::Bytes frame;
        net::appendFrame(frame, message);
        queue_.push_back(std::move(frame));
        if (idle)
            write();
    }

    void close() {
        if (closed_)
            return;
        closed_ = true;
        std::error_code ec;
        socket_.shutdown(tcp::socket::shutdown_both, ec);
        socket_.close(ec);
    }

private:
    void readHeader() {
        auto self = shared_from_this();
        asio::async_read(socket_, asio::buffer(header_), [this, self](std::error_code ec, std::size_t) {
            if (ec)
                return finish();
            const std::size_t size = header_[0] | (header_[1] << 8) | (header_[2] << 16);
            if (size == 0 || size > kMaxFrame)
                return finish();
            body_.resize(size);
            readBody();
        });
    }

    void readBody() {
        auto self = shared_from_this();
        asio::async_read(socket_, asio::buffer(body_), [this, self](std::error_code ec, std::size_t) {
            if (ec)
                return finish();
            owner_.lobby_.onMessage(id_, body_);
            if (closed_)
                return finish();
            readHeader();
        });
    }

    void write() {
        auto self = shared_from_this();
        asio::async_write(socket_, asio::buffer(queue_.front()), [this, self](std::error_code ec, std::size_t) {
            if (ec)
                return finish();
            queue_.pop_front();
            if (!queue_.empty())
                write();
        });
    }

    void finish() {
        if (finished_)
            return;
        finished_ = true;
        close();
        owner_.lobby_.onDisconnect(id_);
        owner_.sessions_.erase(id_);
    }

    MasterServer& owner_;
    tcp::socket socket_;
    std::uint32_t id_;
    std::uint8_t header_[3]{};
    net::Bytes body_;
    std::deque<net::Bytes> queue_;
    bool closed_ = false;
    bool finished_ = false;
};

MasterServer::MasterServer(asio::io_context& io, std::uint16_t port, net::IceServers servers)
    : acceptor_(io, tcp::endpoint(tcp::v4(), port)),
      lobby_(
          std::move(servers), [this](std::uint32_t id, const net::Bytes& m) { send(id, m); }, [this](std::uint32_t id) { close(id); }) {
    port_ = acceptor_.local_endpoint().port();
    accept();
}

MasterServer::~MasterServer() {
    stop();
}

void MasterServer::stop() {
    std::error_code ec;
    acceptor_.close(ec);
    for (auto& [id, session] : sessions_)
        session->close();
}

void MasterServer::accept() {
    acceptor_.async_accept([this](std::error_code ec, tcp::socket socket) {
        if (!acceptor_.is_open())
            return;
        if (!ec && sessions_.size() < kMaxSessions) {
            socket.set_option(tcp::no_delay(true), ec);
            const std::uint32_t id = nextId_++;
            auto session = std::make_shared<Session>(*this, std::move(socket), id);
            sessions_[id] = session;
            std::string address;
            if (const auto ep = session->remote(); !ep.empty())
                address = ep;
            lobby_.onConnect(id, address);
            session->start();
        }
        accept();
    });
}

void MasterServer::send(std::uint32_t id, const net::Bytes& message) {
    if (const auto it = sessions_.find(id); it != sessions_.end())
        it->second->send(message);
}

void MasterServer::close(std::uint32_t id) {
    if (const auto it = sessions_.find(id); it != sessions_.end())
        it->second->close();
}

}
