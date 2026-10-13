#include "net/NetLibrary.h"

#if defined(_WIN32)
#include <winsock2.h>
#endif

#include <juice/juice.h>
#include <reliable.h>
#include <spdlog/spdlog.h>

namespace vw::net {

NetLibrary::NetLibrary() {
#if defined(_WIN32)
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        spdlog::error("WSAStartup failed");
        return;
    }
#endif
    ok_ = reliable_init() == RELIABLE_OK;
    reliable_log_level(RELIABLE_LOG_LEVEL_NONE);
    juice_set_log_level(JUICE_LOG_LEVEL_WARN);
    if (!ok_)
        spdlog::error("reliable_init failed");
}

NetLibrary::~NetLibrary() {
    if (ok_)
        reliable_term();
#if defined(_WIN32)
    WSACleanup();
#endif
}

}
