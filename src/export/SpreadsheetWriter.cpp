// Minimal SpreadsheetML writer (Spec 7.2-7.6). One sheet "Sessions", inline strings, no formulas.
#include "export/SpreadsheetWriter.h"

#include <cstdio>
#include <string>
#include "util/StringHelpers.h"
#include "util/TimeHelpers.h"

namespace {

constexpr char kXmlDecl[] = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
constexpr char kMainNs[] = "http://schemas.openxmlformats.org/spreadsheetml/2006/main";
constexpr char kSheetPath[] = "xl/worksheets/sheet1.xml";

// Style indices (cellXfs in styles.xml).
constexpr int kStyleDate = 1;
constexpr int kStyleTime = 2;
constexpr int kStyleMinutes = 3;
constexpr int kStyleDuration = 4;
constexpr int kStyleNotes = 5;
constexpr int kStyleHeader = 6;

// Number text independent of the C locale (setlocale is never called, so "C" applies).
std::string Num(double v) {
    char buf[40];
    const int n = snprintf(buf, sizeof buf, "%.15g", v);
    return std::string(buf, n > 0 ? static_cast<size_t>(n) : 0);
}

int64_t FloorDiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

struct LocalParts {
    int64_t excelDate;   // Excel serial of the local date
    double  dayFraction; // local time of day as fraction of a day
};

LocalParts SplitLocal(int64_t utc, int32_t offsetMin) {
    const int64_t local = utc + static_cast<int64_t>(offsetMin) * 60;
    const int64_t days = FloorDiv(local, 86400);
    const int64_t sod = ((local % 86400) + 86400) % 86400;
    int y = 0, m = 0, d = 0;
    CivilFromDays(days, y, m, d);
    return {ExcelSerialFromCivil(y, m, d), static_cast<double>(sod) / 86400.0};
}

void AppendNumCell(std::string& out, char col, int row, int style, const std::string& value) {
    out += "<c r=\"";
    out += col;
    out += std::to_string(row);
    out += "\" s=\"";
    out += std::to_string(style);
    out += "\"><v>";
    out += value;
    out += "</v></c>";
}

void AppendStringCell(std::string& out, char col, int row, int style, const std::string& escapedUtf8) {
    out += "<c r=\"";
    out += col;
    out += std::to_string(row);
    out += "\" t=\"inlineStr\" s=\"";
    out += std::to_string(style);
    out += "\"><is><t xml:space=\"preserve\">";
    out += escapedUtf8;
    out += "</t></is></c>";
}

Status WritePart(ZipWriter& zip, std::string_view name, const std::string& content) {
    Status st = zip.BeginEntry(name);
    if (!st) return st;
    st = zip.Write(content);
    if (!st) return st;
    return zip.EndEntry();
}

}  // namespace

Status SheetStream::Begin() {
    Status st = zip_.BeginEntry(kSheetPath);
    if (!st) return st;

    std::string x = kXmlDecl;
    x += "<worksheet xmlns=\"";
    x += kMainNs;
    x += "\">";
    x += "<sheetViews><sheetView workbookViewId=\"0\">"
         "<pane ySplit=\"1\" topLeftCell=\"A2\" activePane=\"bottomLeft\" state=\"frozen\"/>"
         "</sheetView></sheetViews>";
    x += "<cols>"
         "<col min=\"1\" max=\"1\" width=\"12\" customWidth=\"1\"/>"
         "<col min=\"2\" max=\"2\" width=\"11\" customWidth=\"1\"/>"
         "<col min=\"3\" max=\"3\" width=\"11\" customWidth=\"1\"/>"
         "<col min=\"4\" max=\"4\" width=\"18\" customWidth=\"1\"/>"
         "<col min=\"5\" max=\"5\" width=\"19\" customWidth=\"1\"/>"
         "<col min=\"6\" max=\"6\" width=\"60\" style=\"5\" customWidth=\"1\"/>"
         "</cols>";
    x += "<sheetData>";

    static const char* const kHeaders[6] = {"Date",     "Start Time",         "End Time",
                                            "Duration (Minutes)", "Duration (HH:MM:SS)", "Notes / Ticket"};
    x += "<row r=\"1\">";
    for (int i = 0; i < 6; ++i)
        AppendStringCell(x, static_cast<char>('A' + i), 1, kStyleHeader, kHeaders[i]);
    x += "</row>";
    row_ = 1;
    return zip_.Write(x);
}

Status SheetStream::AddSession(const CompletedSession& s) {
    const int r = row_ + 1;
    const LocalParts start = SplitLocal(s.startUtc, s.startOffsetMin);
    const LocalParts end = SplitLocal(s.endUtc, s.endOffsetMin);
    const int64_t dur = s.durationSeconds < 0 ? 0 : s.durationSeconds;

    std::string x;
    x.reserve(512 + s.notes.size() * 2);
    x += "<row r=\"";
    x += std::to_string(r);
    x += "\">";
    AppendNumCell(x, 'A', r, kStyleDate, std::to_string(start.excelDate));
    AppendNumCell(x, 'B', r, kStyleTime, Num(start.dayFraction));
    AppendNumCell(x, 'C', r, kStyleTime, Num(end.dayFraction));
    AppendNumCell(x, 'D', r, kStyleMinutes, Num(static_cast<double>(dur) / 60.0));
    AppendNumCell(x, 'E', r, kStyleDuration, Num(static_cast<double>(dur) / 86400.0));
    AppendStringCell(x, 'F', r, kStyleNotes, XmlEscape(Utf8FromWide(NormalizeNewlinesToLf(s.notes))));
    x += "</row>";

    Status st = zip_.Write(x);
    if (!st) return st;
    row_ = r;
    return Status::Ok();
}

Status SheetStream::End() {
    Status st = zip_.Write(std::string_view("</sheetData></worksheet>"));
    if (!st) return st;
    return zip_.EndEntry();
}

Status WriteStaticParts(ZipWriter& zip) {
    const std::string decl = kXmlDecl;

    const std::string contentTypes =
        decl +
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/worksheets/sheet1.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>"
        "<Override PartName=\"/xl/styles.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>"
        "</Types>";

    const std::string rootRels =
        decl +
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
        "Target=\"xl/workbook.xml\"/>"
        "</Relationships>";

    const std::string workbook =
        decl + "<workbook xmlns=\"" + kMainNs +
        "\" xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<sheets><sheet name=\"Sessions\" sheetId=\"1\" r:id=\"rId1\"/></sheets>"
        "</workbook>";

    const std::string workbookRels =
        decl +
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" "
        "Target=\"worksheets/sheet1.xml\"/>"
        "<Relationship Id=\"rId2\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
        "Target=\"styles.xml\"/>"
        "</Relationships>";

    const std::string styles =
        decl + "<styleSheet xmlns=\"" + kMainNs + "\">" +
        "<numFmts count=\"3\">"
        "<numFmt numFmtId=\"164\" formatCode=\"yyyy-mm-dd\"/>"
        "<numFmt numFmtId=\"165\" formatCode=\"hh:mm:ss\"/>"
        "<numFmt numFmtId=\"166\" formatCode=\"[h]:mm:ss\"/>"
        "</numFmts>"
        "<fonts count=\"2\">"
        "<font><sz val=\"11\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"
        "<font><b/><sz val=\"11\"/><name val=\"Calibri\"/><family val=\"2\"/></font>"
        "</fonts>"
        "<fills count=\"2\">"
        "<fill><patternFill patternType=\"none\"/></fill>"
        "<fill><patternFill patternType=\"gray125\"/></fill>"
        "</fills>"
        "<borders count=\"1\"><border><left/><right/><top/><bottom/><diagonal/></border></borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        "<cellXfs count=\"7\">"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
        "<xf numFmtId=\"164\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
        "<xf numFmtId=\"165\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
        "<xf numFmtId=\"2\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
        "<xf numFmtId=\"166\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyNumberFormat=\"1\"/>"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyAlignment=\"1\">"
        "<alignment vertical=\"top\" wrapText=\"1\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>"
        "</cellXfs>"
        "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
        "</styleSheet>";

    Status st = WritePart(zip, "[Content_Types].xml", contentTypes);
    if (!st) return st;
    st = WritePart(zip, "_rels/.rels", rootRels);
    if (!st) return st;
    st = WritePart(zip, "xl/workbook.xml", workbook);
    if (!st) return st;
    st = WritePart(zip, "xl/_rels/workbook.xml.rels", workbookRels);
    if (!st) return st;
    return WritePart(zip, "xl/styles.xml", styles);
}
