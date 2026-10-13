#include "net/NetLibrary.h"

#include <juice/juice.h>
#include <reliable.h>
#include <spdlog/spdlog.h>

namespace vw::net {

NetLibrary::NetLibrary() {
    ok_ = reliable_init() == RELIABLE_OK;
    juice_set_log_level(JUICE_LOG_LEVEL_WARN);
    if (!ok_)
        spdlog::error("reliable_init failed");
}

NetLibrary::~NetLibrary() {
    if (ok_)
        reliable_term();
}

}
