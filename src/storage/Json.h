#pragma once
// FROZEN CONTRACT (Wave 0). Implementation: Json.cpp (WP-C).
// Minimal flat-object JSON codec: only what TinyLog's two schemas need.
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct JsonValue {
    enum Kind { Null, Number, String } kind = Null;
    int64_t num = 0;      // Number: integers only (optional leading '-')
    std::string str;      // String: decoded UTF-8 (\uXXXX incl. surrogate pairs decoded)
};

// Parses one object {"k": v, ...} with string / integer / null values, surrounding whitespace allowed.
// Rejects nested objects/arrays, true/false, fractions/exponents, duplicate-free not required,
// invalid escapes, lone surrogates, and any trailing non-whitespace.
bool ParseFlatJsonObject(std::string_view text, std::vector<std::pair<std::string, JsonValue>>& out);

// Builds a compact single-line object in call order: {"a":1,"b":"x","c":null}
class JsonObjectWriter {
public:
    void Str(std::string_view key, std::string_view utf8Value);   // value escaped with JsonEscape
    void Num(std::string_view key, int64_t value);
    void Null(std::string_view key);
    std::string Finish();                                         // returns the object; writer is then empty

private:
    std::string buf_;
};
