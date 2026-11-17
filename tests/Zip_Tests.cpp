// ZipWriter tests (WP-D). Independent of JsonLinesStore. Contains a tiny in-test ZIP reader.
#include "TestHarness.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "export/ZipWriter.h"
#include "util/Crc32.h"
#include "util/FileHelpers.h"
#include "util/Handles.h"

namespace {

struct ZEntry {
    std::string name;
    uint32_t    crc = 0;
    uint32_t    size = 0;
    uint32_t    localOffset = 0;
    std::string data;
};

uint16_t U16(const std::string& b, size_t p) {
    return static_cast<uint16_t>(static_cast<uint8_t>(b[p]) | (static_cast<uint8_t>(b[p + 1]) << 8));
}
uint32_t U32(const std::string& b, size_t p) {
    return static_cast<uint32_t>(U16(b, p)) | (static_cast<uint32_t>(U16(b, p + 2)) << 16);
}

// Walks the central directory from the EOCD and validates every local header. Returns "" on success.
std::string ParseZip(const std::string& b, std::vector<ZEntry>& out) {
    out.clear();
    if (b.size() < 22) return "too small";
    const size_t eocd = b.size() - 22;
    if (U32(b, eocd) != 0x06054b50) return "no EOCD signature";
    if (U16(b, eocd + 4) != 0 || U16(b, eocd + 6) != 0) return "multi-disk";
    const size_t count = U16(b, eocd + 10);
    if (U16(b, eocd + 8) != count) return "entry count mismatch";
    const size_t cdSize = U32(b, eocd + 12);
    const size_t cdOff = U32(b, eocd + 16);
    if (U16(b, eocd + 20) != 0) return "unexpected comment";
    if (cdOff + cdSize != eocd) return "central directory bounds";

    size_t pos = cdOff;
    for (size_t i = 0; i < count; ++i) {
        if (pos + 46 > eocd) return "central header out of bounds";
        if (U32(b, pos) != 0x02014b50) return "bad central signature";
        if (U16(b, pos + 4) != 20 || U16(b, pos + 6) != 20) return "bad central versions";
        if (U16(b, pos + 8) != 0x0800) return "bad central flags";
        if (U16(b, pos + 10) != 0) return "not stored";
        if (U16(b, pos + 12) != 0x0000 || U16(b, pos + 14) != 0x0021) return "bad central dos time";
        ZEntry e;
        e.crc = U32(b, pos + 16);
        const uint32_t csize = U32(b, pos + 20);
        e.size = U32(b, pos + 24);
        if (csize != e.size) return "compressed != uncompressed";
        const size_t nlen = U16(b, pos + 28);
        if (U16(b, pos + 30) != 0 || U16(b, pos + 32) != 0) return "unexpected extra/comment";
        e.localOffset = U32(b, pos + 42);
        if (pos + 46 + nlen > eocd) return "central name out of bounds";
        e.name = b.substr(pos + 46, nlen);

        const size_t lo = e.localOffset;
        if (lo + 30 > cdOff) return "local header out of bounds";
        if (U32(b, lo) != 0x04034b50) return "bad local signature";
        if (U16(b, lo + 4) != 20) return "bad local version";
        if (U16(b, lo + 6) != 0x0800) return "bad local flags";
        if (U16(b, lo + 8) != 0) return "bad local method";
        if (U16(b, lo + 10) != 0x0000 || U16(b, lo + 12) != 0x0021) return "bad local dos time";
        if (U32(b, lo + 14) != e.crc) return "local crc mismatch";
        if (U32(b, lo + 18) != e.size || U32(b, lo + 22) != e.size) return "local size mismatch";
        if (U16(b, lo + 26) != nlen || U16(b, lo + 28) != 0) return "local name/extra length mismatch";
        if (b.compare(lo + 30, nlen, e.name) != 0) return "local name mismatch";
        const size_t dataOff = lo + 30 + nlen;
        if (dataOff + e.size > cdOff) return "data out of bounds";
        e.data = b.substr(dataOff, e.size);
        uint32_t crc = 0;
        for (size_t off = 0; off < e.data.size(); off += 4096) {
            const size_t n = e.data.size() - off < 4096 ? e.data.size() - off : 4096;
            crc = Crc32Update(crc, e.data.data() + off, n);
        }
        if (crc != e.crc) return "crc does not match data";
        out.push_back(std::move(e));
        pos += 46 + nlen;
    }
    if (pos != eocd) return "trailing bytes in central directory";
    return "";
}

UniqueHandle OpenTemp(const std::wstring& path) {
    return UniqueHandle(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr));
}

}  // namespace

TEST(Zip_TwoEntriesRoundTrip) {
    TempDir dir;
    const std::wstring path = dir.File(L"two.zip");
    const std::string second = std::string(10000, 'x') + "tail";
    {
        UniqueHandle f = OpenTemp(path);
        CHECK(f.valid());
        ZipWriter zip;
        CHECK(zip.Open(f.get()).ok);
        CHECK(zip.BeginEntry("hello.txt").ok);
        CHECK(zip.Write(std::string_view("Hello, ")).ok);
        CHECK(zip.Write(std::string_view("world")).ok);
        CHECK(zip.EndEntry().ok);
        CHECK(zip.BeginEntry("dir/second.bin").ok);
        for (size_t i = 0; i < second.size(); i += 777)
            CHECK(zip.Write(second.data() + i, std::min<size_t>(777, second.size() - i)).ok);
        CHECK(zip.EndEntry().ok);
        CHECK(zip.Finish().ok);
    }
    std::string bytes;
    CHECK(ReadWholeFile(path, bytes).ok);
    std::vector<ZEntry> es;
    const std::string err = ParseZip(bytes, es);
    CHECK_EQ(err, std::string());
    CHECK_EQ(es.size(), size_t(2));
    if (es.size() == 2) {
        CHECK_EQ(es[0].name, std::string("hello.txt"));
        CHECK_EQ(es[0].data, std::string("Hello, world"));
        CHECK_EQ(es[0].localOffset, 0u);
        CHECK_EQ(es[1].name, std::string("dir/second.bin"));
        CHECK_EQ(es[1].data, second);
        CHECK(es[1].localOffset > 0);
    }
}

TEST(Zip_KnownCrc) {
    TempDir dir;
    const std::wstring path = dir.File(L"crc.zip");
    {
        UniqueHandle f = OpenTemp(path);
        ZipWriter zip;
        CHECK(zip.Open(f.get()).ok);
        CHECK(zip.BeginEntry("n.txt").ok);
        CHECK(zip.Write(std::string_view("123456789")).ok);
        CHECK(zip.EndEntry().ok);
        CHECK(zip.Finish().ok);
    }
    std::string bytes;
    CHECK(ReadWholeFile(path, bytes).ok);
    std::vector<ZEntry> es;
    CHECK_EQ(ParseZip(bytes, es), std::string());
    CHECK_EQ(es.size(), size_t(1));
    if (!es.empty()) CHECK_EQ(es[0].crc, 0xCBF43926u);
}

TEST(Zip_EmptyEntryAndEmptyArchive) {
    TempDir dir;
    {
        const std::wstring path = dir.File(L"emptyentry.zip");
        UniqueHandle f = OpenTemp(path);
        ZipWriter zip;
        CHECK(zip.Open(f.get()).ok);
        CHECK(zip.BeginEntry("empty").ok);
        CHECK(zip.EndEntry().ok);
        CHECK(zip.Finish().ok);
        f.reset();
        std::string bytes;
        CHECK(ReadWholeFile(path, bytes).ok);
        std::vector<ZEntry> es;
        CHECK_EQ(ParseZip(bytes, es), std::string());
        CHECK_EQ(es.size(), size_t(1));
        if (!es.empty()) {
            CHECK_EQ(es[0].size, 0u);
            CHECK_EQ(es[0].crc, 0u);
        }
    }
    {
        const std::wstring path = dir.File(L"empty.zip");
        UniqueHandle f = OpenTemp(path);
        ZipWriter zip;
        CHECK(zip.Open(f.get()).ok);
        CHECK(zip.Finish().ok);
        f.reset();
        std::string bytes;
        CHECK(ReadWholeFile(path, bytes).ok);
        CHECK_EQ(bytes.size(), size_t(22));
        std::vector<ZEntry> es;
        CHECK_EQ(ParseZip(bytes, es), std::string());
        CHECK(es.empty());
    }
}

TEST(Zip_Utf8NameAndLargeEntry) {
    TempDir dir;
    const std::wstring path = dir.File(L"big.zip");
    std::string chunk(65536, '\0');
    for (size_t i = 0; i < chunk.size(); ++i) chunk[i] = static_cast<char>(i * 31 + 7);
    const int kChunks = 48;  // 3 MiB
    {
        UniqueHandle f = OpenTemp(path);
        ZipWriter zip;
        CHECK(zip.Open(f.get()).ok);
        CHECK(zip.BeginEntry("caf\xC3\xA9.dat").ok);
        for (int i = 0; i < kChunks; ++i) CHECK(zip.Write(chunk.data(), chunk.size()).ok);
        CHECK(zip.EndEntry().ok);
        CHECK(zip.BeginEntry("after").ok);
        CHECK(zip.Write(std::string_view("z")).ok);
        CHECK(zip.EndEntry().ok);
        CHECK(zip.Finish().ok);
    }
    std::string bytes;
    CHECK(ReadWholeFile(path, bytes).ok);
    std::vector<ZEntry> es;
    CHECK_EQ(ParseZip(bytes, es), std::string());
    CHECK_EQ(es.size(), size_t(2));
    if (es.size() == 2) {
        CHECK_EQ(es[0].name, std::string("caf\xC3\xA9.dat"));
        CHECK_EQ(es[0].size, static_cast<uint32_t>(kChunks * chunk.size()));
        CHECK_EQ(es[1].data, std::string("z"));
    }
}

TEST(Zip_MisuseIsRejected) {
    TempDir dir;
    UniqueHandle f = OpenTemp(dir.File(L"misuse.zip"));
    ZipWriter zip;
    CHECK(!zip.BeginEntry("x").ok);               // not open
    CHECK(!zip.Open(INVALID_HANDLE_VALUE).ok);
    CHECK(zip.Open(f.get()).ok);
    CHECK(!zip.Write(std::string_view("a")).ok);   // no entry
    CHECK(!zip.EndEntry().ok);
    CHECK(!zip.BeginEntry("").ok);
    CHECK(zip.BeginEntry("a").ok);
    CHECK(!zip.BeginEntry("b").ok);                // entry already open
    CHECK(!zip.Finish().ok);                       // entry still open
    CHECK(zip.EndEntry().ok);
    CHECK(zip.Finish().ok);
}

TEST(Zip_TooManyEntriesFailsCleanly) {
    TempDir dir;
    const std::wstring path = dir.File(L"many.zip");
    UniqueHandle f = OpenTemp(path);
    ZipWriter zip;
    CHECK(zip.Open(f.get()).ok);
    bool allOk = true;
    for (int i = 0; i < 65535 && allOk; ++i) {
        char name[16];
        snprintf(name, sizeof name, "%d", i);
        allOk = zip.BeginEntry(name).ok && zip.EndEntry().ok;
    }
    CHECK(allOk);
    CHECK(!zip.BeginEntry("overflow").ok);  // 65536th entry is refused, not silently wrapped
    if (allOk) {
        CHECK(zip.Finish().ok);
        f.reset();
        std::string bytes;
        CHECK(ReadWholeFile(path, bytes).ok);
        std::vector<ZEntry> es;
        CHECK_EQ(ParseZip(bytes, es), std::string());
        CHECK_EQ(es.size(), size_t(65535));
    }
}
