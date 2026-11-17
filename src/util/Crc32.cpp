#include "util/Crc32.h"

namespace {

struct Crc32Table {
    uint32_t v[256];
};

constexpr Crc32Table MakeTable() {
    Crc32Table t{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        t.v[i] = c;
    }
    return t;
}

constexpr Crc32Table kTable = MakeTable();

}  // namespace

uint32_t Crc32Update(uint32_t crc, const void* data, size_t len) {
    const auto* p = static_cast<const unsigned char*>(data);
    uint32_t c = ~crc;
    for (size_t i = 0; i < len; ++i) c = kTable.v[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return ~c;
}
