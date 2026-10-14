#include "net/PortMapper.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include <miniupnpc.h>
#include <upnpcommands.h>
#include <upnperrors.h>
#include <spdlog/spdlog.h>

namespace vw::net {

PortMapper::~PortMapper() {
    stop();
}

void PortMapper::start(std::vector<std::uint16_t> ports) {
    if (thread_.joinable())
        return;
    state_ = State::Discovering;
    {
        std::lock_guard lock(mutex_);
        status_ = "searching for a UPnP router...";
    }
    thread_ = std::thread([this, ports = std::move(ports)]() mutable { run(std::move(ports)); });
}

void PortMapper::stop() {
    if (thread_.joinable())
        thread_.join();
    removeAll();
    state_ = State::Off;
}

std::string PortMapper::status() const {
    std::lock_guard lock(mutex_);
    return status_;
}

std::string PortMapper::externalIp() const {
    std::lock_guard lock(mutex_);
    return externalIp_;
}

std::optional<std::uint16_t> PortMapper::external(std::uint16_t internalPort) const {
    if (state_ != State::Ready)
        return std::nullopt;
    std::lock_guard lock(mutex_);
    const auto it = mapped_.find(internalPort);
    if (it == mapped_.end())
        return std::nullopt;
    return it->second;
}

std::string PortMapper::candidateFor(const std::string& localDescription) const {
    if (state_ != State::Ready)
        return {};
    std::istringstream in(localDescription);
    std::string line;
    while (std::getline(in, line)) {
        if (line.find("a=candidate:") != 0 || line.find(" typ host") == std::string::npos)
            continue;
        std::istringstream fields(line);
        std::string foundation, component, transport, priority, ip, port;
        fields >> foundation >> component >> transport >> priority >> ip >> port;
        if (transport != "UDP" && transport != "udp")
            continue;
        const int local = std::atoi(port.c_str());
        if (local <= 0 || local > 65535)
            continue;
        const auto ext = external(static_cast<std::uint16_t>(local));
        if (!ext)
            continue;
        char buf[256];
        std::snprintf(buf, sizeof(buf), "a=candidate:9 1 UDP 1694498815 %s %u typ srflx raddr %s rport %d", externalIp().c_str(),
                      static_cast<unsigned>(*ext), ip.c_str(), local);
        return buf;
    }
    return {};
}

void PortMapper::run(std::vector<std::uint16_t> ports) {
    int error = 0;
    UPNPDev* devices = upnpDiscover(2000, nullptr, nullptr, 0, 0, 2, &error);
    if (!devices) {
        std::lock_guard lock(mutex_);
        status_ = "no UPnP router found";
        state_ = State::Unavailable;
        return;
    }
    UPNPUrls urls{};
    IGDdatas data{};
    char lan[64] = {};
    char wan[64] = {};
    const int igd = UPNP_GetValidIGD(devices, &urls, &data, lan, sizeof(lan), wan, sizeof(wan));
    freeUPNPDevlist(devices);
    if (igd != UPNP_CONNECTED_IGD) {
        std::lock_guard lock(mutex_);
        status_ = igd == UPNP_PRIVATEIP_IGD      ? std::string("router has a private WAN address ") + wan + " (double NAT / CGNAT)"
                  : igd == UPNP_DISCONNECTED_IGD ? "router is not connected"
                                                 : "no usable UPnP router";
        state_ = State::Unavailable;
        if (igd != UPNP_NO_IGD)
            FreeUPNPUrls(&urls);
        return;
    }

    char external[64] = {};
    if (UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, external) != UPNPCOMMAND_SUCCESS || !*external)
        std::snprintf(external, sizeof(external), "%s", wan);

    std::map<std::uint16_t, std::uint16_t> mapped;
    std::string lastError;
    for (const auto port : ports) {
        const std::string p = std::to_string(port);
        int r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, p.c_str(), p.c_str(), lan, "Void Warzone", "UDP", nullptr, "7200");
        if (r == 725)
            r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, p.c_str(), p.c_str(), lan, "Void Warzone", "UDP", nullptr, "0");
        if (r == UPNPCOMMAND_SUCCESS) {
            mapped[port] = port;
            continue;
        }
        char reserved[16] = {};
        if (UPNP_AddAnyPortMapping(urls.controlURL, data.first.servicetype, p.c_str(), p.c_str(), lan, "Void Warzone", "UDP", nullptr, "7200",
                                   reserved) == UPNPCOMMAND_SUCCESS &&
            std::atoi(reserved) > 0) {
            mapped[port] = static_cast<std::uint16_t>(std::atoi(reserved));
            continue;
        }
        lastError = strupnperror(r) ? strupnperror(r) : std::to_string(r);
    }

    std::lock_guard lock(mutex_);
    controlUrl_ = urls.controlURL ? urls.controlURL : "";
    serviceType_ = data.first.servicetype;
    FreeUPNPUrls(&urls);
    lanIp_ = lan;
    externalIp_ = external;
    mapped_ = std::move(mapped);
    if (mapped_.empty()) {
        status_ = "router refused port mapping: " + lastError;
        state_ = State::Unavailable;
    } else {
        status_ = "open on " + externalIp_ + " (" + std::to_string(mapped_.size()) + " ports)";
        state_ = State::Ready;
    }
    spdlog::info("upnp: {} | lan {}", status_, lanIp_);
}

void PortMapper::removeAll() {
    std::lock_guard lock(mutex_);
    for (const auto& [internal, ext] : mapped_) {
        const std::string p = std::to_string(ext);
        UPNP_DeletePortMapping(controlUrl_.c_str(), serviceType_.c_str(), p.c_str(), "UDP", nullptr);
    }
    mapped_.clear();
}

}
