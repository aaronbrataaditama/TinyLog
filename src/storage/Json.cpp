// Minimal flat-object JSON codec (WP-C). Only what TinyLog's two schemas need.
#include "storage/Json.h"

#include <cstddef>
#include <cstdint>

#include "util/StringHelpers.h"

namespace {

class Parser {
public:
    explicit Parser(std::string_view t) : t_(t) {}

    bool ParseObject(std::vector<std::pair<std::string, JsonValue>>& out) {
        out.clear();
        SkipWs();
        if (!Eat('{')) return false;
        SkipWs();
        if (Eat('}')) return AtEnd();
        for (;;) {
            SkipWs();
            std::string key;
            if (!ParseString(key)) return false;
            SkipWs();
            if (!Eat(':')) return false;
            SkipWs();
            JsonValue v;
            if (!ParseValue(v)) return false;
            out.emplace_back(std::move(key), std::move(v));
            SkipWs();
            if (Eat(',')) continue;
            if (Eat('}')) return AtEnd();
            return false;
        }
    }

private:
    bool AtEnd() {
        SkipWs();
        return pos_ == t_.size();
    }
    void SkipWs() {
        while (pos_ < t_.size()) {
            const char c = t_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }
    bool Eat(char c) {
        if (pos_ < t_.size() && t_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }
    bool EatWord(std::string_view w) {
        if (t_.substr(pos_, w.size()) == w) {
            pos_ += w.size();
            return true;
        }
        return false;
    }

    bool ParseValue(JsonValue& v) {
        if (pos_ >= t_.size()) return false;
        const char c = t_[pos_];
        if (c == '"') {
            v.kind = JsonValue::String;
            return ParseString(v.str);
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            v.kind = JsonValue::Number;
            return ParseNumber(v.num);
        }
        if (c == 'n') {
            if (!EatWord("null")) return false;
            v.kind = JsonValue::Null;
            return true;
        }
        return false;   // objects, arrays, true/false
    }

    bool ParseNumber(int64_t& out) {
        bool neg = false;
        if (Eat('-')) neg = true;
        if (pos_ >= t_.size() || t_[pos_] < '0' || t_[pos_] > '9') return false;
        if (t_[pos_] == '0' && pos_ + 1 < t_.size() && t_[pos_ + 1] >= '0' && t_[pos_ + 1] <= '9') return false;
        uint64_t acc = 0;
        constexpr uint64_t kLimit = 9223372036854775808ULL;   // 2^63 (magnitude of INT64_MIN)
        while (pos_ < t_.size() && t_[pos_] >= '0' && t_[pos_] <= '9') {
            const uint64_t d = static_cast<uint64_t>(t_[pos_] - '0');
            if (acc > (kLimit - d) / 10) return false;   // overflow
            acc = acc * 10 + d;
            ++pos_;
        }
        // A fraction or exponent makes this not an integer.
        if (pos_ < t_.size() && (t_[pos_] == '.' || t_[pos_] == 'e' || t_[pos_] == 'E')) return false;
        if (neg) {
            out = (acc == kLimit) ? INT64_MIN : -static_cast<int64_t>(acc);
        } else {
            if (acc >= kLimit) return false;
            out = static_cast<int64_t>(acc);
        }
        return true;
    }

    bool Hex4(uint32_t& out) {
        if (pos_ + 4 > t_.size()) return false;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = t_[pos_ + static_cast<size_t>(i)];
            uint32_t d;
            if (c >= '0' && c <= '9') d = static_cast<uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') d = static_cast<uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = static_cast<uint32_t>(c - 'A' + 10);
            else return false;
            v = v * 16 + d;
        }
        pos_ += 4;
        out = v;
        return true;
    }

    static void AppendUtf8(std::string& s, uint32_t cp) {
        if (cp < 0x80) {
            s += static_cast<char>(cp);
        } else if (cp < 0x800) {
            s += static_cast<char>(0xC0 | (cp >> 6));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            s += static_cast<char>(0xE0 | (cp >> 12));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            s += static_cast<char>(0xF0 | (cp >> 18));
            s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            s += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool ParseString(std::string& out) {
        out.clear();
        if (!Eat('"')) return false;
        for (;;) {
            if (pos_ >= t_.size()) return false;
            const unsigned char c = static_cast<unsigned char>(t_[pos_++]);
            if (c == '"') return true;
            if (c < 0x20) return false;   // raw control characters are invalid JSON
            if (c != '\\') {
                out += static_cast<char>(c);
                continue;
            }
            if (pos_ >= t_.size()) return false;
            const char e = t_[pos_++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                uint32_t cp = 0;
                if (!Hex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (pos_ + 2 > t_.size() || t_[pos_] != '\\' || t_[pos_ + 1] != 'u') return false;
                    pos_ += 2;
                    uint32_t lo = 0;
                    if (!Hex4(lo)) return false;
                    if (lo < 0xDC00 || lo > 0xDFFF) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return false;   // lone low surrogate
                }
                AppendUtf8(out, cp);
                break;
            }
            default: return false;
            }
        }
    }

    std::string_view t_;
    size_t pos_ = 0;
};

}  // namespace

bool ParseFlatJsonObject(std::string_view text, std::vector<std::pair<std::string, JsonValue>>& out) {
    Parser p(text);
    if (!p.ParseObject(out)) {
        out.clear();
        return false;
    }
    return true;
}

void JsonObjectWriter::Str(std::string_view key, std::string_view utf8Value) {
    if (!buf_.empty()) buf_ += ',';
    buf_ += '"';
    buf_ += JsonEscape(key);
    buf_ += "\":\"";
    buf_ += JsonEscape(utf8Value);
    buf_ += '"';
}

void JsonObjectWriter::Num(std::string_view key, int64_t value) {
    if (!buf_.empty()) buf_ += ',';
    buf_ += '"';
    buf_ += JsonEscape(key);
    buf_ += "\":";
    buf_ += std::to_string(value);
}

void JsonObjectWriter::Null(std::string_view key) {
    if (!buf_.empty()) buf_ += ',';
    buf_ += '"';
    buf_ += JsonEscape(key);
    buf_ += "\":null";
}

std::string JsonObjectWriter::Finish() {
    std::string out;
    out.reserve(buf_.size() + 2);
    out += '{';
    out += buf_;
    out += '}';
    buf_.clear();
    return out;
}
