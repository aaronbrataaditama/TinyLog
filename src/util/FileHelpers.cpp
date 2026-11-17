#include "util/FileHelpers.h"

#include <windows.h>

#include <vector>

#include "util/Handles.h"

namespace {

constexpr DWORD kReadChunk = 64 * 1024;
constexpr DWORD kWriteChunkMax = 1u << 30;

// Writes bytes to path (CREATE_ALWAYS, write-through), flushes and closes. Caller deletes on failure.
Status WriteTempFile(const std::wstring& path, std::string_view bytes) {
    UniqueHandle h(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr));
    if (!h) return Status::FromLastError(L"create " + path);

    size_t off = 0;
    while (off < bytes.size()) {
        const size_t remaining = bytes.size() - off;
        const DWORD chunk = static_cast<DWORD>(remaining < kWriteChunkMax ? remaining : kWriteChunkMax);
        DWORD written = 0;
        if (!WriteFile(h.get(), bytes.data() + off, chunk, &written, nullptr))
            return Status::FromLastError(L"write " + path);
        if (written == 0) return Status::Fail(L"write " + path + L": short write", 0);
        off += written;
    }
    if (!FlushFileBuffers(h.get())) return Status::FromLastError(L"flush " + path);
    if (!CloseHandle(h.release())) return Status::FromLastError(L"close " + path);
    return Status::Ok();
}

}  // namespace

Status GetExePath(std::wstring& path) {
    path.clear();
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return Status::FromLastError(L"GetModuleFileNameW");
        if (static_cast<size_t>(n) < buf.size()) {
            path.assign(buf.data(), n);
            return Status::Ok();
        }
        if (buf.size() >= 32768) return Status::Fail(L"GetModuleFileNameW: path too long", ERROR_INSUFFICIENT_BUFFER);
        buf.resize(buf.size() * 2);
    }
}

Status GetExeDirectory(std::wstring& dir) {
    std::wstring p;
    Status st = GetExePath(p);
    if (!st) return st;
    const size_t pos = p.find_last_of(L'\\');
    if (pos == std::wstring::npos) return Status::Fail(L"GetExeDirectory: no directory in " + p);
    dir = p.substr(0, pos);
    if (!dir.empty() && dir.back() == L':') dir += L'\\';  // EXE at drive root: "C:" would be relative
    return Status::Ok();
}

Status WriteFileAtomic(const std::wstring& target, std::string_view bytes) {
    const std::wstring tmp = target + L".tmp";

    Status st = WriteTempFile(tmp, bytes);
    if (!st) {
        DeleteFileW(tmp.c_str());
        return st;
    }

    const bool exists = GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES;
    const BOOL ok = exists
        ? ReplaceFileW(target.c_str(), tmp.c_str(), nullptr, REPLACEFILE_WRITE_THROUGH, nullptr, nullptr)
        : MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH);
    if (!ok) {
        Status f = Status::FromLastError(exists ? L"ReplaceFileW " + target : L"MoveFileExW " + target);
        DeleteFileW(tmp.c_str());
        return f;
    }
    return Status::Ok();
}

Status ReadWholeFile(const std::wstring& path, std::string& bytes) {
    bytes.clear();
    UniqueHandle h(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h) return Status::FromLastError(L"open " + path);

    char buf[kReadChunk];
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(h.get(), buf, kReadChunk, &got, nullptr)) {
            Status st = Status::FromLastError(L"read " + path);
            bytes.clear();
            return st;
        }
        if (got == 0) break;
        bytes.append(buf, got);
    }
    return Status::Ok();
}

bool FileExists(const std::wstring& path) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}
