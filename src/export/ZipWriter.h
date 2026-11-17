#pragma once
// FROZEN CONTRACT (Wave 0). Implementation: ZipWriter.cpp (WP-D).
// Classic ZIP, method 0 (Store), streaming with seek-back patching of local headers (no data descriptors).
#include <windows.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include "util/Status.h"

class ZipWriter {
public:
    Status Open(HANDLE file);                        // not owned; must be seekable; writes start at current end
    // Local header: version 20, flags bit 11 (UTF-8 names), method 0, fixed DOS time 1980-01-01 00:00,
    // CRC and sizes = 0 placeholders (patched by EndEntry).
    Status BeginEntry(std::string_view utf8Name);
    Status Write(const void* data, size_t len);      // updates CRC/size; Fail if entry would exceed 0xFFFFFFFE bytes
    Status Write(std::string_view s) { return Write(s.data(), s.size()); }
    Status EndEntry();                               // seek back, patch CRC + sizes, seek to end
    // Central directory + end-of-central-directory. Fail if > 65535 entries or any offset >= 4 GiB.
    Status Finish();

private:
    struct Entry {
        std::string name;
        uint32_t    crc = 0;
        uint32_t    size = 0;
        uint32_t    localHeaderOffset = 0;
    };
    HANDLE             file_ = nullptr;
    std::vector<Entry> entries_;
    bool               inEntry_ = false;
    uint64_t           offset_ = 0;   // current write offset
    uint32_t           curCrc_ = 0;           // running CRC of the open entry
    uint64_t           curSize_ = 0;          // bytes written to the open entry
    uint64_t           curHeaderOffset_ = 0;  // file offset of the open entry's local header
    bool               failed_ = false;       // a write failed; archive unusable
    Status             WriteRaw(const void* data, size_t len);
};
