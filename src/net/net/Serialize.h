#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace vw::net {

class Writer {
public:
    void u8(std::uint8_t v) { buf_.push_back(v); }
    void u16(std::uint16_t v) {
        u8(static_cast<std::uint8_t>(v));
        u8(static_cast<std::uint8_t>(v >> 8));
    }
    void u32(std::uint32_t v) {
        u16(static_cast<std::uint16_t>(v));
        u16(static_cast<std::uint16_t>(v >> 16));
    }
    void u64(std::uint64_t v) {
        u32(static_cast<std::uint32_t>(v));
        u32(static_cast<std::uint32_t>(v >> 32));
    }
    void i16(std::int16_t v) { u16(static_cast<std::uint16_t>(v)); }
    void f32(float v) {
        std::uint32_t b;
        std::memcpy(&b, &v, sizeof(b));
        u32(b);
    }
    void varu(std::uint64_t v) {
        while (v >= 0x80) {
            u8(static_cast<std::uint8_t>(v | 0x80));
            v >>= 7;
        }
        u8(static_cast<std::uint8_t>(v));
    }
    void str(std::string_view s) {
        varu(s.size());
        bytes(s.data(), s.size());
    }
    void bytes(const void* data, std::size_t size) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        buf_.insert(buf_.end(), p, p + size);
    }

    const std::vector<std::uint8_t>& data() const { return buf_; }
    std::vector<std::uint8_t>& data() { return buf_; }
    std::size_t size() const { return buf_.size(); }
    void clear() { buf_.clear(); }

private:
    std::vector<std::uint8_t> buf_;
};

class Reader {
public:
    Reader(const void* data, std::size_t size) : p_(static_cast<const std::uint8_t*>(data)), size_(size) {}

    std::uint8_t u8() { return need(1) ? p_[pos_++] : 0; }
    std::uint16_t u16() {
        const std::uint16_t lo = u8();
        return static_cast<std::uint16_t>(lo | (u8() << 8));
    }
    std::uint32_t u32() {
        const std::uint32_t lo = u16();
        return lo | (static_cast<std::uint32_t>(u16()) << 16);
    }
    std::uint64_t u64() {
        const std::uint64_t lo = u32();
        return lo | (static_cast<std::uint64_t>(u32()) << 32);
    }
    std::int16_t i16() { return static_cast<std::int16_t>(u16()); }
    float f32() {
        const std::uint32_t b = u32();
        float v;
        std::memcpy(&v, &b, sizeof(v));
        return v;
    }
    std::uint64_t varu() {
        std::uint64_t v = 0;
        for (int shift = 0; shift < 64; shift += 7) {
            const std::uint8_t b = u8();
            v |= static_cast<std::uint64_t>(b & 0x7f) << shift;
            if (!(b & 0x80))
                return v;
        }
        ok_ = false;
        return 0;
    }
    std::string str(std::size_t maxLen = 256) {
        const auto len = varu();
        if (len > maxLen || !need(static_cast<std::size_t>(len)))
            return fail();
        std::string s(reinterpret_cast<const char*>(p_ + pos_), static_cast<std::size_t>(len));
        pos_ += static_cast<std::size_t>(len);
        return s;
    }
    bool bytes(void* out, std::size_t size) {
        if (!need(size))
            return false;
        std::memcpy(out, p_ + pos_, size);
        pos_ += size;
        return true;
    }
    bool skip(std::size_t size) {
        if (!need(size))
            return false;
        pos_ += size;
        return true;
    }

    const std::uint8_t* current() const { return p_ + pos_; }
    std::size_t remaining() const { return size_ - pos_; }
    bool ok() const { return ok_; }

private:
    bool need(std::size_t n) {
        if (!ok_ || size_ - pos_ < n) {
            ok_ = false;
            return false;
        }
        return true;
    }
    std::string fail() {
        ok_ = false;
        return {};
    }

    const std::uint8_t* p_;
    std::size_t size_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

}
