// JSON Lines completed-session log (WP-C). Spec 5.2 / 5.5. Binary I/O, 64 KB streaming.
#include "storage/JsonLinesStore.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

#include "util/Handles.h"

namespace {

constexpr DWORD  kChunk = 64 * 1024;
constexpr size_t kMaxLine = 16u * 1024 * 1024;   // a line longer than this is treated as malformed
constexpr DWORD  kShare = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

struct Line {
    std::string_view text;     // without '\n' and a trailing '\r'; empty when tooLong
    uint64_t         start = 0;  // file offset of the first byte of the line
    uint64_t         end = 0;    // offset just after the '\n' (or EOF for an unterminated line)
    bool             terminated = false;
    bool             tooLong = false;
};

bool IsBlank(std::string_view s) {
    for (const char c : s) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') return false;
    }
    return true;
}

bool IsMissingError(DWORD e) { return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND; }

// Streams the file front to back in 64 KB chunks, invoking onLine(Line) per line; onLine returns false to
// stop. exists=false (and Ok) when the file is missing. fileSize receives the bytes seen.
template <class F>
Status ScanLines(const std::wstring& path, bool& exists, uint64_t& fileSize, F&& onLine) {
    exists = false;
    fileSize = 0;
    UniqueHandle h(CreateFileW(path.c_str(), GENERIC_READ, kShare, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (!h) {
        const DWORD e = GetLastError();
        if (IsMissingError(e)) return Status::Ok();
        SetLastError(e);
        return Status::FromLastError(L"open " + path);
    }
    exists = true;

    std::vector<char> buf(kChunk);
    std::string carry;       // bytes of a line spanning chunk boundaries
    bool tooLong = false;
    uint64_t carryStart = 0;
    uint64_t filePos = 0;    // offset of buf[0]

    for (;;) {
        DWORD got = 0;
        if (!ReadFile(h.get(), buf.data(), kChunk, &got, nullptr)) return Status::FromLastError(L"read " + path);
        if (got == 0) break;

        size_t i = 0;
        while (i < got) {
            const void* nlp = std::memchr(buf.data() + i, '\n', got - i);
            if (!nlp) {
                if (!tooLong) {
                    if (carry.size() + (got - i) > kMaxLine) {
                        tooLong = true;
                        carry.clear();
                        carry.shrink_to_fit();
                    } else {
                        carry.append(buf.data() + i, got - i);
                    }
                }
                break;
            }
            const size_t nl = static_cast<size_t>(static_cast<const char*>(nlp) - buf.data());
            std::string_view text;
            if (!tooLong) {
                if (carry.empty()) {
                    text = std::string_view(buf.data() + i, nl - i);
                } else if (carry.size() + (nl - i) > kMaxLine) {
                    tooLong = true;
                } else {
                    carry.append(buf.data() + i, nl - i);
                    text = carry;
                }
            }
            if (tooLong) text = {};
            if (!text.empty() && text.back() == '\r') text.remove_suffix(1);

            Line line;
            line.text = text;
            line.start = carryStart;
            line.end = filePos + nl + 1;
            line.terminated = true;
            line.tooLong = tooLong;
            if (!onLine(line)) {
                fileSize = filePos + got;   // partial; callers only use it after a full scan
                return Status::Ok();
            }
            carry.clear();
            tooLong = false;
            carryStart = line.end;
            i = nl + 1;
        }
        filePos += got;
    }
    fileSize = filePos;

    if (!carry.empty() || tooLong) {
        std::string_view text = tooLong ? std::string_view() : std::string_view(carry);
        if (!text.empty() && text.back() == '\r') text.remove_suffix(1);
        Line line;
        line.text = text;
        line.start = carryStart;
        line.end = filePos;
        line.terminated = false;
        line.tooLong = tooLong;
        onLine(line);
    }
    return Status::Ok();
}

// A line yields a record if it is not too long and parses.
bool ParseLine(const Line& l, CompletedSession& out) {
    return !l.tooLong && !l.text.empty() && ParseCompleted(l.text, out);
}

}  // namespace

JsonLinesStore::JsonLinesStore(std::wstring path) : path_(std::move(path)) {}

Status JsonLinesStore::RepairTail() {
    bool exists = false;
    uint64_t size = 0;
    bool haveLast = false;
    bool lastBad = false;
    bool lastTerminated = true;
    uint64_t lastStart = 0;

    Status st = ScanLines(path_, exists, size, [&](const Line& l) {
        haveLast = true;
        lastStart = l.start;
        lastTerminated = l.terminated;
        CompletedSession tmp;
        // Only an unterminated final segment is ever a candidate for truncation (a torn write). A
        // newline-terminated line that fails strict parsing may be a real record (future schema, hand edit):
        // it is left untouched; ForEach/Contains skip it.
        lastBad = !l.terminated && !ParseLine(l, tmp);
        return true;
    });
    if (!st || !exists || !haveLast) return st;
    if (lastTerminated) return Status::Ok();

    UniqueHandle h(CreateFileW(path_.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h) return Status::FromLastError(L"open for repair " + path_);

    if (!lastBad) {
        // Complete record that merely lacks its '\n' (crash after the bytes landed): keep it, add the newline.
        LARGE_INTEGER pos;
        pos.QuadPart = static_cast<LONGLONG>(size);
        if (!SetFilePointerEx(h.get(), pos, nullptr, FILE_BEGIN)) return Status::FromLastError(L"seek " + path_);
        DWORD written = 0;
        if (!WriteFile(h.get(), "\n", 1, &written, nullptr)) return Status::FromLastError(L"write " + path_);
        if (written != 1) return Status::Fail(L"short write repairing " + path_);
    } else {
        LARGE_INTEGER pos;
        pos.QuadPart = static_cast<LONGLONG>(lastStart);
        if (!SetFilePointerEx(h.get(), pos, nullptr, FILE_BEGIN)) return Status::FromLastError(L"seek " + path_);
        if (!SetEndOfFile(h.get())) return Status::FromLastError(L"truncate " + path_);
    }
    if (!FlushFileBuffers(h.get())) return Status::FromLastError(L"flush " + path_);
    return Status::Ok();
}

Status JsonLinesStore::Contains(const std::string& id, bool& found) {
    found = false;
    bool exists = false;
    uint64_t size = 0;
    bool hit = false;
    Status st = ScanLines(path_, exists, size, [&](const Line& l) {
        CompletedSession c;
        if (ParseLine(l, c) && c.id == id) {
            hit = true;
            return false;
        }
        return true;
    });
    if (!st) return st;
    found = hit;
    return Status::Ok();
}

Status JsonLinesStore::Append(const CompletedSession& s) {
    Status rt = RepairTail();
    if (!rt) return rt;

    std::string line = SerializeCompleted(s);
    line += '\n';

    UniqueHandle h(CreateFileW(path_.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h) return Status::FromLastError(L"open " + path_);

    if (line.size() > 0x7FFFFFFFu) return Status::Fail(L"record too large");
    DWORD written = 0;
    if (!WriteFile(h.get(), line.data(), static_cast<DWORD>(line.size()), &written, nullptr))
        return Status::FromLastError(L"append " + path_);
    if (written != line.size()) return Status::Fail(L"short write appending to " + path_);
    if (!FlushFileBuffers(h.get())) return Status::FromLastError(L"flush " + path_);
    return Status::Ok();
}

Status JsonLinesStore::ForEach(const std::function<bool(const CompletedSession&)>& cb, size_t* skippedMalformed) {
    size_t skipped = 0;
    bool exists = false;
    uint64_t size = 0;
    Status st = ScanLines(path_, exists, size, [&](const Line& l) {
        CompletedSession c;
        if (ParseLine(l, c)) return cb(c);
        if (l.tooLong || !IsBlank(l.text)) ++skipped;
        return true;
    });
    if (skippedMalformed) *skippedMalformed = skipped;
    return st;
}

Status JsonLinesStore::ReadLast(std::optional<CompletedSession>& last) {
    last.reset();
    UniqueHandle h(CreateFileW(path_.c_str(), GENERIC_READ, kShare, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h) {
        const DWORD e = GetLastError();
        if (IsMissingError(e)) return Status::Ok();
        SetLastError(e);
        return Status::FromLastError(L"open " + path_);
    }
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h.get(), &sz)) return Status::FromLastError(L"size " + path_);
    uint64_t readPos = static_cast<uint64_t>(sz.QuadPart);   // bytes before readPos not yet in `tail`

    std::string tail;
    size_t end = 0;   // exclusive end of the unexamined region [0, end) of tail
    std::vector<char> buf(kChunk);

    auto prepend = [&]() -> Status {
        const DWORD n = static_cast<DWORD>(readPos < kChunk ? readPos : kChunk);
        readPos -= n;
        LARGE_INTEGER pos;
        pos.QuadPart = static_cast<LONGLONG>(readPos);
        if (!SetFilePointerEx(h.get(), pos, nullptr, FILE_BEGIN)) return Status::FromLastError(L"seek " + path_);
        DWORD total = 0;
        while (total < n) {
            DWORD got = 0;
            if (!ReadFile(h.get(), buf.data() + total, n - total, &got, nullptr))
                return Status::FromLastError(L"read " + path_);
            if (got == 0) return Status::Fail(L"unexpected end of file reading " + path_);
            total += got;
        }
        tail.insert(0, buf.data(), n);
        end += n;
        return Status::Ok();
    };

    auto tryLine = [&](size_t from, size_t to) -> bool {
        std::string_view t(tail.data() + from, to - from);
        if (!t.empty() && t.back() == '\r') t.remove_suffix(1);
        if (t.empty()) return false;
        CompletedSession c;
        if (!ParseCompleted(t, c)) return false;
        last = std::move(c);
        return true;
    };

    if (readPos == 0) return Status::Ok();
    {
        Status st = prepend();   // initial chunk; `end` grows from 0 to the chunk size
        if (!st) return st;
    }

    for (;;) {
        const size_t p = end == 0 ? std::string::npos : tail.rfind('\n', end - 1);
        if (p == std::string::npos) {
            if (readPos > 0) {
                if (end > kMaxLine) return Status::Ok();   // pathological unterminated line: give up
                Status st = prepend();
                if (!st) return st;
                continue;
            }
            tryLine(0, end);   // oldest line in the file
            return Status::Ok();
        }
        if (end - (p + 1) <= kMaxLine && tryLine(p + 1, end)) return Status::Ok();
        end = p;
    }
}
