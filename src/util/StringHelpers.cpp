#include "util/StringHelpers.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <utility>

std::string Utf8FromWide(std::wstring_view s) {
    if (s.empty()) return {};
    const int len = static_cast<int>(s.size());
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), len, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    if (WideCharToMultiByte(CP_UTF8, 0, s.data(), len, out.data(), n, nullptr, nullptr) != n) return {};
    return out;
}

bool WideFromUtf8(std::string_view s, std::wstring& out) {
    out.clear();
    if (s.empty()) return true;
    if (s.size() > 0x7FFFFFFFu) return false;
    const int len = static_cast<int>(s.size());
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), len, nullptr, 0);
    if (n <= 0) return false;
    std::wstring w(static_cast<size_t>(n), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), len, w.data(), n) != n) return false;
    out = std::move(w);
    return true;
}

std::string JsonEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
            break;
        }
    }
    return out;
}

namespace {

// Length of a UTF-8 sequence from its lead byte, 0 if the byte cannot start one.
size_t Utf8SeqLength(unsigned char lead) {
    if (lead >= 0xC2 && lead <= 0xDF) return 2;
    if (lead >= 0xE0 && lead <= 0xEF) return 3;
    if (lead >= 0xF0 && lead <= 0xF4) return 4;
    return 0;
}

}  // namespace

std::string XmlEscape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 16);
    size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            ++i;
            switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:
                // XML 1.0 forbids control characters other than TAB, LF, CR: drop them.
                if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') break;
                out += static_cast<char>(c);
                break;
            }
            continue;
        }

        // Multi-byte sequence: decode to find U+FFFE / U+FFFF; otherwise pass the bytes through.
        const size_t n = Utf8SeqLength(c);
        bool valid = n != 0 && i + n <= s.size();
        uint32_t cp = n == 2 ? (c & 0x1Fu) : n == 3 ? (c & 0x0Fu) : (c & 0x07u);
        for (size_t k = 1; valid && k < n; ++k) {
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0u) != 0x80u) {
                valid = false;
            } else {
                cp = (cp << 6) | (cc & 0x3Fu);
            }
        }
        if (!valid) {
            out += static_cast<char>(c);
            ++i;
            continue;
        }
        if (cp != 0xFFFEu && cp != 0xFFFFu) out.append(s.substr(i, n));
        i += n;
    }
    return out;
}

std::wstring NormalizeNewlinesToLf(std::wstring_view s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'\r') {
            out += L'\n';
            if (i + 1 < s.size() && s[i + 1] == L'\n') ++i;
        } else {
            out += s[i];
        }
    }
    return out;
}

bool StartsWithFormulaChar(std::wstring_view s) {
    if (s.empty()) return false;
    const wchar_t c = s[0];
    return c == L'=' || c == L'+' || c == L'-' || c == L'@';
}
