// FileCoder primitives — factual binary layout of WOLF RPG data ("dat") records,
// independent clean-room implementation.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace wolf {

class Reader {
public:
    explicit Reader(std::vector<uint8_t> data) : data_(std::move(data)) {}

    size_t pos() const { return pos_; }
    bool eof() const { return pos_ >= data_.size(); }
    size_t remaining() const { return data_.size() - pos_; }

    uint8_t u8();
    int32_t i32();
    std::string str();            // u32 size (includes NUL) + size-1 bytes + NUL check
    std::vector<uint8_t> bytes(); // u32 count + count bytes
    std::vector<int32_t> ints();
    std::vector<std::string> strings();
    std::vector<uint8_t> raw(size_t n);

    // Applies the standard Wolf dat-entry obfuscation:
    // three LCG-XOR streams interleaved at steps 1/2/5, seeds derived from the
    // 10-byte crypt header at indices {0, 8, 6}. Consumes the header first.
    void decrypt_entry(const uint8_t header[10]);

private:
    std::vector<uint8_t> data_;
    size_t pos_ = 0;
    void need(size_t n);
};

} // namespace wolf
