#include "filecoder.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace wolf {

void Reader::need(size_t n) {
    if (pos_ + n > data_.size())
        throw std::runtime_error("eof at offset " + std::to_string(pos_) +
                                 " while wanting " + std::to_string(n) + " bytes");
}

uint8_t Reader::u8() { need(1); return data_[pos_++]; }

int32_t Reader::i32() {
    need(4);
    int32_t v;
    std::memcpy(&v, data_.data() + pos_, 4);
    pos_ += 4;
    return v;
}

std::string Reader::str() {
    const int32_t size = i32();
    if (size <= 0)
        throw std::runtime_error("string with size <= 0 at offset " + std::to_string(pos_ - 4));
    need(static_cast<size_t>(size - 1) + 1);
    std::string out(reinterpret_cast<const char*>(data_.data() + pos_),
                    static_cast<size_t>(size) - 1);
    pos_ += static_cast<size_t>(size) - 1;
    const uint8_t nul = u8();
    if (nul != 0)
        throw std::runtime_error("string not NUL-terminated at offset " + std::to_string(pos_ - 1));
    return out;
}

std::vector<uint8_t> Reader::bytes() {
    const uint32_t n = static_cast<uint32_t>(i32());
    if (n == 0 || n > 0xFFFFFF) // absurd guard
        throw std::runtime_error("bad byte-array count " + std::to_string(n));
    return raw(n);
}

std::vector<int32_t> Reader::ints() {
    const uint32_t n = static_cast<uint32_t>(i32());
    std::vector<int32_t> v(n);
    for (auto& e : v) e = i32();
    return v;
}

std::vector<std::string> Reader::strings() {
    const uint32_t n = static_cast<uint32_t>(i32());
    std::vector<std::string> v(n);
    for (auto& e : v) e = str();
    return v;
}

std::vector<uint8_t> Reader::raw(size_t n) {
    need(n);
    std::vector<uint8_t> out(data_.begin() + pos_, data_.begin() + pos_ + n);
    pos_ += n;
    return out;
}

void Reader::decrypt_entry(const uint8_t header[10]) {
    const uint32_t seeds[3] = {
        header[0], header[8], header[6]
    };
    const uint32_t intervals[3] = { 1, 2, 5 };
    for (size_t s = 0; s < 3; ++s) {
        uint32_t seed = seeds[s];
        for (size_t i = s; i < data_.size(); i += intervals[s]) {
            seed = seed * 0x343FDu + 0x269EC3u;
            data_[i] ^= static_cast<uint8_t>((seed >> 28) & 7);
        }
    }
}

} // namespace wolf
