// Store-only streaming ZIP writer (Spec 7.5). Little-endian fields are written one by one.
#include "export/ZipWriter.h"

#include <cstring>
#include "util/Crc32.h"

namespace {

constexpr uint32_t kLocalSig = 0x04034b50;
constexpr uint32_t kCentralSig = 0x02014b50;
constexpr uint32_t kEocdSig = 0x06054b50;
constexpr uint16_t kVersion = 20;
constexpr uint16_t kFlagUtf8 = 0x0800;
constexpr uint16_t kMethodStore = 0;
constexpr uint16_t kDosTime = 0x0000;
constexpr uint16_t kDosDate = 0x0021;  // 1980-01-01
constexpr uint64_t kMaxOffset = 0xFFFFFFFEull;  // offsets must stay below 0xFFFFFFFF
constexpr uint64_t kMaxEntrySize = 0xFFFFFFFEull;
constexpr size_t kMaxEntries = 0xFFFF;
constexpr size_t kMaxName = 0xFFFF;

void Put16(uint8_t*& p, uint16_t v) {
    *p++ = static_cast<uint8_t>(v & 0xFF);
    *p++ = static_cast<uint8_t>((v >> 8) & 0xFF);
}
void Put32(uint8_t*& p, uint32_t v) {
    *p++ = static_cast<uint8_t>(v & 0xFF);
    *p++ = static_cast<uint8_t>((v >> 8) & 0xFF);
    *p++ = static_cast<uint8_t>((v >> 16) & 0xFF);
    *p++ = static_cast<uint8_t>((v >> 24) & 0xFF);
}

}  // namespace

Status ZipWriter::WriteRaw(const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    size_t remaining = len;
    while (remaining > 0) {
        const DWORD chunk = remaining > 0x40000000u ? 0x40000000u : static_cast<DWORD>(remaining);
        DWORD written = 0;
        if (!WriteFile(file_, p, chunk, &written, nullptr)) {
            failed_ = true;
            return Status::FromLastError(L"ZIP write");
        }
        if (written == 0) {
            failed_ = true;
            return Status::Fail(L"ZIP write: no progress (disk full?)");
        }
        p += written;
        remaining -= written;
    }
    offset_ += len;
    return Status::Ok();
}

Status ZipWriter::Open(HANDLE file) {
    if (file == nullptr || file == INVALID_HANDLE_VALUE) return Status::Fail(L"ZIP: invalid file handle");
    LARGE_INTEGER zero{};
    LARGE_INTEGER pos{};
    if (!SetFilePointerEx(file, zero, &pos, FILE_END)) return Status::FromLastError(L"ZIP seek to end");
    file_ = file;
    entries_.clear();
    inEntry_ = false;
    failed_ = false;
    offset_ = static_cast<uint64_t>(pos.QuadPart);
    curCrc_ = 0;
    curSize_ = 0;
    curHeaderOffset_ = 0;
    return Status::Ok();
}

Status ZipWriter::BeginEntry(std::string_view utf8Name) {
    if (file_ == nullptr) return Status::Fail(L"ZIP: not open");
    if (failed_) return Status::Fail(L"ZIP: archive is in a failed state");
    if (inEntry_) return Status::Fail(L"ZIP: entry already open");
    if (utf8Name.empty() || utf8Name.size() > kMaxName) return Status::Fail(L"ZIP: invalid entry name length");
    if (entries_.size() >= kMaxEntries) return Status::Fail(L"ZIP: too many entries for classic ZIP");
    if (offset_ > kMaxOffset) return Status::Fail(L"ZIP: archive exceeds classic ZIP size limit");

    uint8_t hdr[30];
    uint8_t* p = hdr;
    Put32(p, kLocalSig);
    Put16(p, kVersion);
    Put16(p, kFlagUtf8);
    Put16(p, kMethodStore);
    Put16(p, kDosTime);
    Put16(p, kDosDate);
    Put32(p, 0);  // crc placeholder
    Put32(p, 0);  // compressed size placeholder
    Put32(p, 0);  // uncompressed size placeholder
    Put16(p, static_cast<uint16_t>(utf8Name.size()));
    Put16(p, 0);  // extra length

    const uint64_t headerOffset = offset_;
    Status st = WriteRaw(hdr, sizeof hdr);
    if (!st) return st;
    st = WriteRaw(utf8Name.data(), utf8Name.size());
    if (!st) return st;

    Entry e;
    e.name.assign(utf8Name);
    e.localHeaderOffset = static_cast<uint32_t>(headerOffset);
    entries_.push_back(std::move(e));
    curHeaderOffset_ = headerOffset;
    curCrc_ = 0;
    curSize_ = 0;
    inEntry_ = true;
    return Status::Ok();
}

Status ZipWriter::Write(const void* data, size_t len) {
    if (!inEntry_) return Status::Fail(L"ZIP: no open entry");
    if (failed_) return Status::Fail(L"ZIP: archive is in a failed state");
    if (len == 0) return Status::Ok();
    if (static_cast<uint64_t>(len) > kMaxEntrySize || curSize_ + len > kMaxEntrySize)
        return Status::Fail(L"ZIP: entry exceeds classic ZIP size limit");
    if (offset_ + len > kMaxOffset) return Status::Fail(L"ZIP: archive exceeds classic ZIP size limit");
    Status st = WriteRaw(data, len);
    if (!st) return st;
    curCrc_ = Crc32Update(curCrc_, data, len);
    curSize_ += len;
    return Status::Ok();
}

Status ZipWriter::EndEntry() {
    if (!inEntry_) return Status::Fail(L"ZIP: no open entry");
    if (failed_) return Status::Fail(L"ZIP: archive is in a failed state");

    uint8_t patch[12];
    uint8_t* p = patch;
    Put32(p, curCrc_);
    Put32(p, static_cast<uint32_t>(curSize_));
    Put32(p, static_cast<uint32_t>(curSize_));

    LARGE_INTEGER pos;
    pos.QuadPart = static_cast<LONGLONG>(curHeaderOffset_ + 14);
    if (!SetFilePointerEx(file_, pos, nullptr, FILE_BEGIN)) {
        failed_ = true;
        return Status::FromLastError(L"ZIP seek to header");
    }
    // Write the patch without touching offset_ (WriteRaw would advance it).
    DWORD written = 0;
    if (!WriteFile(file_, patch, sizeof patch, &written, nullptr)) {
        failed_ = true;
        return Status::FromLastError(L"ZIP header patch");
    }
    if (written != sizeof patch) {
        failed_ = true;
        return Status::Fail(L"ZIP header patch: short write");
    }
    pos.QuadPart = static_cast<LONGLONG>(offset_);
    if (!SetFilePointerEx(file_, pos, nullptr, FILE_BEGIN)) {
        failed_ = true;
        return Status::FromLastError(L"ZIP seek to end");
    }

    Entry& e = entries_.back();
    e.crc = curCrc_;
    e.size = static_cast<uint32_t>(curSize_);
    inEntry_ = false;
    return Status::Ok();
}

Status ZipWriter::Finish() {
    if (file_ == nullptr) return Status::Fail(L"ZIP: not open");
    if (failed_) return Status::Fail(L"ZIP: archive is in a failed state");
    if (inEntry_) return Status::Fail(L"ZIP: entry still open");
    if (entries_.size() > kMaxEntries) return Status::Fail(L"ZIP: too many entries for classic ZIP");

    const uint64_t cdOffset = offset_;
    if (cdOffset > kMaxOffset) return Status::Fail(L"ZIP: central directory offset exceeds classic ZIP limit");

    for (const Entry& e : entries_) {
        uint8_t hdr[46];
        uint8_t* p = hdr;
        Put32(p, kCentralSig);
        Put16(p, kVersion);  // version made by
        Put16(p, kVersion);  // version needed
        Put16(p, kFlagUtf8);
        Put16(p, kMethodStore);
        Put16(p, kDosTime);
        Put16(p, kDosDate);
        Put32(p, e.crc);
        Put32(p, e.size);
        Put32(p, e.size);
        Put16(p, static_cast<uint16_t>(e.name.size()));
        Put16(p, 0);  // extra
        Put16(p, 0);  // comment
        Put16(p, 0);  // disk number start
        Put16(p, 0);  // internal attrs
        Put32(p, 0);  // external attrs
        Put32(p, e.localHeaderOffset);
        Status st = WriteRaw(hdr, sizeof hdr);
        if (!st) return st;
        st = WriteRaw(e.name.data(), e.name.size());
        if (!st) return st;
    }

    const uint64_t cdSize = offset_ - cdOffset;
    if (cdSize > kMaxOffset || offset_ + 22 > kMaxOffset)
        return Status::Fail(L"ZIP: archive exceeds classic ZIP size limit");

    uint8_t eocd[22];
    uint8_t* p = eocd;
    Put32(p, kEocdSig);
    Put16(p, 0);  // this disk
    Put16(p, 0);  // disk with central directory
    Put16(p, static_cast<uint16_t>(entries_.size()));
    Put16(p, static_cast<uint16_t>(entries_.size()));
    Put32(p, static_cast<uint32_t>(cdSize));
    Put32(p, static_cast<uint32_t>(cdOffset));
    Put16(p, 0);  // comment length
    return WriteRaw(eocd, sizeof eocd);
}
