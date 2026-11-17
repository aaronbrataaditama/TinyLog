#pragma once
// FROZEN CONTRACT (Wave 0). Implementation: StringHelpers.cpp (WP-A).
#include <string>
#include <string_view>

std::string  Utf8FromWide(std::wstring_view s);
bool         WideFromUtf8(std::string_view s, std::wstring& out);   // false on invalid UTF-8 (out unspecified)

// JSON string-content escaping: " \ \b \f \n \r \t; other bytes < 0x20 as \u00XX; bytes >= 0x80 pass through.
std::string  JsonEscape(std::string_view utf8);

// XML text/attribute escaping: & < > " ' -> &amp; &lt; &gt; &quot; &apos;. Also removes characters
// forbidden in XML 1.0: code points < 0x20 except \t \n \r, and U+FFFE / U+FFFF. Input is UTF-8.
std::string  XmlEscape(std::string_view utf8);

std::wstring NormalizeNewlinesToLf(std::wstring_view s);   // "\r\n" and lone "\r" -> "\n"
bool         StartsWithFormulaChar(std::wstring_view s);   // first char is = + - @
