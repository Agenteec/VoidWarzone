#pragma once

namespace vw::net {

class NetLibrary {
public:
    NetLibrary();
    ~NetLibrary();

    NetLibrary(const NetLibrary&) = delete;
    NetLibrary& operator=(const NetLibrary&) = delete;

    bool ok() const { return ok_; }

private:
    bool ok_ = false;
};

}
