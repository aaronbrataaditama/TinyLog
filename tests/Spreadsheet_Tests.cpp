// SheetStream / WriteStaticParts / ExportWorkbook tests (WP-D). Sessions are built in memory;
// nothing here depends on the JsonLinesStore implementation except the one skip-guarded export test.
#include "TestHarness.h"

#include <windows.h>
#include <psapi.h>

#include <cstdint>
#include <string>
#include <vector>

#include "export/ExportController.h"
#include "export/SpreadsheetWriter.h"
#include "export/ZipWriter.h"
#include "storage/JsonLinesStore.h"
#include "util/Crc32.h"
#include "util/FileHelpers.h"
#include "util/Handles.h"
#include "util/TimeHelpers.h"

namespace {

struct ZPart {
    std::string name;
    std::string data;
};

uint16_t U16(const std::string& b, size_t p) {
    return static_cast<uint16_t>(static_cast<uint8_t>(b[p]) | (static_cast<uint8_t>(b[p + 1]) << 8));
}
uint32_t U32(const std::string& b, size_t p) {
    return static_cast<uint32_t>(U16(b, p)) | (static_cast<uint32_t>(U16(b, p + 2)) << 16);
}

// Minimal central-directory reader that also verifies CRCs. Returns "" on success.
std::string Unzip(const std::string& b, std::vector<ZPart>& out) {
    out.clear();
    if (b.size() < 22 || U32(b, b.size() - 22) != 0x06054b50) return "no EOCD";
    const size_t eocd = b.size() - 22;
    const size_t count = U16(b, eocd + 10);
    size_t pos = U32(b, eocd + 16);
    for (size_t i = 0; i < count; ++i) {
        if (pos + 46 > eocd || U32(b, pos) != 0x02014b50) return "bad central header";
        const uint32_t crc = U32(b, pos + 16);
        const uint32_t size = U32(b, pos + 24);
        const size_t nlen = U16(b, pos + 28);
        const size_t lo = U32(b, pos + 42);
        if (lo + 30 > eocd || U32(b, lo) != 0x04034b50) return "bad local header";
        const size_t dataOff = lo + 30 + U16(b, lo + 26);
        if (dataOff + size > eocd) return "data out of bounds";
        ZPart p;
        p.name = b.substr(pos + 46, nlen);
        p.data = b.substr(dataOff, size);
        if (Crc32Update(0, p.data.data(), p.data.size()) != crc) return "crc mismatch in " + p.name;
        out.push_back(std::move(p));
        pos += 46 + nlen;
    }
    return "";
}

const ZPart* Find(const std::vector<ZPart>& parts, const std::string& name) {
    for (const ZPart& p : parts)
        if (p.name == name) return &p;
    return nullptr;
}

bool Has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

CompletedSession Make(int y, int m, int d, int hh, int mm, int ss, int offsetMin, int64_t durSec,
                      std::wstring notes) {
    CompletedSession s;
    s.id = "00000000-0000-0000-0000-000000000001";
    const int64_t localSec = DaysFromCivil(y, m, d) * 86400 + hh * 3600 + mm * 60 + ss;
    s.startUtc = localSec - static_cast<int64_t>(offsetMin) * 60;
    s.endUtc = s.startUtc + durSec;
    s.startOffsetMin = offsetMin;
    s.endOffsetMin = offsetMin;
    s.durationSeconds = durSec;
    s.notes = std::move(notes);
    return s;
}

Status WriteWorkbook(const std::wstring& path, const std::vector<CompletedSession>& sessions) {
    UniqueHandle f(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!f.valid()) return Status::FromLastError(L"create");
    ZipWriter zip;
    Status st = zip.Open(f.get());
    if (!st) return st;
    st = WriteStaticParts(zip);
    if (!st) return st;
    SheetStream sheet(zip);
    st = sheet.Begin();
    if (!st) return st;
    for (const CompletedSession& s : sessions) {
        st = sheet.AddSession(s);
        if (!st) return st;
    }
    st = sheet.End();
    if (!st) return st;
    return zip.Finish();
}

std::string SheetOf(const std::wstring& path, std::vector<ZPart>* partsOut = nullptr) {
    std::string bytes;
    if (!ReadWholeFile(path, bytes).ok) return "<<read failed>>";
    std::vector<ZPart> parts;
    const std::string err = Unzip(bytes, parts);
    if (!err.empty()) return "<<" + err + ">>";
    const ZPart* sheet = Find(parts, "xl/worksheets/sheet1.xml");
    if (!sheet) return "<<no sheet>>";
    if (partsOut) *partsOut = parts;
    return sheet->data;
}

std::vector<CompletedSession> SampleSessions() {
    std::vector<CompletedSession> v;
    v.push_back(Make(2026, 11, 17, 9, 0, 0, 480, 2730, L"ABC-123"));                       // 45.5 min
    v.push_back(Make(2026, 11, 17, 10, 0, 0, 480, 4500, L"=1+1"));                         // formula-like
    v.push_back(Make(2026, 11, 17, 8, 0, 0, 480, 90000, L"long: 25 hours"));              // 25 h
    v.push_back(Make(2026, 11, 17, 23, 30, 0, -300, 3600, L"line1\r\nline2 & <b> \"q\" 'a'"));
    v.push_back(Make(2026, 11, 18, 1, 0, 0, 0, 60, L"ctl\x01 stripped"));
    v.push_back(Make(2026, 11, 18, 2, 0, 0, 0, 60, L"café 日本"));
    v.push_back(Make(2026, 11, 18, 3, 0, 0, 0, 0, L""));
    return v;
}

}  // namespace

TEST(Sheet_StaticPartsPresentAndWellFormedish) {
    TempDir dir;
    const std::wstring path = dir.File(L"w.xlsx");
    CHECK(WriteWorkbook(path, SampleSessions()).ok);
    std::string bytes;
    CHECK(ReadWholeFile(path, bytes).ok);
    std::vector<ZPart> parts;
    CHECK_EQ(Unzip(bytes, parts), std::string());
    CHECK_EQ(parts.size(), size_t(6));
    const char* names[6] = {"[Content_Types].xml", "_rels/.rels", "xl/workbook.xml", "xl/_rels/workbook.xml.rels",
                            "xl/styles.xml", "xl/worksheets/sheet1.xml"};
    for (const char* n : names) {
        const ZPart* p = Find(parts, n);
        CHECK(p != nullptr);
        if (p) {
            CHECK(p->data.rfind("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n", 0) == 0);
        }
    }
    if (const ZPart* wb = Find(parts, "xl/workbook.xml")) {
        CHECK(Has(wb->data, "<sheet name=\"Sessions\" sheetId=\"1\" r:id=\"rId1\"/>"));
    }
    if (const ZPart* rels = Find(parts, "xl/_rels/workbook.xml.rels")) {
        CHECK(Has(rels->data, "Id=\"rId1\""));
        CHECK(Has(rels->data, "Target=\"worksheets/sheet1.xml\""));
        CHECK(Has(rels->data, "Id=\"rId2\""));
        CHECK(Has(rels->data, "Target=\"styles.xml\""));
    }
    if (const ZPart* ct = Find(parts, "[Content_Types].xml")) {
        CHECK(Has(ct->data, "spreadsheetml.sheet.main+xml"));
        CHECK(Has(ct->data, "spreadsheetml.worksheet+xml"));
        CHECK(Has(ct->data, "spreadsheetml.styles+xml"));
        CHECK(Has(ct->data, "Extension=\"rels\""));
        CHECK(Has(ct->data, "Extension=\"xml\""));
    }
    if (const ZPart* st = Find(parts, "xl/styles.xml")) {
        CHECK(Has(st->data, "numFmtId=\"164\" formatCode=\"yyyy-mm-dd\""));
        CHECK(Has(st->data, "numFmtId=\"165\" formatCode=\"hh:mm:ss\""));
        CHECK(Has(st->data, "numFmtId=\"166\" formatCode=\"[h]:mm:ss\""));
        CHECK(Has(st->data, "<cellXfs count=\"7\">"));
        CHECK(Has(st->data, "wrapText=\"1\""));
        CHECK(Has(st->data, "<cellStyle name=\"Normal\""));
        // schema element order
        const size_t a = st->data.find("<numFmts"), b = st->data.find("<fonts"), c = st->data.find("<fills"),
                     d = st->data.find("<borders"), e = st->data.find("<cellStyleXfs"),
                     f = st->data.find("<cellXfs"), g = st->data.find("<cellStyles");
        CHECK(a < b && b < c && c < d && d < e && e < f && f < g);
    }
}

TEST(Sheet_HeaderAndLayout) {
    TempDir dir;
    const std::wstring path = dir.File(L"h.xlsx");
    CHECK(WriteWorkbook(path, SampleSessions()).ok);
    const std::string sheet = SheetOf(path);
    CHECK(Has(sheet, "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"));
    CHECK(Has(sheet, "<sheetViews><sheetView workbookViewId=\"0\"><pane ySplit=\"1\" topLeftCell=\"A2\" "
                     "activePane=\"bottomLeft\" state=\"frozen\"/></sheetView></sheetViews>"));
    const size_t views = sheet.find("<sheetViews>"), cols = sheet.find("<cols>"), data = sheet.find("<sheetData>");
    CHECK(views != std::string::npos && views < cols && cols < data);
    for (const char* w : {"width=\"12\"", "width=\"11\"", "width=\"18\"", "width=\"19\"", "width=\"60\""})
        CHECK(Has(sheet, w));
    const char* headers[6] = {"Date", "Start Time", "End Time", "Duration (Minutes)", "Duration (HH:MM:SS)",
                              "Notes / Ticket"};
    for (int i = 0; i < 6; ++i) {
        const std::string cell = std::string("<c r=\"") + static_cast<char>('A' + i) +
                                 "1\" t=\"inlineStr\" s=\"6\"><is><t xml:space=\"preserve\">" + headers[i] +
                                 "</t></is></c>";
        CHECK(Has(sheet, cell));
    }
    CHECK(sheet.size() > 24 && sheet.substr(sheet.size() - 24) == "</sheetData></worksheet>");
}

TEST(Sheet_RowCellsAndValues) {
    TempDir dir;
    const std::wstring path = dir.File(L"r.xlsx");
    CHECK(WriteWorkbook(path, SampleSessions()).ok);
    const std::string sheet = SheetOf(path);

    // Independent anchor: 2024-01-01 is Excel serial 45292.
    CHECK_EQ(ExcelSerialFromCivil(2024, 1, 1), int64_t(45292));
    const std::string serial = std::to_string(ExcelSerialFromCivil(2026, 11, 17));

    // Row 2: 09:00:00 local, 45.5 minutes.
    CHECK(Has(sheet, "<row r=\"2\"><c r=\"A2\" s=\"1\"><v>" + serial + "</v></c>"));
    CHECK(Has(sheet, "<c r=\"B2\" s=\"2\"><v>0.375</v></c>"));  // 09:00 = 0.375
    CHECK(Has(sheet, "<c r=\"C2\" s=\"2\"><v>0.390625</v></c>") == false);  // 09:45:30 is not 09:45
    CHECK(Has(sheet, "<c r=\"C2\" s=\"2\"><v>0.4065972222222"));            // 35130 / 86400
    CHECK(Has(sheet, "<c r=\"D2\" s=\"3\"><v>45.5</v></c>"));
    CHECK(Has(sheet, "<c r=\"E2\" s=\"4\"><v>0.0315972222222"));            // 2730 / 86400
    CHECK(Has(sheet, "<c r=\"F2\" t=\"inlineStr\" s=\"5\"><is><t xml:space=\"preserve\">ABC-123</t></is></c>"));

    // Row 3: formula-like note stays a string, and no formula element exists anywhere.
    CHECK(Has(sheet, "<c r=\"F3\" t=\"inlineStr\" s=\"5\"><is><t xml:space=\"preserve\">=1+1</t></is></c>"));
    CHECK(!Has(sheet, "<f>"));
    CHECK(!Has(sheet, "<f "));

    // Row 4: 25 hours.
    CHECK(Has(sheet, "<c r=\"D4\" s=\"3\"><v>1500</v></c>"));
    CHECK(Has(sheet, "<c r=\"E4\" s=\"4\"><v>1.0416666666666"));

    // Row 5: UTC-5, 23:30 local; date stays the 17th even though UTC is already the 18th.
    CHECK(Has(sheet, "<c r=\"A5\" s=\"1\"><v>" + serial + "</v></c>"));
    CHECK(Has(sheet, "<c r=\"B5\" s=\"2\"><v>0.979166666666667</v></c>"));
    CHECK(Has(sheet, "<c r=\"C5\" s=\"2\"><v>0.0208333333333333</v></c>"));  // end 00:30 next day
    // Newlines normalised to LF, XML escaped.
    CHECK(Has(sheet, "line1\nline2 &amp; &lt;b&gt; &quot;q&quot; &apos;a&apos;"));
    CHECK(!Has(sheet, "\r"));

    // Row 6: control char stripped.
    CHECK(Has(sheet, "ctl stripped"));
    CHECK(!Has(sheet, "\x01"));
    // Row 7: Unicode as UTF-8 bytes.
    CHECK(Has(sheet, "caf\xC3\xA9 \xE6\x97\xA5\xE6\x9C\xAC"));
    // Row 8: empty notes and zero duration.
    CHECK(Has(sheet, "<c r=\"D8\" s=\"3\"><v>0</v></c>"));
    CHECK(Has(sheet, "<c r=\"F8\" t=\"inlineStr\" s=\"5\"><is><t xml:space=\"preserve\"></t></is></c>"));
    CHECK(Has(sheet, "<row r=\"8\">"));
    CHECK(!Has(sheet, "<row r=\"9\">"));
}

TEST(Sheet_EmptySheetIsHeaderOnly) {
    TempDir dir;
    const std::wstring path = dir.File(L"e.xlsx");
    CHECK(WriteWorkbook(path, {}).ok);
    std::vector<ZPart> parts;
    const std::string sheet = SheetOf(path, &parts);
    CHECK_EQ(parts.size(), size_t(6));
    CHECK(Has(sheet, "<row r=\"1\">"));
    CHECK(!Has(sheet, "<row r=\"2\">"));
    CHECK(Has(sheet, "Notes / Ticket"));
    CHECK(sheet.size() > 24 && sheet.substr(sheet.size() - 24) == "</sheetData></worksheet>");
}

TEST(Sheet_TenThousandRowsBoundedMemory) {
    TempDir dir;
    const std::wstring path = dir.File(L"big.xlsx");
    UniqueHandle f(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr));
    CHECK(f.valid());
    PROCESS_MEMORY_COUNTERS_EX before{};
    before.cb = sizeof before;
    CHECK(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&before),
                               sizeof before) != 0);
    ZipWriter zip;
    CHECK(zip.Open(f.get()).ok);
    CHECK(WriteStaticParts(zip).ok);
    SheetStream sheet(zip);
    CHECK(sheet.Begin().ok);
    CompletedSession s = Make(2026, 11, 17, 9, 0, 0, 480, 2730, L"ticket ABC-123 some note text");
    bool ok = true;
    for (int i = 0; i < 10000 && ok; ++i) {
        s.startUtc += 3600;
        s.endUtc += 3600;
        ok = sheet.AddSession(s).ok;
    }
    CHECK(ok);
    CHECK(sheet.End().ok);
    CHECK(zip.Finish().ok);
    PROCESS_MEMORY_COUNTERS_EX after{};
    after.cb = sizeof after;
    CHECK(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&after),
                               sizeof after) != 0);
    const SIZE_T delta = after.PrivateUsage > before.PrivateUsage ? after.PrivateUsage - before.PrivateUsage : 0;
    CHECK(delta < 15u * 1024u * 1024u);
    f.reset();
    const std::string xml = SheetOf(path);
    CHECK(Has(xml, "<row r=\"10001\">"));
    CHECK(!Has(xml, "<row r=\"10002\">"));
}

TEST(Sheet_ExportWorkbookEmptyLog) {
    TempDir dir;
    JsonLinesStore store(dir.File(L"TinyLog.jsonl"));
    size_t probe = 0;
    const Status scan = store.ForEach([&](const CompletedSession&) { ++probe; return true; });
    if (!scan.ok) {
        std::printf("  [skipped] Sheet_ExportWorkbookEmptyLog: JsonLinesStore::ForEach not implemented yet\n");
        return;
    }
    const std::wstring dest = dir.File(L"out.xlsx");
    size_t rows = 99;
    const Status st = ExportWorkbook(store, dest, rows);
    CHECK(st.ok);
    CHECK_EQ(rows, size_t(0));
    CHECK(FileExists(dest));
    CHECK(!FileExists(dest + L".tmp-" + std::to_wstring(GetCurrentProcessId())));
    std::vector<ZPart> parts;
    const std::string sheet = SheetOf(dest, &parts);
    CHECK_EQ(parts.size(), size_t(6));
    CHECK(Has(sheet, "Notes / Ticket"));
    CHECK(!Has(sheet, "<row r=\"2\">"));

    // Overwrites an existing destination.
    rows = 99;
    CHECK(ExportWorkbook(store, dest, rows).ok);
    CHECK_EQ(rows, size_t(0));
}

TEST(Sheet_ExportWorkbookBadDestinationFails) {
    TempDir dir;
    JsonLinesStore store(dir.File(L"TinyLog.jsonl"));
    size_t rows = 5;
    const Status st = ExportWorkbook(store, dir.File(L"no-such-dir\\out.xlsx"), rows);
    CHECK(!st.ok);
    CHECK_EQ(rows, size_t(0));
}

// Writes a sample workbook for opening in Excel when TINYLOG_SAMPLE_XLSX is set to a target path.
TEST(Sheet_WriteSampleForManualInspection) {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetEnvironmentVariableW(L"TINYLOG_SAMPLE_XLSX", buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) return;
    std::vector<CompletedSession> v = SampleSessions();
    v.push_back(Make(2026, 3, 1, 9, 0, 0, 480, 2 * 86400 + 3 * 3600 + 15 * 60, L"multi-day: [h]:mm:ss shows 51:15:00"));
    v.push_back(Make(2024, 2, 29, 12, 0, 0, 0, 1800, L"leap day 2024-02-29"));
    const Status st = WriteWorkbook(buf, v);
    CHECK(st.ok);
    if (st.ok) std::printf("  sample workbook written: %s\n", th::Narrow(buf).c_str());
}
